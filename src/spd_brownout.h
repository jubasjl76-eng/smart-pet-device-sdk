// spd_brownout.h — brown-out / power-loss awareness (Phase 19, A12 #15).
//
// The ESP32's hardware brown-out detector already resets the chip when Vdd
// sags, which stops a low-voltage MCU from scribbling garbage into flash. What
// it doesn't give you is a running tally, so a device on a marginal PSU looks
// the same as a one-off dip. We keep a counter in RTC_NOINIT memory (survives
// the brown-out reset — the RTC domain is powered longest) and expose it for
// status telemetry.
#pragma once
#include <Arduino.h>
#include <esp_system.h>

namespace spd {

inline RTC_NOINIT_ATTR uint32_t g_brownoutCount;
inline RTC_NOINIT_ATTR uint32_t g_brownoutMagic;

struct BrownoutInfo {
  uint32_t count = 0;      // brown-out resets since the last true cold boot
  bool thisBoot = false;   // did *this* boot follow a brown-out reset?
};

// Call once at the top of begin(). Initialises the RTC counter on a cold boot
// and bumps it when we're recovering from a brown-out reset.
inline BrownoutInfo brownoutBootCheck() {
  constexpr uint32_t kMagic = 0xB0B0CAFEu;
  if (g_brownoutMagic != kMagic) {  // RTC memory was lost -> genuine cold boot
    g_brownoutMagic = kMagic;
    g_brownoutCount = 0;
  }
  BrownoutInfo info;
  info.thisBoot = (esp_reset_reason() == ESP_RST_BROWNOUT);
  if (info.thisBoot) g_brownoutCount++;
  info.count = g_brownoutCount;
  return info;
}

}  // namespace spd
