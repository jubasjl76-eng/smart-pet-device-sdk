// Contract test — golden values from the MQTT protocol v2 (smart-pet-mqtt
// asyncapi.yaml + @jubasjl76-eng/mqtt-contract). If spd_topics.h ever drifts
// from the protocol, this fails.
#include <gtest/gtest.h>
#include "../../src/spd_topics.h"

using namespace spd;

TEST(Contract, topicScheme) {
  // kennel/{kennelId}/{deviceType}/{deviceId}/{leaf}
  EXPECT_EQ(buildTopic("k", "feeder", "d", "status"), "kennel/k/feeder/d/status");
  EXPECT_EQ(buildTopic("k", "gps", "d", "location"), "kennel/k/gps/d/location");
}

TEST(Contract, deviceTypesAccepted) {
  for (const char* t : {"feeder", "water", "door", "sensor", "gps", "camera", "scale", "hub"}) {
    EXPECT_TRUE(parseTopic(std::string("kennel/k/") + t + "/d/status").valid) << t;
  }
  EXPECT_FALSE(parseTopic("kennel/k/collar/d/status").valid);  // not a v2 device type
}

TEST(Contract, deliveryPolicyMatchesTheTable) {
  // command : QoS 2, not retained
  EXPECT_EQ(deliveryFor("command").qos, Qos::ExactlyOnce);
  EXPECT_FALSE(deliveryFor("command").retain);

  // status : QoS 1, retained (also the LWT topic)
  EXPECT_EQ(deliveryFor("status").qos, Qos::AtLeastOnce);
  EXPECT_TRUE(deliveryFor("status").retain);

  // event / ack / telemetry / location / presence / audio / <metric> : QoS 1, not retained
  for (const char* leaf : {"event", "ack", "telemetry", "location", "presence", "audio", "temperature"}) {
    EXPECT_EQ(deliveryFor(leaf).qos, Qos::AtLeastOnce) << leaf;
    EXPECT_FALSE(deliveryFor(leaf).retain) << leaf;
  }
}

TEST(Contract, backendFanInFilters) {
  // A backend subscribing per-type status wildcards must match its devices.
  EXPECT_TRUE(topicMatches("kennel/+/feeder/+/status", "kennel/home/feeder/f1/status"));
  EXPECT_TRUE(topicMatches("kennel/+/+/+/event", "kennel/home/door/d1/event"));
  EXPECT_TRUE(topicMatches("kennel/+/gps/+/location", "kennel/home/gps/c1/location"));
}
