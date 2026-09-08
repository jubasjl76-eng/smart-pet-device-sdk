// EnvSensorModule.h — whelping-room / kennel-zone environment.
// Generic: pass reader lambdas so it works with DHT22, SHT31, BME280, MQ-135, a
// reed door switch, a PIR — whatever is wired. The SDK publishes each as its own
// metric leaf (temperature / humidity / airquality) plus a door/motion event.
#pragma once
#include <Arduino.h>
#include <functional>

namespace spd {

class EnvSensorModule {
 public:
  using Reader = std::function<float()>;
  using BoolReader = std::function<bool()>;

  void setTemperature(Reader r) { temp_ = std::move(r); }
  void setHumidity(Reader r) { hum_ = std::move(r); }
  void setAirQuality(Reader r) { air_ = std::move(r); }
  void setDoor(BoolReader r) { door_ = std::move(r); }     // true = open
  void setMotion(BoolReader r) { motion_ = std::move(r); }

  bool hasTemperature() const { return (bool)temp_; }
  bool hasHumidity() const { return (bool)hum_; }
  bool hasAirQuality() const { return (bool)air_; }

  float temperature() { return temp_ ? temp_() : NAN; }
  float humidity() { return hum_ ? hum_() : NAN; }
  float airQuality() { return air_ ? air_() : NAN; }

  // Edge-detect door / motion. Returns 1 (rising), -1 (falling), 0 (no change).
  int pollDoor() { return edge(door_, doorLast_); }
  int pollMotion() { return edge(motion_, motionLast_); }

 private:
  int edge(BoolReader& r, int& last) {
    if (!r) return 0;
    int cur = r() ? 1 : 0;
    if (last < 0) { last = cur; return 0; }
    int d = cur - last;
    last = cur;
    return d;  // +1 or -1 or 0
  }
  Reader temp_, hum_, air_;
  BoolReader door_, motion_;
  int doorLast_ = -1, motionLast_ = -1;
};

}  // namespace spd
