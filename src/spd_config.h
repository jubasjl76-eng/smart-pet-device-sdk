// spd_config.h — device identity + connection config, persisted in NVS.
// Nothing is hardcoded in firmware: values come from NVS, and if Wi-Fi creds are
// missing the device opens a provisioning AP (see spd_wifi.h).
#pragma once
#include <Arduino.h>
#include <Preferences.h>

namespace spd {

struct Config {
  // identity
  String kennelId;        // tenant slug, e.g. "home"
  String deviceType;      // "feeder" | "water" | "door" | "scale" | "sensor" | "gps" | "hub"
  String deviceId;        // stable per unit, e.g. "feeder-01"

  // wifi
  String wifiSsid;
  String wifiPass;

  // mqtt
  String mqttHost;
  uint16_t mqttPort = 1883;
  String mqttUser;        // conventionally "device:<deviceId>"
  String mqttPass;        // minted on claim, stored once

  // misc
  String otaPassword;
  String timezone;        // POSIX TZ string, e.g. "WET0WEST,M3.5.0/1,M10.5.0"

  bool hasWifi() const { return wifiSsid.length() > 0; }
  bool hasMqtt() const { return mqttHost.length() > 0; }
  bool hasIdentity() const { return kennelId.length() && deviceType.length() && deviceId.length(); }
};

class ConfigStore {
 public:
  // ns defaults to "spd"; call before begin() to override.
  explicit ConfigStore(const char* ns = "spd") : ns_(ns) {}

  Config load() {
    Config c;
    prefs_.begin(ns_, /*readOnly=*/true);
    c.kennelId    = prefs_.getString("kennelId", "");
    c.deviceType  = prefs_.getString("deviceType", "");
    c.deviceId    = prefs_.getString("deviceId", "");
    c.wifiSsid    = prefs_.getString("wifiSsid", "");
    c.wifiPass    = prefs_.getString("wifiPass", "");
    c.mqttHost    = prefs_.getString("mqttHost", "");
    c.mqttPort    = prefs_.getUShort("mqttPort", 1883);
    c.mqttUser    = prefs_.getString("mqttUser", "");
    c.mqttPass    = prefs_.getString("mqttPass", "");
    c.otaPassword = prefs_.getString("otaPass", "");
    c.timezone    = prefs_.getString("tz", "UTC0");
    prefs_.end();
    if (c.mqttUser.isEmpty() && c.deviceId.length()) c.mqttUser = "device:" + c.deviceId;
    return c;
  }

  void save(const Config& c) {
    prefs_.begin(ns_, /*readOnly=*/false);
    prefs_.putString("kennelId", c.kennelId);
    prefs_.putString("deviceType", c.deviceType);
    prefs_.putString("deviceId", c.deviceId);
    prefs_.putString("wifiSsid", c.wifiSsid);
    prefs_.putString("wifiPass", c.wifiPass);
    prefs_.putString("mqttHost", c.mqttHost);
    prefs_.putUShort("mqttPort", c.mqttPort);
    prefs_.putString("mqttUser", c.mqttUser);
    prefs_.putString("mqttPass", c.mqttPass);
    prefs_.putString("otaPass", c.otaPassword);
    prefs_.putString("tz", c.timezone);
    prefs_.end();
  }

  // Small NVS scratch area for module state (schedule cache, offline journal).
  void putBlobString(const char* key, const String& value) {
    prefs_.begin(ns_, false);
    prefs_.putString(key, value);
    prefs_.end();
  }
  String getBlobString(const char* key) {
    prefs_.begin(ns_, true);
    String v = prefs_.getString(key, "");
    prefs_.end();
    return v;
  }
  void erase() {
    prefs_.begin(ns_, false);
    prefs_.clear();
    prefs_.end();
  }

 private:
  const char* ns_;
  Preferences prefs_;
};

}  // namespace spd
