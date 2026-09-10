#include <gtest/gtest.h>
#include "../../src/spd_config_codec.h"

using namespace spd;

TEST(ConfigCodec, crc32KnownVector) {
  EXPECT_EQ(crc32("123456789"), 0xCBF43926u);  // canonical CRC-32/ISO-HDLC
  EXPECT_EQ(crc32(""), 0u);
}

static ConfigFields sample() {
  ConfigFields f;
  f.kennelId = "home";
  f.deviceType = "feeder";
  f.deviceId = "feeder-01";
  f.wifiSsid = "Garden|Net";
  f.wifiPass = "p@ss%w|rd\nline2";  // contains the separator + escape + newline
  f.mqttHost = "10.0.0.5";
  f.mqttPort = 8883;
  f.mqttUser = "device:feeder-01";
  f.mqttPass = "minted-secret";
  f.otaPassword = "ota";
  f.timezone = "WET0WEST,M3.5.0/1,M10.5.0";
  return f;
}

TEST(ConfigCodec, roundTripsEveryField) {
  ConfigFields a = sample();
  ConfigFields b;
  ASSERT_TRUE(deserializeConfig(serializeConfig(a), b));
  EXPECT_EQ(b.kennelId, a.kennelId);
  EXPECT_EQ(b.wifiSsid, a.wifiSsid);
  EXPECT_EQ(b.wifiPass, a.wifiPass);  // survives | % \n
  EXPECT_EQ(b.mqttHost, a.mqttHost);
  EXPECT_EQ(b.mqttPort, 8883);
  EXPECT_EQ(b.mqttUser, a.mqttUser);
  EXPECT_EQ(b.timezone, a.timezone);
}

TEST(ConfigCodec, rejectsTamperedOrTruncated) {
  std::string blob = serializeConfig(sample());
  ConfigFields out;

  std::string flipped = blob;
  flipped[8] ^= 0x10;  // corrupt a byte inside the body
  EXPECT_FALSE(deserializeConfig(flipped, out));

  EXPECT_FALSE(deserializeConfig(blob.substr(0, blob.size() / 2), out));
  EXPECT_FALSE(deserializeConfig("", out));
  EXPECT_FALSE(deserializeConfig("garbage-no-pipes", out));
  EXPECT_FALSE(deserializeConfig("2|only|version|bumped|" + blob, out));  // wrong version
}

TEST(ConfigCodec, plausibilityGate) {
  EXPECT_TRUE(configPlausible(sample()));

  ConfigFields empty;  // fresh device: all blank, port defaulted
  EXPECT_TRUE(configPlausible(empty));

  ConfigFields badType = sample();
  badType.deviceType = "toaster";
  EXPECT_FALSE(configPlausible(badType));

  ConfigFields badPort = sample();
  badPort.mqttPort = 0;
  EXPECT_FALSE(configPlausible(badPort));

  ConfigFields tooLong = sample();
  tooLong.wifiPass = std::string(300, 'x');
  EXPECT_FALSE(configPlausible(tooLong));
}
