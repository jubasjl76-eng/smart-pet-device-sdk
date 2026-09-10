// spd_topics.h — canonical Smart Pet MQTT topic scheme (C++ mirror of
// smart-pet-mqtt/src/topics.ts). Freestanding C++17: no Arduino headers, so it
// compiles and is unit-tested on the host.
//
//   kennel/{kennelId}/{deviceType}/{deviceId}/{leaf}
#pragma once
#include <string>
#include <array>
#include <cstdint>

namespace spd {

enum class Qos : uint8_t { AtMostOnce = 0, AtLeastOnce = 1, ExactlyOnce = 2 };

struct Delivery {
  Qos qos;
  bool retain;
};

// Leaf policy — must match deliveryFor() in topics.ts.
inline Delivery deliveryFor(const std::string& leaf) {
  if (leaf == "command") return { Qos::ExactlyOnce, false };
  if (leaf == "status")  return { Qos::AtLeastOnce, true };
  // event, ack, telemetry, location, presence, audio, and metric leaves
  return { Qos::AtLeastOnce, false };
}

inline bool isValidSegment(const std::string& s) {
  if (s.empty() || s.size() > 128) return false;
  for (char c : s) {
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' || c == '-';
    if (!ok) return false;
  }
  return true;
}

inline std::string buildTopic(const std::string& kennelId, const std::string& deviceType,
                              const std::string& deviceId, const std::string& leaf) {
  return "kennel/" + kennelId + "/" + deviceType + "/" + deviceId + "/" + leaf;
}

struct TopicParts {
  std::string kennelId, deviceType, deviceId, leaf;
  bool valid = false;
};

inline TopicParts parseTopic(const std::string& topic) {
  TopicParts p;
  std::array<std::string, 5> seg;
  size_t idx = 0, start = 0;
  for (size_t i = 0; i <= topic.size(); ++i) {
    // cppcheck-suppress containerOutOfBounds  ; short-circuits at i == size()
    if (i == topic.size() || topic[i] == '/') {
      if (idx >= 5) { return p; }  // too many segments
      seg[idx++] = topic.substr(start, i - start);
      start = i + 1;
    }
  }
  if (idx != 5 || seg[0] != "kennel") return p;
  static const std::array<const char*, 8> kinds = {
    "feeder", "water", "door", "sensor", "gps", "camera", "scale", "hub"
  };
  bool knownKind = false;
  for (auto k : kinds) if (seg[2] == k) knownKind = true;
  if (!knownKind) return p;
  if (!isValidSegment(seg[1]) || !isValidSegment(seg[3]) || !isValidSegment(seg[4])) return p;
  p.kennelId = seg[1]; p.deviceType = seg[2]; p.deviceId = seg[3]; p.leaf = seg[4];
  p.valid = true;
  return p;
}

inline bool isLegacyTopic(const std::string& topic) {
  return topic.rfind("dogs/", 0) == 0 ||
         topic.rfind("devices/", 0) == 0 ||
         topic.find("/telemetry/") != std::string::npos;
}

// MQTT wildcard match ('+' one level, '#' rest).
inline bool topicMatches(const std::string& filter, const std::string& topic) {
  if (filter == topic) return true;
  size_t fi = 0, ti = 0;
  auto next = [](const std::string& s, size_t& i) {
    size_t start = i;
    while (i < s.size() && s[i] != '/') ++i;
    std::string seg = s.substr(start, i - start);
    if (i < s.size()) ++i;
    return seg;
  };
  while (fi < filter.size()) {
    std::string f = next(filter, fi);
    if (f == "#") return true;
    if (ti >= topic.size()) return false;
    std::string t = next(topic, ti);
    if (f == "+") continue;
    if (f != t) return false;
  }
  return ti >= topic.size();
}

}  // namespace spd
