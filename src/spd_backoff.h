// spd_backoff.h — exponential reconnect backoff with full jitter.
// Freestanding C++17, host-tested.
#pragma once
#include <cstdint>
#include <algorithm>

namespace spd {

class Backoff {
 public:
  Backoff(uint32_t baseMs = 1000, uint32_t maxMs = 60000)
      : baseMs_(baseMs), maxMs_(maxMs) {}

  // Next delay. `rnd01` is a caller-supplied uniform in [0,1) so the maths stays
  // testable (device passes esp_random()/UINT32_MAX).
  uint32_t next(double rnd01) {
    uint32_t ceiling = ceilingFor(attempt_);
    ++attempt_;
    uint32_t jittered = static_cast<uint32_t>(rnd01 * ceiling);
    return std::max<uint32_t>(baseMs_ / 4, jittered);
  }

  uint32_t peekCeiling() const { return ceilingFor(attempt_); }
  uint32_t attempt() const { return attempt_; }
  void reset() { attempt_ = 0; }

 private:
  uint32_t ceilingFor(uint32_t a) const {
    uint64_t v = static_cast<uint64_t>(baseMs_) << std::min<uint32_t>(a, 20);
    return static_cast<uint32_t>(std::min<uint64_t>(v, maxMs_));
  }
  uint32_t baseMs_;
  uint32_t maxMs_;
  uint32_t attempt_ = 0;
};

}  // namespace spd
