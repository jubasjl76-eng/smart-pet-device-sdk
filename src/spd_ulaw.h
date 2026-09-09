// spd_ulaw.h — G.711 mu-law encode/decode. Freestanding C++17 (host-tested):
// halves the byte count of a 16-bit PCM mic stream before it goes on the wire.
//
//   uint8_t  u  = spd::ulawEncode(pcm16);
//   int16_t  p  = spd::ulawDecode(u);
//   spd::ulawEncodeBlock(pcm, u, n);  // n samples
#pragma once
#include <cstdint>
#include <cstddef>

namespace spd {

inline uint8_t ulawEncode(int16_t pcm) {
  const uint16_t BIAS = 0x84;
  const int16_t CLIP = 32635;
  uint8_t sign = (pcm >> 8) & 0x80;
  int16_t mag = sign ? -pcm : pcm;
  if (mag > CLIP) mag = CLIP;
  mag = (int16_t)(mag + BIAS);

  int exponent = 7;
  for (int mask = 0x4000; (mag & mask) == 0 && exponent > 0; mask >>= 1) exponent--;
  int mantissa = (mag >> (exponent + 3)) & 0x0F;
  return (uint8_t)~(sign | (exponent << 4) | mantissa);
}

inline int16_t ulawDecode(uint8_t u) {
  u = (uint8_t)~u;
  int sign = u & 0x80;
  int exponent = (u >> 4) & 0x07;
  int mantissa = u & 0x0F;
  int sample = ((mantissa << 3) + 0x84) << exponent;
  sample -= 0x84;
  return (int16_t)(sign ? -sample : sample);
}

inline void ulawEncodeBlock(const int16_t* pcm, uint8_t* out, size_t n) {
  for (size_t i = 0; i < n; ++i) out[i] = ulawEncode(pcm[i]);
}
inline void ulawDecodeBlock(const uint8_t* in, int16_t* pcm, size_t n) {
  for (size_t i = 0; i < n; ++i) pcm[i] = ulawDecode(in[i]);
}

}  // namespace spd
