// spd_health_util.h — freestanding (no Arduino) memory-pressure verdicts, so
// the native tests can exercise the thresholds. Phase 19 (on-device memory
// guards).
#pragma once
#include <cstdint>

namespace spd {

enum class HeapVerdict : uint8_t { Ok, Low, Critical };

struct HeapThresholds {
  uint32_t lowFreeBytes = 24 * 1024;       // below -> shed optional work
  uint32_t criticalFreeBytes = 12 * 1024;  // below -> shed everything optional
  uint32_t lowBlockBytes = 8 * 1024;       // largest contiguous block (fragmentation)
};

inline HeapVerdict heapVerdict(uint32_t freeBytes, uint32_t largestBlockBytes,
                               const HeapThresholds& th = HeapThresholds{}) {
  if (freeBytes <= th.criticalFreeBytes) return HeapVerdict::Critical;
  if (freeBytes <= th.lowFreeBytes || largestBlockBytes <= th.lowBlockBytes)
    return HeapVerdict::Low;
  return HeapVerdict::Ok;
}

// uxTaskGetStackHighWaterMark reports the *minimum free* stack ever seen for a
// task. Flag when it's within a safety margin of overflow.
inline bool stackLow(uint32_t minFreeStackBytes, uint32_t marginBytes = 512) {
  return minFreeStackBytes < marginBytes;
}

inline const char* heapVerdictStr(HeapVerdict v) {
  switch (v) {
    case HeapVerdict::Ok: return "ok";
    case HeapVerdict::Low: return "low";
    case HeapVerdict::Critical: return "critical";
  }
  return "ok";
}

}  // namespace spd
