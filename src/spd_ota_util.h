// spd_ota_util.h — freestanding (no Arduino) hex helpers for OTA hash verify,
// so the native tests can exercise the comparison. Phase 19 (OTA provenance).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace spd {

inline std::string toHexLower(const uint8_t* p, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    s += d[p[i] >> 4];
    s += d[p[i] & 0x0F];
  }
  return s;
}

// Compare a 32-byte digest to an expected hex string. Case-insensitive;
// tolerates a "0x" prefix and surrounding whitespace. Returns false on any
// length / character mismatch.
inline bool sha256HexEqual(const uint8_t digest[32], const std::string& expect) {
  size_t a = 0, b = expect.size();
  while (a < b && (expect[a] == ' ' || expect[a] == '\t' || expect[a] == '\n' || expect[a] == '\r')) ++a;
  while (b > a && (expect[b - 1] == ' ' || expect[b - 1] == '\t' || expect[b - 1] == '\n' || expect[b - 1] == '\r')) --b;
  if (b - a >= 2 && expect[a] == '0' && (expect[a + 1] == 'x' || expect[a + 1] == 'X')) a += 2;
  if (b - a != 64) return false;

  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < 32; ++i) {
    int hi = nib(expect[a + i * 2]);
    int lo = nib(expect[a + i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    if ((uint8_t)((hi << 4) | lo) != digest[i]) return false;
  }
  return true;
}

}  // namespace spd
