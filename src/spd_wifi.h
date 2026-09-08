// spd_wifi.h — Wi-Fi connection with a provisioning fallback.
//
// If NVS has no SSID (or STA fails for `provisionAfterMs`), the device starts a
// SoftAP "smartpet-<deviceId>" with a captive portal that accepts SSID / pass /
// kennelId / deviceId / MQTT host+creds, writes them to NVS, and reboots.
// Nothing is compiled into the firmware image.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "spd_config.h"

namespace spd {

class WifiManager {
 public:
  WifiManager(ConfigStore& store, Config& cfg) : store_(store), cfg_(cfg) {}

  void begin(uint32_t provisionAfterMs = 20000) {
    provisionAfterMs_ = provisionAfterMs;
    if (!cfg_.hasWifi()) { startPortal(); return; }
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(cfg_.wifiSsid.c_str(), cfg_.wifiPass.c_str());
    connectStartedMs_ = millis();
  }

  // Call from loop(). Returns true while STA is connected.
  bool poll() {
    if (portalActive_) { dns_.processNextRequest(); server_.handleClient(); return false; }
    if (WiFi.status() == WL_CONNECTED) { everConnected_ = true; return true; }
    if (!everConnected_ && millis() - connectStartedMs_ > provisionAfterMs_) startPortal();
    return false;
  }

  bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
  bool inPortal() const { return portalActive_; }
  int rssi() const { return WiFi.RSSI(); }
  String ip() const { return WiFi.localIP().toString(); }

 private:
  void startPortal() {
    if (portalActive_) return;
    portalActive_ = true;
    String ap = "smartpet-" + (cfg_.deviceId.length() ? cfg_.deviceId : String((uint32_t)ESP.getEfuseMac(), HEX));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap.c_str());
    dns_.start(53, "*", WiFi.softAPIP());
    server_.on("/", [this]() { server_.send(200, "text/html", form()); });
    server_.on("/save", HTTP_POST, [this]() { handleSave(); });
    server_.onNotFound([this]() { server_.send(200, "text/html", form()); });
    server_.begin();
    Serial.printf("[wifi] provisioning AP up: %s  (http://%s)\n", ap.c_str(), WiFi.softAPIP().toString().c_str());
  }

  String field(const char* name, const char* label, const String& val, const char* type = "text") {
    return "<label>" + String(label) + "<br><input name='" + name + "' type='" + type +
           "' value='" + val + "'></label><br><br>";
  }

  String form() {
    String h = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
               "<style>body{font-family:system-ui;margin:24px;max-width:420px}input{width:100%;padding:8px}</style>"
               "<h2>Smart Pet setup</h2><form method=POST action=/save>";
    h += field("ssid", "Wi-Fi SSID", cfg_.wifiSsid);
    h += field("pass", "Wi-Fi password", "", "password");
    h += field("kennelId", "Kennel / household id", cfg_.kennelId);
    h += field("deviceId", "Device id", cfg_.deviceId);
    h += field("mqttHost", "MQTT host", cfg_.mqttHost);
    h += field("mqttPort", "MQTT port", String(cfg_.mqttPort));
    h += field("mqttUser", "MQTT user", cfg_.mqttUser);
    h += field("mqttPass", "MQTT password", "", "password");
    h += "<button type=submit style='padding:10px 16px'>Save &amp; reboot</button></form>";
    return h;
  }

  void handleSave() {
    cfg_.wifiSsid = server_.arg("ssid");
    cfg_.wifiPass = server_.arg("pass");
    cfg_.kennelId = server_.arg("kennelId");
    cfg_.deviceId = server_.arg("deviceId");
    cfg_.mqttHost = server_.arg("mqttHost");
    if (server_.arg("mqttPort").length()) cfg_.mqttPort = (uint16_t)server_.arg("mqttPort").toInt();
    cfg_.mqttUser = server_.arg("mqttUser");
    if (server_.arg("mqttPass").length()) cfg_.mqttPass = server_.arg("mqttPass");
    if (cfg_.mqttUser.isEmpty() && cfg_.deviceId.length()) cfg_.mqttUser = "device:" + cfg_.deviceId;
    store_.save(cfg_);
    server_.send(200, "text/html", "<h3>Saved. Rebooting…</h3>");
    delay(800);
    ESP.restart();
  }

  ConfigStore& store_;
  Config& cfg_;
  WebServer server_{80};
  DNSServer dns_;
  bool portalActive_ = false;
  bool everConnected_ = false;
  uint32_t connectStartedMs_ = 0;
  uint32_t provisionAfterMs_ = 20000;
};

}  // namespace spd
