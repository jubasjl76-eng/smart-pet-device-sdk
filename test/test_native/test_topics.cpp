// Host unit tests for spd_topics.h (freestanding C++17, no Arduino).
#include <gtest/gtest.h>
#include "../../src/spd_topics.h"

using namespace spd;

TEST(Topics, buildAndParseRoundTrip) {
  EXPECT_EQ(buildTopic("home", "feeder", "feeder-01", "command"),
            "kennel/home/feeder/feeder-01/command");

  const auto p = parseTopic("kennel/home/feeder/feeder-01/status");
  ASSERT_TRUE(p.valid);
  EXPECT_EQ(p.kennelId, "home");
  EXPECT_EQ(p.deviceType, "feeder");
  EXPECT_EQ(p.deviceId, "feeder-01");
  EXPECT_EQ(p.leaf, "status");
}

TEST(Topics, parseRejectsMalformed) {
  EXPECT_FALSE(parseTopic("kennel/home/feeder/feeder-01").valid);       // too short
  EXPECT_FALSE(parseTopic("kennel/home/toaster/x/status").valid);        // unknown type
  EXPECT_FALSE(parseTopic("dogs/collar-01/location").valid);             // legacy scheme
  EXPECT_FALSE(parseTopic("kennel/home/feeder/bad id/status").valid);    // space in segment
  EXPECT_FALSE(parseTopic("kennel/home/feeder/f1/status/extra").valid);  // too many segments
}

TEST(Topics, legacyDetection) {
  EXPECT_TRUE(isLegacyTopic("dogs/collar-01/location"));
  EXPECT_TRUE(isLegacyTopic("devices/x/telemetry/y"));
  EXPECT_FALSE(isLegacyTopic("kennel/home/gps/collar-01/location"));
}

TEST(Topics, wildcardMatch) {
  EXPECT_TRUE(topicMatches("kennel/+/feeder/+/status", "kennel/home/feeder/f1/status"));
  EXPECT_FALSE(topicMatches("kennel/+/feeder/+/status", "kennel/home/water/w1/status"));
  EXPECT_TRUE(topicMatches("kennel/home/#", "kennel/home/gps/c1/location"));
  EXPECT_FALSE(topicMatches("kennel/home/#", "kennel/away/gps/c1/location"));
}

TEST(Topics, segmentValidation) {
  EXPECT_TRUE(isValidSegment("feeder-01"));
  EXPECT_TRUE(isValidSegment("home.kennel_2:a"));
  EXPECT_FALSE(isValidSegment(""));
  EXPECT_FALSE(isValidSegment("has space"));
  EXPECT_FALSE(isValidSegment(std::string(129, 'x')));
}
