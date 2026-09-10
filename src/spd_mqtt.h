// spd_mqtt.h — MQTT transport on the canonical scheme.
//
//  - LWT on kennel/{k}/{type}/{id}/status : {timestamp:0,status:"offline"} retained
//  - subscribes kennel/{k}/{type}/{id}/command  (QoS 2)
//  - publishStatus (retained QoS 1), publishEvent/Ack/Telemetry/Metric/Presence/Location (QoS 1)
//  - exponential-backoff reconnect (spd::Backoff)
#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "spd_topics.h"
#include "spd_backoff.h"
#include "spd_config.h"

namespace spd {

using MqttMessageCb = std::function<void(const String& topic, JsonObjectConst payload)>;

class MqttTransport {
 public:
  MqttTransport(Config& cfg) : cfg_(cfg), mqtt_(net_) {}

  void begin(MqttMessageCb onMessage) {
    onMessage_ = std::move(onMessage);
    mqtt_.setServer(cfg_.mqttHost.c_str(), cfg_.mqttPort);
    mqtt_.setBufferSize(1024);
    mqtt_.setKeepAlive(30);
    mqtt_.setCallback([this](char* t, uint8_t* p, unsigned int n) { onRaw(t, p, n); });
  }

  // Call frequently from loop(). Handles (re)connect + PubSubClient pump.
  void loop() {
    if (mqtt_.connected()) { mqtt_.loop(); return; }
    uint32_t now = millis();
    if (now < nextAttemptMs_) return;
    connectOnce();
    double r = (double)esp_random() / (double)UINT32_MAX;
    nextAttemptMs_ = now + backoff_.next(r);
  }

  bool connected() { return mqtt_.connected(); }

  String base(const String& leaf) const {
    return buildTopic(cfg_.kennelId.c_str(), cfg_.deviceType.c_str(), cfg_.deviceId.c_str(), leaf.c_str()).c_str();
  }

  bool publishJson(const String& leaf, const JsonDocument& doc) {
    if (!mqtt_.connected()) return false;
    String out;
    serializeJson(doc, out);
    Delivery d = deliveryFor(std::string(leaf.c_str()));
    return mqtt_.publish(base(leaf).c_str(), (const uint8_t*)out.c_str(), out.length(),
                         d.retain);  // QoS from PubSubClient is 0/1; retain honoured
  }

  // Convenience wrappers add the envelope.
  bool publishStatus(const String& status, std::function<void(JsonObject&)> fill = nullptr) {
    JsonDocument doc; envelope(doc);
    doc["status"] = status;
    if (fill) { JsonObject o = doc.to<JsonObject>(); fill(o); }
    return publishJson("status", doc);
  }
  bool publishEvent(const String& event, std::function<void(JsonObject&)> fill = nullptr) {
    JsonDocument doc; envelope(doc);
    doc["event"] = event;
    if (fill) { JsonObject data = doc["data"].to<JsonObject>(); fill(data); }
    return publishJson("event", doc);
  }
  // Echo the current command's W3C Trace Context on its ack(s) so the
  // command -> ack round trip links (Phase 16). Set from onMqtt before dispatch.
  void setAckTrace(const String& traceparent, const String& tracestate) {
    ackTraceparent_ = traceparent;
    ackTracestate_ = tracestate;
  }

  bool publishAck(const String& ackId, const String& command, const String& result, const String& detail = "") {
    JsonDocument doc; envelope(doc);
    doc["ackId"] = ackId; doc["command"] = command; doc["result"] = result;
    if (detail.length()) doc["detail"] = detail;
    if (ackTraceparent_.length()) doc["traceparent"] = ackTraceparent_;
    if (ackTracestate_.length()) doc["tracestate"] = ackTracestate_;
    return publishJson("ack", doc);
  }
  bool publishMetric(const String& metricLeaf, float value, const String& unit = "") {
    JsonDocument doc; envelope(doc);
    doc["value"] = value;
    if (unit.length()) doc["unit"] = unit;
    return publishJson(metricLeaf, doc);
  }
  bool publishTelemetry(std::function<void(JsonObject&)> fill) {
    JsonDocument doc; envelope(doc);
    JsonObject m = doc["metrics"].to<JsonObject>();
    fill(m);
    return publishJson("telemetry", doc);
  }
  bool publishPresence(const char* tagId, int rssi) {
    JsonDocument doc; envelope(doc);
    if (tagId) doc["tagId"] = tagId; else doc["tagId"] = nullptr;
    doc["rssi"] = rssi;
    return publishJson("presence", doc);
  }

  void envelope(JsonDocument& doc) {
    doc["deviceId"] = cfg_.deviceId;
    doc["kennelId"] = cfg_.kennelId;
    doc["timestamp"] = (uint64_t)time(nullptr) * 1000ULL;
  }

 private:
  void connectOnce() {
    String willTopic = base("status");
    JsonDocument will;
    will["deviceId"] = cfg_.deviceId; will["kennelId"] = cfg_.kennelId;
    will["timestamp"] = 0; will["status"] = "offline";
    String willPayload; serializeJson(will, willPayload);

    bool ok = mqtt_.connect(cfg_.deviceId.c_str(),
                            cfg_.mqttUser.length() ? cfg_.mqttUser.c_str() : nullptr,
                            cfg_.mqttPass.length() ? cfg_.mqttPass.c_str() : nullptr,
                            willTopic.c_str(), 1, true, willPayload.c_str(), /*cleanSession=*/true);
    if (ok) {
      backoff_.reset();
      String cmd = base("command");
      mqtt_.subscribe(cmd.c_str(), 1);   // PubSubClient max granted QoS is 1
      mqtt_.subscribe(base("audio").c_str(), 1);  // two-way-audio signalling
      mqtt_.subscribe(controlTopic(cfg_.kennelId.c_str()).c_str(), 1);  // fleet kill switch (retained)
      Serial.printf("[mqtt] connected; sub %s\n", cmd.c_str());
    } else {
      Serial.printf("[mqtt] connect failed rc=%d\n", mqtt_.state());
    }
  }

  void onRaw(char* topic, uint8_t* payload, unsigned int len) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, len)) return;
    if (onMessage_) onMessage_(String(topic), doc.as<JsonObjectConst>());
  }

  Config& cfg_;
  WiFiClient net_;
  PubSubClient mqtt_;
  MqttMessageCb onMessage_;
  Backoff backoff_{1000, 60000};
  uint32_t nextAttemptMs_ = 0;
  String ackTraceparent_;
  String ackTracestate_;
};

}  // namespace spd
