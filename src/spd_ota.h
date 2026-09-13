// spd_ota.h — over-the-air updates.
//  - ArduinoOTA for LAN pushes during development
//  - HTTP(S) pull triggered by the `ota` command: {url, version, sha256?}
//    The image is SHA-256'd while it streams and rejected on a mismatch — the
//    artifact download is plain HTTP from S3/CDN, so this is the integrity
//    boundary until Secure Boot (Phase 19 OTA provenance).
//  - Resumable via HTTP Range on a stall/drop (Phase 21, A11 — OTA CDN
//    hardening): a dropped connection at 90% resumes from there instead of
//    re-downloading the whole image. Resume state lives only in RAM for this
//    power cycle — a reboot mid-OTA still starts the next attempt from 0,
//    which the existing kill-switch/retry path already tolerates.
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
  //
  // Resumable (Phase 21, A11): a stall or drop mid-download retries with an
  // HTTP Range request picking up from the last byte actually written to the
  // flash partition, instead of discarding the whole image and starting
  // over — the running SHA-256 keeps accumulating across attempts too, so
  // the final digest is still over the *whole* image regardless of how many
  // resumes it took.
  bool applyFromUrl(const String& url, const String& expectSha256 = "") {
    lastError_ = "";
    int fullLen = -1;
    size_t total = 0;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, /*is224=*/0);

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
      if (attempt > 0) delay(otaRetryDelayMs(attempt - 1));

      HTTPClient http;
      if (!http.begin(url)) continue;
      bool resuming = total > 0;
      if (resuming) http.addHeader("Range", otaRangeHeader(total).c_str());

      int code = http.GET();
      if (resuming && code == HTTP_CODE_OK) {
        // Server ignored the Range header and would resend from byte 0 —
        // our partial write + running hash can't be trusted to continue.
        http.end();
        mbedtls_sha256_free(&sha);
        Update.abort();
        return fail("range-not-honored");
      }
      int want = resuming ? 206 : HTTP_CODE_OK;
      if (code != want) { http.end(); continue; }

      int chunkLen = http.getSize();
      if (chunkLen <= 0) { http.end(); continue; }
      if (fullLen < 0) {
        fullLen = (int)total + chunkLen;
        if (!Update.begin(fullLen)) {
          http.end();
          mbedtls_sha256_free(&sha);
          return fail("update-begin");
        }
      }

      WiFiClient* stream = http.getStreamPtr();
      uint8_t buf[1024];
      size_t chunkRead = 0;
      uint32_t idleSince = millis();
      while (http.connected() && chunkRead < (size_t)chunkLen) {
        size_t avail = stream->available();
        if (!avail) {
          if (millis() - idleSince > kStallMs) break;
          delay(1);
          continue;
        }
        idleSince = millis();
        size_t wantN = avail < sizeof(buf) ? avail : sizeof(buf);
        size_t n = stream->readBytes(buf, wantN);
        if (Update.write(buf, n) != n) {
          http.end();
          mbedtls_sha256_free(&sha);
          Update.abort();
          return fail("write");
        }
        mbedtls_sha256_update(&sha, buf, n);
        total += n;
        chunkRead += n;
      }
      http.end();

      if (total >= (size_t)fullLen) break;
      // else: stalled/short this attempt — loop again, resuming from `total`
    }

    if (fullLen < 0 || total != (size_t)fullLen) {
      mbedtls_sha256_free(&sha);
      Update.abort();
      return fail("short-read");
    }

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);

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
  static constexpr int kMaxAttempts = 5;  // initial GET + up to 4 resumes
  bool fail(const String& why) {
    lastError_ = why;
    return false;
  }
  String lastError_;
};

}  // namespace spd
