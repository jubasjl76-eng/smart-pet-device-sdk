// ScaleModule.h — HX711 load-cell platform / bowl scale.
//
// Two uses:
//   • bowl under a feeder/water unit → grams actually eaten vs dispensed
//   • doorway platform → animal body weight (needs a stable-reading gate)
//
// Depends on the HX711 library (bogde/HX711). Calibrate `scaleFactor` once with a
// known mass: raw_per_gram = (reading_with_mass - tare) / mass_g.
#pragma once
#include <Arduino.h>
#include <HX711.h>

namespace spd {

class ScaleModule {
 public:
  ScaleModule(int doutPin, int sckPin) : dout_(doutPin), sck_(sckPin) {}

  void begin(float scaleFactor = 1.0f) {
    hx_.begin(dout_, sck_);
    hx_.set_scale(scaleFactor);
    delay(50);
    hx_.tare(10);
  }

  void tare(uint8_t n = 10) { hx_.tare(n); }
  void setScaleFactor(float f) { hx_.set_scale(f); }

  // Instantaneous grams (median of `n` reads).
  float grams(uint8_t n = 5) {
    if (!hx_.is_ready()) return lastGrams_;
    lastGrams_ = hx_.get_units(n);
    return lastGrams_;
  }

  // Returns a body-weight reading only once it has been stable (± `tolG`) for
  // `stableMs`; otherwise NAN. Feed it from loop() while an animal stands on it.
  float stableWeight(float tolG = 40.0f, uint32_t stableMs = 1500) {
    float g = grams(3);
    uint32_t now = millis();
    if (isnan(refG_) || fabsf(g - refG_) > tolG) {
      refG_ = g;
      stableSinceMs_ = now;
      return NAN;
    }
    if (now - stableSinceMs_ >= stableMs && g > 300) {  // ignore an empty platform
      return g;
    }
    return NAN;
  }

  // grams eaten between two marks (call markBefore() when a meal is served).
  void markBefore() { beforeG_ = grams(8); }
  float consumedSinceMark() { return max(0.0f, beforeG_ - grams(8)); }

  float lastGrams() const { return lastGrams_; }

 private:
  HX711 hx_;
  int dout_, sck_;
  float lastGrams_ = 0;
  float refG_ = NAN;
  uint32_t stableSinceMs_ = 0;
  float beforeG_ = 0;
};

}  // namespace spd
