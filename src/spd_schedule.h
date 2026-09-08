// spd_schedule.h — on-device feeding/dosing schedule cache + next-due maths.
// Freestanding C++17, host-tested. The device caches this in NVS so schedules
// keep firing through a Wi-Fi / cloud outage (RTC time only).
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace spd {

struct ScheduleEntry {
  std::string id;
  uint8_t hour = 0;     // 0..23  (local wall-clock, after NTP)
  uint8_t minute = 0;   // 0..59
  float amount = 0;     // grams (feeder) or seconds/ml (water)
  bool enabled = true;
};

// Parse "HH:MM" → minutes since midnight, or -1 if malformed.
inline int parseHhMm(const std::string& s) {
  if (s.size() != 5 || s[2] != ':') return -1;
  auto digit = [](char c) { return c >= '0' && c <= '9'; };
  if (!digit(s[0]) || !digit(s[1]) || !digit(s[3]) || !digit(s[4])) return -1;
  int h = (s[0] - '0') * 10 + (s[1] - '0');
  int m = (s[3] - '0') * 10 + (s[4] - '0');
  if (h > 23 || m > 59) return -1;
  return h * 60 + m;
}

inline int entryMinutes(const ScheduleEntry& e) { return e.hour * 60 + e.minute; }

class ScheduleCache {
 public:
  void set(std::vector<ScheduleEntry> entries) {
    entries_ = std::move(entries);
    std::sort(entries_.begin(), entries_.end(),
              [](const ScheduleEntry& a, const ScheduleEntry& b) {
                return entryMinutes(a) < entryMinutes(b);
              });
    lastFiredMinuteOfDay_ = -1;
    lastFiredDayIndex_ = -1;
  }

  const std::vector<ScheduleEntry>& entries() const { return entries_; }
  size_t size() const { return entries_.size(); }

  // Given the current wall-clock (minutes since midnight + an absolute day index
  // so "same slot next day" is distinguishable), return the entry that is due
  // now and has not already fired this slot, else nullptr.
  //
  // `windowMinutes` tolerates a device that was asleep across the exact minute.
  const ScheduleEntry* due(int minuteOfDay, long dayIndex, int windowMinutes = 5) {
    const ScheduleEntry* hit = nullptr;
    for (const auto& e : entries_) {
      if (!e.enabled) continue;
      int em = entryMinutes(e);
      int delta = minuteOfDay - em;
      if (delta < 0 || delta > windowMinutes) continue;
      // already fired this slot today?
      if (dayIndex == lastFiredDayIndex_ && em == lastFiredMinuteOfDay_) continue;
      hit = &e;  // last match in the sorted list within the window
    }
    return hit;
  }

  void markFired(const ScheduleEntry& e, long dayIndex) {
    lastFiredMinuteOfDay_ = entryMinutes(e);
    lastFiredDayIndex_ = dayIndex;
  }

  // Minutes until the next enabled slot (wraps to tomorrow). -1 if none.
  int minutesUntilNext(int minuteOfDay) const {
    int best = -1;
    for (const auto& e : entries_) {
      if (!e.enabled) continue;
      int em = entryMinutes(e);
      int diff = em - minuteOfDay;
      if (diff <= 0) diff += 24 * 60;
      if (best < 0 || diff < best) best = diff;
    }
    return best;
  }

 private:
  std::vector<ScheduleEntry> entries_;
  int lastFiredMinuteOfDay_ = -1;
  long lastFiredDayIndex_ = -1;
};

}  // namespace spd
