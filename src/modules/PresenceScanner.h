// PresenceScanner.h — BLE scan for collar tags, to identify WHICH dog is at a
// shared feeder / water unit / pen. Emits the strongest tag seen (and the list),
// which the backend turns into per-animal attribution + a wrong-pen alert.
//
// A "tag" is any BLE advertiser whose name or manufacturer data matches the
// configured prefix (e.g. the collar advertising "spd-tag-7"). Uses the ESP32
// Arduino BLE library (BLEDevice).
#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <vector>
#include <algorithm>

namespace spd {

// BLEScan::start() returns BLEScanResults by value on ESP32 Arduino core 2.x and
// a pointer on 3.x. Bind the result to a local `auto` and pass it through here to
// get a reference either way.
namespace detail {
inline BLEScanResults& scanRef(BLEScanResults& r) { return r; }   // core 2.x: local value
inline BLEScanResults& scanRef(BLEScanResults* p) { return *p; }  // core 3.x: pointer
}  // namespace detail

struct TagSighting {
  String tagId;
  int rssi;
};

class PresenceScanner {
 public:
  void begin(const String& namePrefix = "spd-tag-") {
    prefix_ = namePrefix;
    BLEDevice::init("");
    scan_ = BLEDevice::getScan();
    scan_->setActiveScan(true);
    scan_->setInterval(100);
    scan_->setWindow(80);
  }

  // Blocking scan for `seconds`; returns sightings sorted strongest-first.
  std::vector<TagSighting> scan(uint32_t seconds = 2) {
    std::vector<TagSighting> out;
    auto started = scan_->start(seconds, false);
    BLEScanResults& r = detail::scanRef(started);
    for (int i = 0; i < r.getCount(); ++i) {
      BLEAdvertisedDevice d = r.getDevice(i);
      String name = d.haveName() ? String(d.getName().c_str()) : "";
      if (!name.startsWith(prefix_)) continue;
      out.push_back({ name.substring(prefix_.length()), d.getRSSI() });
    }
    scan_->clearResults();
    std::sort(out.begin(), out.end(), [](const TagSighting& a, const TagSighting& b) {
      return a.rssi > b.rssi;
    });
    last_ = out;
    return out;
  }

  const std::vector<TagSighting>& last() const { return last_; }
  const char* strongestTag() const { return last_.empty() ? nullptr : last_.front().tagId.c_str(); }
  int strongestRssi() const { return last_.empty() ? 0 : last_.front().rssi; }

 private:
  BLEScan* scan_ = nullptr;
  String prefix_ = "spd-tag-";
  std::vector<TagSighting> last_;
};

}  // namespace spd
