// spd_config.h — device identity + connection config, persisted in NVS.
// Nothing is hardcoded in firmware: values come from NVS, and if Wi-Fi creds are
// missing the device opens a provisioning AP (see spd_wifi.h).
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "spd_config_codec.h"

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

  // Load the config, preferring the CRC-checked single-entry blob. A missing
  // or corrupt blob falls back to the legacy per-key format (deployed devices),
  // then to an empty config (-> provisioning portal). `lastLoadOk()` /
  // `lastMigrated()` report which path ran.
  Config load() {
    lastLoadOk_ = false;
    lastMigrated_ = false;

    prefs_.begin(ns_, /*readOnly=*/true);
    String blob = prefs_.getString("cfg", "");
    prefs_.end();

    ConfigFields f;
    if (blob.length() && deserializeConfig(std::string(blob.c_str()), f) && configPlausible(f)) {
      lastLoadOk_ = true;
      return fromFields(f);
    }

    Config legacy = loadLegacy();
    if (legacy.hasIdentity() || legacy.hasWifi()) {
      lastLoadOk_ = true;
      lastMigrated_ = true;  // caller should save() to upgrade to the blob format
      return legacy;
    }

    return Config{};  // nothing trustworthy -> factory default
  }

  void save(const Config& c) {
    std::string blob = serializeConfig(toFields(c));
    prefs_.begin(ns_, /*readOnly=*/false);
    prefs_.putString("cfg", blob.c_str());  // one atomic NVS entry
    prefs_.end();
  }

  bool lastLoadOk() const { return lastLoadOk_; }
  bool lastMigrated() const { return lastMigrated_; }

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
  Config loadLegacy() {
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

  static ConfigFields toFields(const Config& c) {
    ConfigFields f;
    f.kennelId = c.kennelId.c_str();  f.deviceType = c.deviceType.c_str();
    f.deviceId = c.deviceId.c_str();  f.wifiSsid = c.wifiSsid.c_str();
    f.wifiPass = c.wifiPass.c_str();  f.mqttHost = c.mqttHost.c_str();
    f.mqttPort = c.mqttPort;          f.mqttUser = c.mqttUser.c_str();
    f.mqttPass = c.mqttPass.c_str();  f.otaPassword = c.otaPassword.c_str();
    f.timezone = c.timezone.c_str();
    return f;
  }
  static Config fromFields(const ConfigFields& f) {
    Config c;
    c.kennelId = f.kennelId.c_str();  c.deviceType = f.deviceType.c_str();
    c.deviceId = f.deviceId.c_str();  c.wifiSsid = f.wifiSsid.c_str();
    c.wifiPass = f.wifiPass.c_str();  c.mqttHost = f.mqttHost.c_str();
    c.mqttPort = f.mqttPort;          c.mqttUser = f.mqttUser.c_str();
    c.mqttPass = f.mqttPass.c_str();  c.otaPassword = f.otaPassword.c_str();
    c.timezone = f.timezone.length() ? String(f.timezone.c_str()) : String("UTC0");
    if (c.mqttUser.isEmpty() && c.deviceId.length()) c.mqttUser = "device:" + c.deviceId;
    return c;
  }

  const char* ns_;
  Preferences prefs_;
  bool lastLoadOk_ = false;
  bool lastMigrated_ = false;
};

}  // namespace spd
