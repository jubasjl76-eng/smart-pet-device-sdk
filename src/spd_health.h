// spd_health.h — on-device memory guards (Phase 19).
//
// Polls heap + stack head-room, gives a shed-work verdict for OOM-safe
// degradation, and — in a debug build (-DSPD_DEBUG or CORE_DEBUG_LEVEL >= 4) —
// periodically checks heap integrity and panics on corruption so the crash
// path reports it.
#pragma once
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "spd_health_util.h"

namespace spd {

class HealthMonitor {
 public:
  void poll() {
    freeHeap_ = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    minFreeHeap_ = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    largestBlock_ = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    uint32_t stackNow = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
    if (minStack_ == 0 || stackNow < minStack_) minStack_ = stackNow;

#if defined(SPD_DEBUG) || (defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL >= 4)
    if (millis() - lastIntegrityMs_ > kIntegrityEveryMs) {
      lastIntegrityMs_ = millis();
      if (!heap_caps_check_integrity_all(true)) {
        Serial.println("[spd] HEAP CORRUPTION — restarting");
        delay(50);
        abort();  // -> panic -> crash event on next boot (spd_crash.h)
      }
    }
#endif
  }

  HeapVerdict verdict() const { return heapVerdict(freeHeap_, largestBlock_); }
  bool degraded() const { return verdict() != HeapVerdict::Ok; }
  bool critical() const { return verdict() == HeapVerdict::Critical; }

  uint32_t freeHeap() const { return freeHeap_; }
  uint32_t minFreeHeap() const { return minFreeHeap_; }
  uint32_t largestBlock() const { return largestBlock_; }
  uint32_t minStack() const { return minStack_; }
  bool stackLow() const { return spd::stackLow(minStack_); }

 private:
  static constexpr uint32_t kIntegrityEveryMs = 30000;
  uint32_t freeHeap_ = 0, minFreeHeap_ = 0, largestBlock_ = 0, minStack_ = 0;
  uint32_t lastIntegrityMs_ = 0;
};

}  // namespace spd
