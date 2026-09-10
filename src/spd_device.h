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
#include <cstdlib>
#include <ctime>
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
#include "spd_brownout.h"
#include "spd_health.h"

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
    brownout_ = brownoutBootCheck();
    if (brownout_.thisBoot)
      Serial.printf("[spd] brown-out reset (#%u since cold boot)\n", brownout_.count);

    cfg_ = store_.load();
    if (!store_.lastLoadOk())
      Serial.println("[spd] config unreadable/corrupt — starting from factory defaults");
    else if (store_.lastMigrated()) {
      Serial.println("[spd] upgrading config to the CRC-checked format");
      store_.save(cfg_);
    }
    if (cfg_.deviceType.isEmpty()) cfg_.deviceType = deviceType_;

    wifi_.begin();
    time_.begin(cfg_.timezone);
    {  // seed the RTC from the last-known-good epoch so timestamps aren't 1970
      String lkg = store_.getBlobString("time_lkg");
      if (lkg.length()) time_.restoreLastKnown((time_t)strtoull(lkg.c_str(), nullptr, 10));
    }
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
    health_.poll();

    if (wifiUp && !otaBegun_) { ota_.begin(cfg_); otaBegun_ = true; }
    if (otaBegun_) ota_.loop();

    if (wifiUp) mqtt_.loop();

    bool online = wifiUp && mqtt_.connected();
    handleConnectivity(online, timeOk);

    // Persist last-known-good time so a power cut doesn't lose the clock, and
    // report the restored-clock offset once after the first sync (A12 #16).
    if (timeOk && millis() - lastTimeSaveMs_ > kTimeSaveEveryMs) {
      lastTimeSaveMs_ = millis();
      store_.putBlobString("time_lkg", String(time_.epochS()));
    }
    if (online && time_.haveClockOffset() && !clockOffsetReported_) {
      clockOffsetReported_ = true;
      publishMetric("clockOffsetS", (float)time_.clockOffsetS(), "seconds");
    }

    // Memory pressure: one event on each verdict change (ok <-> low <-> critical).
    if (online) {
      HeapVerdict v = health_.verdict();
      if (v != lastHeapVerdict_) {
        lastHeapVerdict_ = v;
        publishEvent("health", [this, v](JsonObject& d) {
          d["heap"] = heapVerdictStr(v);
          d["heapFree"] = health_.freeHeap();
          d["heapLargest"] = health_.largestBlock();
          d["stackMin"] = health_.minStack();
        });
      }
    }

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
      s["heapFree"] = health_.freeHeap();
      s["heapMin"] = health_.minFreeHeap();
      s["stackMin"] = health_.minStack();
      if (brownout_.count) s["brownouts"] = brownout_.count;
      if (!store_.lastLoadOk()) s["configDefaulted"] = true;
      if (safeMode_) s["safeMode"] = true;
      if (health_.degraded()) s["degraded"] = heapVerdictStr(health_.verdict());
      // Under critical memory pressure, skip the app's optional status fill —
      // it may allocate. Keep the device reporting rather than risk an OOM.
      if (statusFill_ && !health_.critical()) statusFill_(s);
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
      journal_.record(time_.timeUsable() ? time_.epochS() : 0, std::string(kind.c_str()), amount);
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
  // True while the kennel is halted via the fleet kill switch — a sketch can
  // check this to blink an LED, freeze a display, etc. (actuation is already
  // suppressed by the SDK).
  bool safeModeActive() const { return safeMode_; }
  // Memory head-room. A sketch can check `health().degraded()` before an
  // allocation-heavy operation; the SDK already sheds its own optional work.
  HealthMonitor& health() { return health_; }

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
    if (safeMode_) return;              // fleet kill switch — no scheduled actuation
    if (!time_.timeTrusted()) return;   // never fire a schedule on an unsynced clock
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
    if (topic.endsWith("/_control")) { applyControl(p); return; }
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

    if (handleBuiltin(command, params, id)) return;  // restart/identify/ota/schedule_set/set_interval — always allowed

    auto it = handlers_.find(command);
    if (it == handlers_.end()) {
      mqtt_.publishAck(id, command, "rejected", "no handler");
      return;
    }
    if (safeMode_) {  // fleet kill switch — app actuator handlers are suspended
      mqtt_.publishAck(id, command, "rejected", "safe-mode");
      return;
    }
    bool ok = false;
    ok = it->second(params, id);
    mqtt_.publishAck(id, command, ok ? "ok" : "error");
    if (ok) publishStatus();
  }

  // Retained kennel/{k}/_control : {"safeMode":bool,"reason":str}. An empty /
  // cleared retained message (safeMode absent) leaves safe mode.
  void applyControl(JsonObjectConst p) {
    bool now = p["safeMode"] | false;
    if (now == safeMode_) return;
    safeMode_ = now;
    if (now) Serial.printf("[spd] FLEET SAFE MODE — %s\n", (const char*)(p["reason"] | "(no reason)"));
    else Serial.println("[spd] fleet resumed");
    String reason = p["reason"] | "";
    publishEvent(now ? "safe_mode" : "safe_mode_cleared", [reason](JsonObject& d) {
      if (reason.length()) d["reason"] = reason;
    });
    publishStatus();
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
  BrownoutInfo brownout_{};
  bool safeMode_ = false;
  HealthMonitor health_{};
  HeapVerdict lastHeapVerdict_ = HeapVerdict::Ok;

  static constexpr uint32_t kTimeSaveEveryMs = 15UL * 60 * 1000;  // persist LKG every 15 min
  uint32_t lastTimeSaveMs_ = 0;
  bool clockOffsetReported_ = false;
};

}  // namespace spd
