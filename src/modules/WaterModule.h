// WaterModule.h — pump + level (HC-SR04) + TDS + NTC temperature.
// Defaults match smart-water-dispenser: pump=GPIO4, trig=5, echo=18, tds=34, ntc=35.
#pragma once
#include <Arduino.h>

namespace spd {

class WaterModule {
 public:
  WaterModule(int pumpPin, int trigPin, int echoPin, int tdsPin = 34, int ntcPin = 35)
      : pumpPin_(pumpPin), trigPin_(trigPin), echoPin_(echoPin), tdsPin_(tdsPin), ntcPin_(ntcPin) {}

  void begin() {
    pinMode(pumpPin_, OUTPUT);
    digitalWrite(pumpPin_, LOW);
    pinMode(trigPin_, OUTPUT);
    pinMode(echoPin_, INPUT);
  }

  // Run the pump for `seconds` (bounded). Blocking; callers with tight loops
  // should split this into start()/stop().
  bool dispense(float seconds) {
    if (seconds <= 0) seconds = 5;
    seconds = constrain(seconds, 0.5f, 30.0f);
    digitalWrite(pumpPin_, HIGH);
    delay((uint32_t)(seconds * 1000));
    digitalWrite(pumpPin_, LOW);
    lastRunS_ = seconds;
    return true;
  }
  void start() { digitalWrite(pumpPin_, HIGH); }
  void stop()  { digitalWrite(pumpPin_, LOW); }

  float levelPct(float fullCm = 2, float emptyCm = 15) {
    digitalWrite(trigPin_, LOW);  delayMicroseconds(2);
    digitalWrite(trigPin_, HIGH); delayMicroseconds(10);
    digitalWrite(trigPin_, LOW);
    long dur = pulseIn(echoPin_, HIGH, 30000);
    if (dur == 0) return lastLevel_;
    float cm = (dur * 0.0343f) / 2.0f;
    lastLevel_ = constrain(100.0f * (emptyCm - cm) / (emptyCm - fullCm), 0.0f, 100.0f);
    return lastLevel_;
  }

  // Uncalibrated TDS (ppm). Provide vRef/temp for a better estimate.
  float tdsPpm(float vRef = 3.3f, float tempC = 25.0f) {
    int raw = analogRead(tdsPin_);
    float v = raw * vRef / 4095.0f;
    float comp = v / (1.0f + 0.02f * (tempC - 25.0f));
    return (133.42f * comp * comp * comp - 255.86f * comp * comp + 857.39f * comp) * 0.5f;
  }

  // 10k NTC divider → °C (beta model).
  float temperatureC(float beta = 3950, float r0 = 10000, float t0 = 298.15f, float rFixed = 10000) {
    int raw = analogRead(ntcPin_);
    if (raw <= 0 || raw >= 4095) return NAN;
    float r = rFixed * ((4095.0f / raw) - 1.0f);
    float tK = 1.0f / (1.0f / t0 + (1.0f / beta) * logf(r / r0));
    return tK - 273.15f;
  }

  float lastRunS() const { return lastRunS_; }

 private:
  int pumpPin_, trigPin_, echoPin_, tdsPin_, ntcPin_;
  float lastLevel_ = 100.0f;
  float lastRunS_ = 0;
};

}  // namespace spd
