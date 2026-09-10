#include <gtest/gtest.h>
#include <cctype>
#include <cstring>
#include "../../src/spd_ota_util.h"

using namespace spd;

// SHA-256("") digest bytes.
static const uint8_t kDigest[32] = {
  0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
  0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
};

TEST(OtaUtil, toHexLower) {
  const uint8_t b[] = { 0x00, 0x0f, 0xa0, 0xff };
  EXPECT_EQ(toHexLower(b, 4), "000fa0ff");
  EXPECT_EQ(toHexLower(kDigest, 32).size(), 64u);
  EXPECT_EQ(toHexLower(kDigest, 32),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(OtaUtil, sha256HexEqualMatches) {
  const std::string hex = toHexLower(kDigest, 32);
  EXPECT_TRUE(sha256HexEqual(kDigest, hex));
  std::string upper = hex;
  for (auto& c : upper) c = (char)toupper((unsigned char)c);
  EXPECT_TRUE(sha256HexEqual(kDigest, upper));
  EXPECT_TRUE(sha256HexEqual(kDigest, "0x" + hex));
  EXPECT_TRUE(sha256HexEqual(kDigest, "  " + hex + "\n"));
}

TEST(OtaUtil, sha256HexEqualRejects) {
  const std::string hex = toHexLower(kDigest, 32);
  EXPECT_FALSE(sha256HexEqual(kDigest, ""));
  EXPECT_FALSE(sha256HexEqual(kDigest, "deadbeef"));          // too short
  EXPECT_FALSE(sha256HexEqual(kDigest, hex + "00"));          // too long
  EXPECT_FALSE(sha256HexEqual(kDigest, "z" + hex.substr(1))); // non-hex char

  uint8_t off[32];
  std::memcpy(off, kDigest, 32);
  off[31] ^= 0x01;
  EXPECT_FALSE(sha256HexEqual(off, hex));  // one bit different
}
