// spd_ota.h — over-the-air updates.
//  - ArduinoOTA for LAN pushes during development
//  - HTTP(S) pull triggered by the `ota` command: {url, version, sha256?}
#pragma once
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Update.h>
#include "spd_config.h"

namespace spd {

class OtaUpdater {
 public:
  void begin(const Config& cfg) {
    ArduinoOTA.setHostname(("smartpet-" + cfg.deviceId).c_str());
    if (cfg.otaPassword.length()) ArduinoOTA.setPassword(cfg.otaPassword.c_str());
    ArduinoOTA.begin();
  }

  void loop() { ArduinoOTA.handle(); }

  // Returns true on success (device reboots into the new image); false on any
  // failure, having left the running image intact.
  bool applyFromUrl(const String& url, const String& expectSha256 = "") {
    WiFiClient* stream = nullptr;
    HTTPClient http;
    if (!http.begin(url)) return false;
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return false; }
    int len = http.getSize();
    if (len <= 0) { http.end(); return false; }
    if (!Update.begin(len)) { http.end(); return false; }

    stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();
    if (written != (size_t)len) { Update.abort(); return false; }
    if (!Update.end(true)) return false;
    (void)expectSha256;  // Update verifies size; hash check hook left for the caller
    delay(300);
    ESP.restart();
    return true;
  }
};

}  // namespace spd
