#include <gtest/gtest.h>
#include <cstdint>
#include <cstdlib>
#include "../../src/spd_ulaw.h"

using namespace spd;

TEST(Ulaw, roundTripStaysWithinStepSize) {
  for (int s = -32000; s <= 32000; s += 250) {
    const int16_t back = ulawDecode(ulawEncode(static_cast<int16_t>(s)));
    const int err = std::abs(static_cast<int>(back) - s);
    EXPECT_LE(err, (std::abs(s) / 8) + 260) << "sample " << s;
  }
}

TEST(Ulaw, silenceAndSign) {
  EXPECT_EQ(ulawEncode(0), 0xFF);
  EXPECT_LE(std::abs(static_cast<int>(ulawDecode(ulawEncode(0)))), 8);
  EXPECT_LT(ulawDecode(ulawEncode(-12345)), 0);
  EXPECT_GT(ulawDecode(ulawEncode(12345)), 0);
}

TEST(Ulaw, blockHelpersMatchScalar) {
  const int16_t pcm[4] = {0, 1000, -1000, 30000};
  uint8_t u[4];
  int16_t out[4];
  ulawEncodeBlock(pcm, u, 4);
  ulawDecodeBlock(u, out, 4);
  EXPECT_EQ(out[0], ulawDecode(u[0]));
  EXPECT_GT(out[3], 20000);
}
