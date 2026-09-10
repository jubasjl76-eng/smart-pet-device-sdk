// spd_device.h — SmartPetDevice: the orchestrator a sketch talks to.
//
//   SmartPetDevice dev("feeder");
//   void setup() { dev.begin();
//     dev.onCommand("feed", [](JsonObjectConst p, String){ feeder.dispense(p["amount"]|40.0f); return true; });
//     dev.onScheduledAction([](float amount){ feeder.dispense(amount); });
//   }
//   void loop() { dev.loop(); }
//
// Built-in commands handled for you: restart, ota, identify, schedule_set, set_interval.
// Offline handling: when MQTT drops, actions you report via dev.reportAction(...) are
// journalled and replayed (as an "offline_recovered" event) on reconnect.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <map>
#include "spd_config.h"
#include "spd_wifi.h"
#include "spd_time.h"
#include "spd_mqtt.h"
#include "spd_ota.h"
#include "spd_schedule.h"
#include "spd_offline_journal.h"
#include "spd_crash.h"

#ifndef SPD_FW_VERSION
#define SPD_FW_VERSION "0.1.0"
#endif

namespace spd {

using CommandFn = std::function<bool(JsonObjectConst params, const String& id)>;
using ScheduledFn = std::function<void(float amount)>;
using StatusFillFn = std::function<void(JsonObject& status)>;
using AudioSignalFn = std::function<void(const String& kind, JsonObjectConst signal)>;

class SmartPetDevice {
 public:
  explicit SmartPetDevice(const char* deviceType) : deviceType_(deviceType) {}

  void begin() {
    Serial.begin(115200);
    delay(100);
    bootCrash_ = readCrashInfo();
    if (bootCrash_.isCrash) Serial.printf("[spd] recovered from a crash: %s\n", bootCrash_.reason);
    cfg_ = store_.load();
    if (cfg_.deviceType.isEmpty()) cfg_.deviceType = deviceType_;

    wifi_.begin();
    time_.begin(cfg_.timezone);
    mqtt_.begin([this](const String& t, JsonObjectConst p) { onMqtt(t, p); });
    otaBegun_ = false;

    // restore any schedule + a half-open journal from a previous power cycle
    restoreSchedule();
    String j = store_.getBlobString("journal");
    if (j.length()) journal_.deserialize(std::string(j.c_str()));
  }

  void loop() {
    bool wifiUp = wifi_.poll();
    bool timeOk = time_.poll();

    if (wifiUp && !otaBegun_) { ota_.begin(cfg_); otaBegun_ = true; }
    if (otaBegun_) ota_.loop();

    if (wifiUp) mqtt_.loop();

    bool online = wifiUp && mqtt_.connected();
    handleConnectivity(online, timeOk);

    if (online && timeOk) tickSchedule();

    if (online && millis() - lastStatusMs_ > statusEveryMs_) {
      publishStatus();
      lastStatusMs_ = millis();
    }
  }

  // ── registration ─────────────────────────────────────────────────────────
  SmartPetDevice& onCommand(const String& name, CommandFn fn) { handlers_[name] = std::move(fn); return *this; }
  SmartPetDevice& onScheduledAction(ScheduledFn fn) { scheduled_ = std::move(fn); return *this; }
  SmartPetDevice& onStatusFill(StatusFillFn fn) { statusFill_ = std::move(fn); return *this; }
  // Two-way-audio signals on kennel/{k}/{type}/{id}/audio:
  // kind = offer|answer|ice|talk|play|stop (see camera-service AudioRelay).
  SmartPetDevice& onAudioSignal(AudioSignalFn fn) { audioSignal_ = std::move(fn); return *this; }
  void setStatusInterval(uint32_t ms) { statusEveryMs_ = ms; }

  // ── outbound ─────────────────────────────────────────────────────────────
  void publishStatus() {
    if (!mqtt_.connected()) return;
    mqtt_.publishStatus("online", [this](JsonObject& s) {
      s["fw"] = SPD_FW_VERSION;
      s["rssi"] = wifi_.rssi();
      s["uptimeS"] = (uint32_t)(millis() / 1000);
      if (statusFill_) statusFill_(s);
    });
  }
  void publishEvent(const String& event, std::function<void(JsonObject&)> fill = nullptr) {
    mqtt_.publishEvent(event, fill);
  }
  void publishMetric(const String& metricLeaf, float v, const String& unit = "") {
    mqtt_.publishMetric(metricLeaf, v, unit);
  }
  void publishTelemetry(std::function<void(JsonObject&)> fill) { mqtt_.publishTelemetry(fill); }
  void publishPresence(const char* tagId, int rssi) { mqtt_.publishPresence(tagId, rssi); }

