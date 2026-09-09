// TwoWayAudioModule.h — ESP32 I2S mic (INMP441) + I2S amp (MAX98357A) for
// "talk to your dog". Media path is deliberately simple: 16 kHz mono, mu-law on
// the wire over UDP (uplink) and a raw-PCM HTTP clip player (downlink). Full
// duplex WebRTC lives in the camera-service / gateway, not here.
//
//   spd::TwoWayAudioModule audio(/*micBclk*/32,/*micWs*/25,/*micDin*/33,
//                                /*spkBclk*/27,/*spkWs*/26,/*spkDout*/14);
//   audio.begin();
//   // on a `talk:start` signal:  audio.startTalk("10.0.0.5", 6000);
//   // in loop():                 audio.pumpTalk();
//   // on a `play:url` signal:    audio.play("http://.../come-here.pcm");
//   // on `stop` / `talk:end`:    audio.stopTalk();
#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include "../spd_ulaw.h"

namespace spd {

class TwoWayAudioModule {
 public:
  TwoWayAudioModule(int micBclk, int micWs, int micDin,
                    int spkBclk, int spkWs, int spkDout,
                    uint32_t sampleRate = 16000,
                    i2s_port_t micPort = I2S_NUM_0, i2s_port_t spkPort = I2S_NUM_1)
      : micBclk_(micBclk), micWs_(micWs), micDin_(micDin),
        spkBclk_(spkBclk), spkWs_(spkWs), spkDout_(spkDout),
        rate_(sampleRate), micPort_(micPort), spkPort_(spkPort) {}

  void begin() {
    installMic();
    installSpk();
  }

  // ── uplink: mic → mu-law → UDP ──────────────────────────────────────────
  void startTalk(const char* host, uint16_t port) {
    talkHost_ = host;
    talkPort_ = port;
    udp_.begin(0);
    talking_ = true;
  }
  void stopTalk() {
    talking_ = false;
    udp_.stop();
  }
  bool talking() const { return talking_; }

  // Call every loop() while talking. Reads one frame, sends it if non-silent.
  void pumpTalk() {
    if (!talking_) return;
    int16_t pcm[kFrame];
    size_t got = 0;
    if (i2s_read(micPort_, pcm, sizeof(pcm), &got, 0) != ESP_OK || got == 0) return;
    size_t n = got / sizeof(int16_t);
    if (muted_) return;
    uint8_t ulaw[kFrame];
    ulawEncodeBlock(pcm, ulaw, n);
    udp_.beginPacket(talkHost_.c_str(), talkPort_);
    udp_.write(ulaw, n);
    udp_.endPacket();
  }

  // ── downlink: a received mu-law frame → speaker ─────────────────────────
  void feedSpeaker(const uint8_t* ulaw, size_t n) {
    int16_t pcm[kFrame];
    if (n > kFrame) n = kFrame;
    ulawDecodeBlock(ulaw, pcm, n);
    applyGain(pcm, n);
    size_t wrote = 0;
    i2s_write(spkPort_, pcm, n * sizeof(int16_t), &wrote, portMAX_DELAY);
  }

  // ── downlink: play a raw 16-bit mono PCM clip from a URL ────────────────
  bool play(const char* url) {
    HTTPClient http;
    if (!http.begin(url)) return false;
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return false; }
    WiFiClient* s = http.getStreamPtr();
    int16_t buf[kFrame];
    while (http.connected()) {
      int r = s->readBytes((uint8_t*)buf, sizeof(buf));
      if (r <= 0) break;
      size_t n = r / sizeof(int16_t);
      applyGain(buf, n);
      size_t wrote = 0;
      i2s_write(spkPort_, buf, n * sizeof(int16_t), &wrote, portMAX_DELAY);
    }
    http.end();
    return true;
  }

  void setMuted(bool m) { muted_ = m; }
  void setGain(float g) { gain_ = constrain(g, 0.0f, 4.0f); }

 private:
  static constexpr size_t kFrame = 160;  // 10 ms at 16 kHz

  void applyGain(int16_t* pcm, size_t n) {
    if (gain_ == 1.0f) return;
    for (size_t i = 0; i < n; ++i) {
      int32_t v = (int32_t)(pcm[i] * gain_);
      pcm[i] = (int16_t)constrain(v, -32768, 32767);
    }
  }

  void installMic() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate = rate_;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = kFrame;
    i2s_driver_install(micPort_, &cfg, 0, nullptr);
    i2s_pin_config_t pins = {};
    pins.bck_io_num = micBclk_;
    pins.ws_io_num = micWs_;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = micDin_;
    i2s_set_pin(micPort_, &pins);
  }

  void installSpk() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = rate_;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = kFrame;
    i2s_driver_install(spkPort_, &cfg, 0, nullptr);
    i2s_pin_config_t pins = {};
    pins.bck_io_num = spkBclk_;
    pins.ws_io_num = spkWs_;
    pins.data_out_num = spkDout_;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    i2s_set_pin(spkPort_, &pins);
  }

  int micBclk_, micWs_, micDin_, spkBclk_, spkWs_, spkDout_;
  uint32_t rate_;
  i2s_port_t micPort_, spkPort_;
  WiFiUDP udp_;
  String talkHost_;
  uint16_t talkPort_ = 0;
  bool talking_ = false;
  bool muted_ = false;
  float gain_ = 1.0f;
};

}  // namespace spd
