// spd_config_codec.h — freestanding (no Arduino) codec for the device config:
// a single CRC-checked string so an NVS write is one atomic entry and a power
// cut mid-write can't leave a half-updated config. Phase 19 (A12 #15).
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace spd {

// IEEE CRC-32 (poly 0xEDB88320), table-less.
inline uint32_t crc32(const std::string& s) {
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char b : s) {
    c ^= b;
    for (int i = 0; i < 8; ++i) c = (c >> 1) ^ (0xEDB88320u & (~(c & 1) + 1));
  }
  return c ^ 0xFFFFFFFFu;
}

// Config fields, Arduino-String-free so the native tests can round-trip them.
struct ConfigFields {
  std::string kennelId, deviceType, deviceId, wifiSsid, wifiPass, mqttHost,
      mqttUser, mqttPass, otaPassword, timezone;
  uint16_t mqttPort = 1883;
};

namespace detail {
// Percent-encode the field separator and the escape char so a Wi-Fi password
// containing '|' or '%' survives the round trip.
inline std::string enc(const std::string& in) {
  std::string o;
  o.reserve(in.size());
  for (char ch : in) {
    if (ch == '|' || ch == '%' || ch == '\n' || ch == '\r') {
      static const char* hex = "0123456789ABCDEF";
      o += '%';
      o += hex[(ch >> 4) & 0xF];
      o += hex[ch & 0xF];
    } else {
      o += ch;
    }
  }
  return o;
}
inline std::string dec(const std::string& in) {
  std::string o;
  o.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size()) {
      o += static_cast<char>(std::strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      o += in[i];
    }
  }
  return o;
}
inline std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : s) {
    if (ch == sep) { out.push_back(cur); cur.clear(); }
    else cur += ch;
  }
  out.push_back(cur);
  return out;
}
}  // namespace detail

// "1|<11 encoded fields>|<crc hex>"  — crc covers everything up to the last '|'.
inline std::string serializeConfig(const ConfigFields& f) {
  using detail::enc;
  std::string body = "1|" + enc(f.kennelId) + "|" + enc(f.deviceType) + "|" +
                     enc(f.deviceId) + "|" + enc(f.wifiSsid) + "|" + enc(f.wifiPass) +
                     "|" + enc(f.mqttHost) + "|" + std::to_string(f.mqttPort) + "|" +
                     enc(f.mqttUser) + "|" + enc(f.mqttPass) + "|" + enc(f.otaPassword) +
                     "|" + enc(f.timezone);
  char crc[9];
  std::snprintf(crc, sizeof(crc), "%08X", crc32(body));
  return body + "|" + crc;
}

inline bool deserializeConfig(const std::string& blob, ConfigFields& f) {
  auto parts = detail::split(blob, '|');
  if (parts.size() != 13) return false;          // "1" + 11 fields + crc
  if (parts[0] != "1") return false;
  const std::string& crcStr = parts.back();
  std::string body = blob.substr(0, blob.size() - crcStr.size() - 1);
  char want[9];
  std::snprintf(want, sizeof(want), "%08X", crc32(body));
  if (crcStr != want) return false;

  using detail::dec;
  f.kennelId = dec(parts[1]);
  f.deviceType = dec(parts[2]);
  f.deviceId = dec(parts[3]);
  f.wifiSsid = dec(parts[4]);
  f.wifiPass = dec(parts[5]);
  f.mqttHost = dec(parts[6]);
  long port = std::strtol(parts[7].c_str(), nullptr, 10);
  f.mqttPort = (port >= 1 && port <= 65535) ? static_cast<uint16_t>(port) : 1883;
  f.mqttUser = dec(parts[8]);
  f.mqttPass = dec(parts[9]);
  f.otaPassword = dec(parts[10]);
  f.timezone = dec(parts[11]);
  return true;
}

// Cheap sanity gate — a config that fails this is treated as corrupt.
inline bool configPlausible(const ConfigFields& f) {
  auto ok = [](const std::string& s) { return s.size() <= 256; };
  if (!(ok(f.kennelId) && ok(f.deviceType) && ok(f.deviceId) && ok(f.wifiSsid) &&
        ok(f.wifiPass) && ok(f.mqttHost) && ok(f.mqttUser) && ok(f.mqttPass) &&
        ok(f.otaPassword) && ok(f.timezone)))
    return false;
  if (f.mqttPort == 0) return false;
  // deviceType, when set, must be one we recognise.
  if (!f.deviceType.empty()) {
    static const char* kinds[] = {"feeder", "water", "door", "scale",
                                  "sensor", "gps",   "hub",  "camera"};
    bool known = false;
    for (auto* k : kinds) if (f.deviceType == k) { known = true; break; }
    if (!known) return false;
  }
  return true;
}

}  // namespace spd
