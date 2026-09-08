// spd_time.h — NTP wall-clock. Schedules and journal timestamps depend on this;
// the device does not trust its clock until sync() has succeeded once.
#pragma once
#include <Arduino.h>
#include <time.h>

namespace spd {

class TimeSync {
 public:
  void begin(const String& tz, const char* ntp1 = "pool.ntp.org", const char* ntp2 = "time.nist.gov") {
    tz_ = tz;
    configTzTime(tz_.c_str(), ntp1, ntp2);
  }

  // Poll from loop(); returns true once the clock looks valid (year >= 2024).
  bool poll() {
    if (synced_) return true;
    time_t now = time(nullptr);
    if (now > 1704067200) {  // 2024-01-01
      synced_ = true;
      firstSyncS_ = (uint32_t)now;
    }
    return synced_;
  }

  bool isSynced() const { return synced_; }
  uint32_t epochS() const { return (uint32_t)time(nullptr); }
  uint32_t firstSyncS() const { return firstSyncS_; }

  // Local wall-clock helpers for the schedule engine.
  int minuteOfDay() const {
    struct tm t;
    if (!getLocalTime(&t, 5)) return -1;
    return t.tm_hour * 60 + t.tm_min;
  }
  long dayIndex() const {
    return (long)(time(nullptr) / 86400L);  // absolute day count; distinguishes "same slot tomorrow"
  }

 private:
  String tz_ = "UTC0";
  bool synced_ = false;
  uint32_t firstSyncS_ = 0;
};

}  // namespace spd
