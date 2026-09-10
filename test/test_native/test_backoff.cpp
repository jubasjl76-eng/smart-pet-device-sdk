#include <gtest/gtest.h>
#include <cstdint>
#include "../../src/spd_backoff.h"

using namespace spd;

TEST(Backoff, doublesUntilCapped) {
  Backoff b(1000, 60000);
  EXPECT_EQ(b.peekCeiling(), 1000u);
  b.next(0.5);
  EXPECT_EQ(b.peekCeiling(), 2000u);
  b.next(0.5);
  EXPECT_EQ(b.peekCeiling(), 4000u);
  for (int i = 0; i < 20; ++i) b.next(0.9);
  EXPECT_EQ(b.peekCeiling(), 60000u);  // capped
}

TEST(Backoff, fullJitterStaysInRange) {
  Backoff c(1000, 60000);
  EXPECT_EQ(c.next(0.0), 250u);  // floor = base / 4
  const uint32_t d1 = c.next(1.0);
  EXPECT_GE(d1, 250u);
  EXPECT_LE(d1, 2000u);
}

TEST(Backoff, resetClearsAttempt) {
  Backoff c(1000, 60000);
  c.next(0.5);
  c.next(0.5);
  c.reset();
  EXPECT_EQ(c.attempt(), 0u);
}
