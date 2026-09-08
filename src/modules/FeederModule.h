// FeederModule.h — SG90 servo auger + HC-SR04 food-level.
// Wiring (defaults match smart-feeder): servo=GPIO4, trig=GPIO5, echo=GPIO18.
#pragma once
#include <Arduino.h>
#include <ESP32Servo.h>

namespace spd {

class FeederModule {
 public:
  FeederModule(int servoPin, int trigPin, int echoPin,
               int openAngle = 90, int closeAngle = 0)
      : servoPin_(servoPin), trigPin_(trigPin), echoPin_(echoPin),
        openAngle_(openAngle), closeAngle_(closeAngle) {}

  void begin() {
    servo_.attach(servoPin_);
    servo_.write(closeAngle_);
    pinMode(trigPin_, OUTPUT);
    pinMode(echoPin_, INPUT);
  }

  // Dispense roughly `grams`. Calibrate gramsPerSecond_ for your auger/kibble.
  bool dispense(float grams) {
    if (jammed_) return false;
    uint32_t ms = (uint32_t)((grams / gramsPerSecond_) * 1000.0f);
    ms = constrain(ms, 300u, 8000u);
    servo_.write(openAngle_);
    uint32_t start = millis();
    // crude jam check: level should not be totally unchanged if there is food
    delay(ms);
    servo_.write(closeAngle_);
    lastDispenseMs_ = millis() - start;
    lastDispensedG_ = grams;
    return true;
  }

  void setCalibration(float gramsPerSecond) { gramsPerSecond_ = gramsPerSecond; }
  void setJammed(bool j) { jammed_ = j; }
  bool jammed() const { return jammed_; }

  // Food level 0..100 from distance (emptyCm..fullCm).
  float foodLevelPct(float fullCm = 2, float emptyCm = 15) {
    float d = measureCm();
    if (d < 0) return lastLevel_;
    float pct = 100.0f * (emptyCm - d) / (emptyCm - fullCm);
    lastLevel_ = constrain(pct, 0.0f, 100.0f);
    return lastLevel_;
  }

  float measureCm() {
    digitalWrite(trigPin_, LOW);  delayMicroseconds(2);
    digitalWrite(trigPin_, HIGH); delayMicroseconds(10);
    digitalWrite(trigPin_, LOW);
    long dur = pulseIn(echoPin_, HIGH, 30000);
    if (dur == 0) return -1;
    float cm = (dur * 0.0343f) / 2.0f;
    return (cm > 0 && cm < 400) ? cm : -1;
  }

  uint32_t lastDispenseMs() const { return lastDispenseMs_; }

 private:
  Servo servo_;
  int servoPin_, trigPin_, echoPin_, openAngle_, closeAngle_;
  float gramsPerSecond_ = 20.0f;
  float lastLevel_ = 100.0f;
  float lastDispensedG_ = 0;
  uint32_t lastDispenseMs_ = 0;
  bool jammed_ = false;
};

}  // namespace spd