  // GPS fix stream on kennel/{k}/gps/{id}/location. `fill` adds lat/lng and any
  // extras (accuracy, speed, heading, battery) onto the envelope.
  bool publishLocation(std::function<void(JsonObject&)> fill) {
    JsonDocument doc;
    mqtt_.envelope(doc);                 // deviceId / kennelId / timestamp
    JsonObject o = doc.as<JsonObject>(); // view of the root, not a reset
    if (fill) fill(o);
    return mqtt_.publishJson("location", doc);
  }

  // Report a physical action. Published as an event when online; journalled when not.
  void reportAction(const String& kind, float amount = 0) {
    if (mqtt_.connected()) {
      publishEvent(kind, [amount](JsonObject& d) { d["amount"] = amount; });
    } else {
      journal_.record(time_.epochS(), std::string(kind.c_str()), amount);
      persistJournal();
    }
  }

  // ── accessors ────────────────────────────────────────────────────────────
  Config& config() { return cfg_; }
  ConfigStore& store() { return store_; }
  TimeSync& clock() { return time_; }
  ScheduleCache& schedule() { return sched_; }
  bool isOnline() { return wifi_.isConnected() && mqtt_.connected(); }
  bool inProvisioning() { return wifi_.inPortal(); }

 private:
  void handleConnectivity(bool online, bool timeOk) {
    if (online == wasOnline_) return;
    wasOnline_ = online;
    if (!online) {
      Serial.println("[spd] link down — journalling actions");
      journal_.markOffline(timeOk ? time_.epochS() : 0, wifi_.isConnected() ? "network" : "power");
      persistJournal();
    } else {
      Serial.println("[spd] link up");
      publishStatus();
      reportCrashOnce();
      if (journal_.isOpen()) flushJournal();
    }
  }

  // One `crash` event per boot that followed a panic / watchdog / brown-out.
  void reportCrashOnce() {
    if (!bootCrash_.isCrash || crashReported_ || !mqtt_.connected()) return;
    mqtt_.publishEvent("crash", [this](JsonObject& d) {
      d["reason"] = bootCrash_.reason;
      d["rawReason"] = (int)bootCrash_.raw;
      d["fw"] = SPD_FW_VERSION;
      d["heapFree"] = (uint32_t)ESP.getFreeHeap();
      d["minHeapFree"] = (uint32_t)ESP.getMinFreeHeap();
    });
    crashReported_ = true;
  }

  void flushJournal() {
    auto rep = journal_.closeOnline(time_.epochS());
    persistJournal();  // clears the NVS copy
    mqtt_.publishEvent("offline_recovered", [&rep](JsonObject& d) {
      d["wentOfflineAtS"] = rep.wentOfflineAtS;
      d["cameOnlineAtS"] = rep.cameOnlineAtS;
      d["cause"] = rep.cause.c_str();
      d["dropped"] = rep.dropped;
      JsonArray arr = d["missedActions"].to<JsonArray>();
      for (auto& a : rep.actions) {
        JsonObject o = arr.add<JsonObject>();
        o["atEpochS"] = a.atEpochS;
        o["kind"] = a.kind.c_str();
        o["amount"] = a.amount;
      }
    });
  }

  void persistJournal() {
    if (journal_.isOpen()) store_.putBlobString("journal", journal_.serialize().c_str());
    else store_.putBlobString("journal", "");
  }

  void tickSchedule() {
    if (sched_.size() == 0 || !scheduled_) return;
    int mod = time_.minuteOfDay();
    if (mod < 0) return;
    long day = time_.dayIndex();
    const ScheduleEntry* e = sched_.due(mod, day);
    if (!e) return;
    sched_.markFired(*e, day);
    Serial.printf("[spd] scheduled action: %s amount=%.1f\n", e->id.c_str(), e->amount);
    scheduled_(e->amount);
    reportAction("scheduled", e->amount);
  }

