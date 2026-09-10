// spd_time_util.h — freestanding time-trust helpers (no Arduino deps, so the
// native test suite can exercise them). Phase 19 (A12 #16).
#pragma once
#include <cstdint>
#include <ctime>

namespace spd {

// A wall-clock epoch we'll believe: 2024-01-01 .. 2100-01-01.
constexpr time_t kEpoch2024 = 1704067200;
constexpr time_t kEpoch2100 = 4102444800;

inline bool plausibleEpoch(time_t t) { return t >= kEpoch2024 && t < kEpoch2100; }

// device clock minus the authoritative (NTP) time; positive == device ahead.
inline int32_t driftSeconds(time_t authoritative, time_t deviceGuess) {
  return static_cast<int32_t>(static_cast<int64_t>(deviceGuess) -
                              static_cast<int64_t>(authoritative));
}

// Seed the RTC from a persisted last-known-good value only when it's plausible
// AND the running clock isn't already set to a real time (don't clobber NTP).
inline bool shouldRestoreClock(time_t lastKnownGood, time_t systemNow) {
  return plausibleEpoch(lastKnownGood) && !plausibleEpoch(systemNow);
}

}  // namespace spd
