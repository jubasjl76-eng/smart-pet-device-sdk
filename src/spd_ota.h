// spd_ota.h — over-the-air updates.
//  - ArduinoOTA for LAN pushes during development
//  - HTTP(S) pull triggered by the `ota` command: {url, version, sha256?}
//    The image is SHA-256'd while it streams and rejected on a mismatch — the
//    artifact download is plain HTTP from S3/CDN, so this is the integrity
//    boundary until Secure Boot (Phase 19 OTA provenance).
#pragma once
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include "spd_config.h"
#include "spd_ota_util.h"

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
  // failure, having left the running image intact. `lastError()` says why.
  bool applyFromUrl(const String& url, const String& expectSha256 = "") {
    lastError_ = "";
    HTTPClient http;
    if (!http.begin(url)) return fail("begin");
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return fail("http-" + String(code)); }
    int len = http.getSize();
    if (len <= 0) { http.end(); return fail("no-length"); }
    if (!Update.begin(len)) { http.end(); return fail("update-begin"); }

    WiFiClient* stream = http.getStreamPtr();
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, /*is224=*/0);

    uint8_t buf[1024];
    size_t total = 0;
    uint32_t idleSince = millis();
    while (http.connected() && total < (size_t)len) {
      size_t avail = stream->available();
      if (!avail) {
        if (millis() - idleSince > kStallMs) break;
        delay(1);
        continue;
      }
      idleSince = millis();
      size_t want = avail < sizeof(buf) ? avail : sizeof(buf);
      size_t n = stream->readBytes(buf, want);
      if (Update.write(buf, n) != n) {
        mbedtls_sha256_free(&sha);
        Update.abort();
        http.end();
        return fail("write");
      }
      mbedtls_sha256_update(&sha, buf, n);
      total += n;
    }
    http.end();

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (total != (size_t)len) { Update.abort(); return fail("short-read"); }

    if (expectSha256.length() && !sha256HexEqual(digest, std::string(expectSha256.c_str()))) {
      Serial.println("[ota] SHA-256 mismatch — refusing image");
      Update.abort();
      return fail("sha-mismatch");
    }
    if (!Update.end(true)) return fail("update-end");

    Serial.printf("[ota] verified %s — restarting\n", toHexLower(digest, 32).c_str());
    delay(300);
    ESP.restart();
    return true;
  }

  const String& lastError() const { return lastError_; }

 private:
  static constexpr uint32_t kStallMs = 15000;
  bool fail(const String& why) {
    lastError_ = why;
    return false;
  }
  String lastError_;
};

}  // namespace spd
