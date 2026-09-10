// spd_time.h — NTP wall-clock with a trust model (Phase 19, A12 #16).
//
// The device does not trust its clock until an NTP sync has succeeded *this
// session* (`timeTrusted()`), and the schedule engine refuses to fire until
// then. A persisted last-known-good time (see spd_device.h) is restored on
// boot so timestamps are approximately right immediately and so first-sync
// drift can be measured.
#pragma once
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include "spd_time_util.h"

namespace spd {

class TimeSync {
 public:
  void begin(const String& tz, const char* ntp1 = "pool.ntp.org", const char* ntp2 = "time.nist.gov") {
    tz_ = tz;
    configTzTime(tz_.c_str(), ntp1, ntp2);
  }

  // Seed the RTC from NVS (last-known-good epoch). Call once, after begin(),
  // before the first poll(). No-op if the value is implausible or the clock is
  // already real.
  void restoreLastKnown(time_t lkg) {
    if (!shouldRestoreClock(lkg, time(nullptr))) return;
    struct timeval tv { .tv_sec = lkg, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    restoredFrom_ = lkg;
    restoredAtBootMs_ = millis();
  }

  // Poll from loop(). Returns true once an NTP sync has landed this session.
  bool poll() {
    if (synced_) return true;
    // configTzTime's SNTP runs in the background; a plausible clock means it
    // has corrected us at least once.
    time_t now = time(nullptr);
    if (!plausibleEpoch(now)) return false;

    synced_ = true;
    firstSyncS_ = static_cast<uint32_t>(now);
    if (restoredFrom_ != 0) {
      // What the restored, free-running clock would read now, vs real time.
      // Large == this device's timekeeping is unreliable or it was offline a
      // long while (its RTC does not survive a power cut).
      time_t est = restoredFrom_ + (millis() - restoredAtBootMs_) / 1000UL;
      clockOffsetS_ = driftSeconds(now, est);
      haveOffset_ = true;
    }
    return true;
  }

  // NTP-confirmed this session — the schedule engine gates on this.
  bool isSynced() const { return synced_; }
  bool timeTrusted() const { return synced_; }
  // Has *some* plausible clock (NTP this session, or a restored LKG) — safe to
  // stamp on a journal entry, not safe to fire a schedule from.
  bool timeUsable() const { return synced_ || restoredFrom_ != 0; }

  uint32_t epochS() const { return static_cast<uint32_t>(time(nullptr)); }
  uint32_t firstSyncS() const { return firstSyncS_; }

  // Restored-clock offset measured at the first NTP sync: the persisted
  // free-running estimate minus real time, seconds (positive == ran ahead).
  int32_t clockOffsetS() const { return clockOffsetS_; }
  bool haveClockOffset() const { return haveOffset_; }

  // Local wall-clock helpers for the schedule engine.
  int minuteOfDay() const {
    struct tm t;
    if (!getLocalTime(&t, 5)) return -1;
    return t.tm_hour * 60 + t.tm_min;
  }
  long dayIndex() const {
    return static_cast<long>(time(nullptr) / 86400L);
  }

 private:
  String tz_ = "UTC0";
  bool synced_ = false;
  uint32_t firstSyncS_ = 0;
  time_t restoredFrom_ = 0;
  uint32_t restoredAtBootMs_ = 0;
  int32_t clockOffsetS_ = 0;
  bool haveOffset_ = false;
};

}  // namespace spd
