#include <gtest/gtest.h>
#include "../../src/spd_time_util.h"

using namespace spd;

TEST(TimeTrust, plausibleEpochBounds) {
  EXPECT_FALSE(plausibleEpoch(0));
  EXPECT_FALSE(plausibleEpoch(1700000000));       // 2023-11 — before the floor
  EXPECT_TRUE(plausibleEpoch(kEpoch2024));
  EXPECT_TRUE(plausibleEpoch(1757548800));        // 2025-09
  EXPECT_FALSE(plausibleEpoch(kEpoch2100));       // exclusive upper bound
  EXPECT_FALSE(plausibleEpoch(-1));
}

TEST(TimeTrust, driftSecondsSignAndMagnitude) {
  // device ahead of real time -> positive
  EXPECT_EQ(driftSeconds(1'000'000, 1'000'042), 42);
  // device behind -> negative
  EXPECT_EQ(driftSeconds(1'000'000, 999'900), -100);
  EXPECT_EQ(driftSeconds(1'000'000, 1'000'000), 0);
  // hours of skew still fit in int32
  EXPECT_EQ(driftSeconds(kEpoch2024, kEpoch2024 + 7200), 7200);
}

TEST(TimeTrust, shouldRestoreOnlyWhenClockIsUnsetAndLkgIsGood) {
  // fresh boot: no clock, good LKG -> restore
  EXPECT_TRUE(shouldRestoreClock(kEpoch2024 + 500, 12));
  // clock already real -> never clobber, even with a good LKG
  EXPECT_FALSE(shouldRestoreClock(kEpoch2024 + 500, kEpoch2024 + 400));
  // garbage LKG -> don't restore
  EXPECT_FALSE(shouldRestoreClock(0, 12));
  EXPECT_FALSE(shouldRestoreClock(1000, 0));
}
