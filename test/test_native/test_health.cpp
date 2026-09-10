#include <gtest/gtest.h>
#include "../../src/spd_health_util.h"

using namespace spd;

TEST(Health, heapVerdictByFreeBytes) {
  const uint32_t bigBlock = 64 * 1024;  // plenty of contiguous space
  EXPECT_EQ(heapVerdict(200 * 1024, bigBlock), HeapVerdict::Ok);
  EXPECT_EQ(heapVerdict(24 * 1024, bigBlock), HeapVerdict::Low);       // == low threshold
  EXPECT_EQ(heapVerdict(20 * 1024, bigBlock), HeapVerdict::Low);
  EXPECT_EQ(heapVerdict(12 * 1024, bigBlock), HeapVerdict::Critical);  // == critical threshold
  EXPECT_EQ(heapVerdict(4 * 1024, bigBlock), HeapVerdict::Critical);
}

TEST(Health, heapVerdictByFragmentation) {
  // lots of free bytes but no large contiguous block -> Low
  EXPECT_EQ(heapVerdict(120 * 1024, 8 * 1024), HeapVerdict::Low);
  EXPECT_EQ(heapVerdict(120 * 1024, 4 * 1024), HeapVerdict::Low);
  EXPECT_EQ(heapVerdict(120 * 1024, 8 * 1024 + 1), HeapVerdict::Ok);
  // critical free always wins, regardless of block size
  EXPECT_EQ(heapVerdict(8 * 1024, 64 * 1024), HeapVerdict::Critical);
}

TEST(Health, customThresholds) {
  HeapThresholds tight{ /*low*/ 4096, /*critical*/ 1024, /*block*/ 1024 };
  EXPECT_EQ(heapVerdict(8192, 8192, tight), HeapVerdict::Ok);
  EXPECT_EQ(heapVerdict(3000, 8192, tight), HeapVerdict::Low);
  EXPECT_EQ(heapVerdict(1000, 8192, tight), HeapVerdict::Critical);
}

TEST(Health, stackLowMargin) {
  EXPECT_TRUE(stackLow(400));
  EXPECT_TRUE(stackLow(511));
  EXPECT_FALSE(stackLow(512));
  EXPECT_FALSE(stackLow(4096));
  EXPECT_TRUE(stackLow(2000, 4096));  // wider margin
}

TEST(Health, verdictStrings) {
  EXPECT_STREQ(heapVerdictStr(HeapVerdict::Ok), "ok");
  EXPECT_STREQ(heapVerdictStr(HeapVerdict::Low), "low");
  EXPECT_STREQ(heapVerdictStr(HeapVerdict::Critical), "critical");
}