  void onMqtt(const String& topic, JsonObjectConst p) {
    if (topic.endsWith("/audio")) {
      if (audioSignal_) audioSignal_(String((const char*)(p["kind"] | "")), p);
      return;
    }
    if (!topic.endsWith("/command")) return;
    const char* command = p["command"] | "";
    String id = p["id"] | "";
    JsonObjectConst params = p["params"].as<JsonObjectConst>();

    // Echo this command's trace context on every ack it produces (Phase 16).
    mqtt_.setAckTrace(p["traceparent"] | "", p["tracestate"] | "");

    if (handleBuiltin(command, params, id)) return;

    auto it = handlers_.find(command);
    if (it == handlers_.end()) {
      mqtt_.publishAck(id, command, "rejected", "no handler");
      return;
    }
    bool ok = false;
    ok = it->second(params, id);
    mqtt_.publishAck(id, command, ok ? "ok" : "error");
    if (ok) publishStatus();
  }

  bool handleBuiltin(const String& command, JsonObjectConst params, const String& id) {
    if (command == "restart") {
      mqtt_.publishAck(id, command, "ok");
      delay(200); ESP.restart(); return true;
    }
    if (command == "identify") {
      mqtt_.publishAck(id, command, "ok");
      if (identifyFn_) identifyFn_(params["seconds"] | 5);
      return true;
    }
    if (command == "ota") {
      String url = params["url"] | "";
      if (!url.length()) { mqtt_.publishAck(id, command, "rejected", "no url"); return true; }
      mqtt_.publishAck(id, command, "queued");
      bool ok = ota_.applyFromUrl(url, params["sha256"] | "");
      if (!ok) mqtt_.publishAck(id, command, "error", "ota failed");
      return true;
    }
    if (command == "schedule_set") {
      std::vector<ScheduleEntry> list;
      for (JsonObjectConst s : params["schedules"].as<JsonArrayConst>()) {
        ScheduleEntry e;
        e.id = String((const char*)(s["id"] | "")).c_str();
        int mm = parseHhMm(std::string((const char*)(s["time"] | "")));
        if (mm < 0) continue;
        e.hour = mm / 60; e.minute = mm % 60;
        e.amount = s["amount"] | 0.0f;
        e.enabled = s["enabled"] | true;
        list.push_back(e);
      }
      sched_.set(list);
      persistSchedule();
      mqtt_.publishAck(id, command, "ok");
      return true;
    }
    if (command == "set_interval") {
      uint32_t sec = params["seconds"] | 30;
      statusEveryMs_ = sec * 1000UL;
      mqtt_.publishAck(id, command, "ok");
      return true;
    }
    return false;
  }

  void persistSchedule() {
    String s;
    for (auto& e : sched_.entries()) {
      s += String(e.id.c_str()) + "," + String(e.hour) + ":" + String(e.minute) + "," +
           String(e.amount, 1) + "," + (e.enabled ? "1" : "0") + ";";
    }
    store_.putBlobString("sched", s);
  }
  void restoreSchedule() {
    String s = store_.getBlobString("sched");
    if (!s.length()) return;
    std::vector<ScheduleEntry> list;
    int start = 0;
    while (true) {
      int sc = s.indexOf(';', start);
      if (sc < 0) break;
      String rec = s.substring(start, sc);
      start = sc + 1;
      int c1 = rec.indexOf(','), c2 = rec.indexOf(',', c1 + 1), c3 = rec.indexOf(',', c2 + 1);
      if (c1 < 0 || c2 < 0 || c3 < 0) continue;
      ScheduleEntry e;
      e.id = rec.substring(0, c1).c_str();
      String hm = rec.substring(c1 + 1, c2);
      e.hour = hm.substring(0, hm.indexOf(':')).toInt();
      e.minute = hm.substring(hm.indexOf(':') + 1).toInt();
      e.amount = rec.substring(c2 + 1, c3).toFloat();
      e.enabled = rec.substring(c3 + 1) == "1";
      list.push_back(e);
    }
    if (!list.empty()) sched_.set(list);
  }

 public:
  std::function<void(int)> identifyFn_;

 private:
  const char* deviceType_;
  ConfigStore store_;
  Config cfg_;
  WifiManager wifi_{store_, cfg_};
  TimeSync time_;
  MqttTransport mqtt_{cfg_};
  OtaUpdater ota_;
  ScheduleCache sched_;
  OfflineJournal journal_;

  std::map<String, CommandFn> handlers_;
  ScheduledFn scheduled_;
  StatusFillFn statusFill_;
  AudioSignalFn audioSignal_;

  bool wasOnline_ = false;
  bool otaBegun_ = false;
  uint32_t lastStatusMs_ = 0;
  uint32_t statusEveryMs_ = 30000;
  CrashInfo bootCrash_{};
  bool crashReported_ = false;
};

}  // namespace spd
