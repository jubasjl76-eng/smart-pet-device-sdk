// spd_crash.h — post-crash reporting (hardening Phase 15).
//
// On boot the ESP32 knows why it last reset (esp_reset_reason()). If the last
// run ended in a panic / watchdog / brown-out, SmartPetDevice publishes one
// `crash` event on the next MQTT connect:
//
//   kennel/{k}/{type}/{id}/event  { "event":"crash", "reason":"panic",
//     "rawReason":4, "fw":"1.2.0", "heapFree":123456, "minHeapFree":98765 }
//
// The backend (smart-pet-backend engine) forwards it to Sentry + raises a
// care-inbox exception. No coredump/backtrace — just the reason + heap vitals;
// the timing correlates with server logs.
#pragma once
#include <Arduino.h>
#include <esp_system.h>

namespace spd {

struct CrashInfo {
  esp_reset_reason_t raw = ESP_RST_UNKNOWN;
  const char* reason = "unknown";
  bool isCrash = false;
};

inline const char* resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "sw";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_wdt";
    case ESP_RST_TASK_WDT:  return "task_wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    default:                return "unknown";
  }
}

// A reset we want a human to see. Power-on / external / software resets and
// deep-sleep wakeups are normal and are NOT reported.
inline bool isCrashReason(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
         r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
         r == ESP_RST_BROWNOUT;
}

inline CrashInfo readCrashInfo() {
  const esp_reset_reason_t r = esp_reset_reason();
  return { r, resetReasonStr(r), isCrashReason(r) };
}

}  // namespace spd
