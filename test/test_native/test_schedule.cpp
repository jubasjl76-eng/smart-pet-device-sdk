#include <gtest/gtest.h>
#include "../../src/spd_schedule.h"

using namespace spd;

TEST(Schedule, parseHhMm) {
  EXPECT_EQ(parseHhMm("08:00"), 480);
  EXPECT_EQ(parseHhMm("23:59"), 23 * 60 + 59);
  EXPECT_EQ(parseHhMm("24:00"), -1);
  EXPECT_EQ(parseHhMm("8:00"), -1);
}

class ScheduleFixture : public ::testing::Test {
 protected:
  ScheduleCache sc;
  void SetUp() override {
    sc.set({
      { "b", 20, 0, 30, true },
      { "a", 8, 0, 40, true },
      { "c", 12, 30, 25, false },  // disabled
    });
  }
};

TEST_F(ScheduleFixture, sortedByTime) {
  ASSERT_EQ(sc.size(), 3u);
  EXPECT_EQ(sc.entries()[0].id, "a");
}

TEST_F(ScheduleFixture, firesOncePerSlotThenNextDay) {
  const ScheduleEntry* d = sc.due(8 * 60, 100);
  ASSERT_NE(d, nullptr);
  EXPECT_EQ(d->id, "a");

  sc.markFired(*d, 100);
  EXPECT_EQ(sc.due(8 * 60, 100), nullptr);        // same slot, same day
  EXPECT_NE(sc.due(8 * 60, 101), nullptr);        // next day
}

TEST_F(ScheduleFixture, disabledNeverFires) {
  EXPECT_EQ(sc.due(12 * 60 + 30, 101), nullptr);
}

TEST_F(ScheduleFixture, lateWindowTolerance) {
  EXPECT_NE(sc.due(20 * 60 + 3, 101), nullptr);   // 3 min late: inside the 5-min window
  EXPECT_EQ(sc.due(20 * 60 + 6, 102), nullptr);   // 6 min late: outside
}

TEST_F(ScheduleFixture, minutesUntilNext) {
  EXPECT_EQ(sc.minutesUntilNext(7 * 60), 60);                     // to 08:00
  EXPECT_EQ(sc.minutesUntilNext(21 * 60), (8 * 60) + (3 * 60));   // wraps to 08:00 next day
}
