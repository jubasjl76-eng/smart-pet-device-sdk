// spd_offline_journal.h — record what the device did while it could not reach the
// cloud, so it can (a) report the dark window on reconnect and (b) replay events.
// Freestanding C++17, host-tested. Serialises to a compact line format that fits
// NVS without pulling in a JSON lib on the device.
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace spd {

struct MissedAction {
  uint32_t atEpochS = 0;      // device wall-clock (RTC) when it happened
  std::string kind;           // "fed" | "dispensed" | "door_open" | ...
  float amount = 0;           // grams / seconds / 0
};

class OfflineJournal {
 public:
  void markOffline(uint32_t epochS, const std::string& cause) {
    if (open_) return;
    open_ = true;
    wentOfflineAtS_ = epochS;
    cause_ = cause;
    actions_.clear();
  }

  bool isOpen() const { return open_; }

  void record(uint32_t epochS, const std::string& kind, float amount = 0) {
    if (!open_) return;
    if (actions_.size() < kMaxActions) actions_.push_back({ epochS, kind, amount });
    else dropped_++;
  }

  // Close the window and hand back a payload the device publishes on
  // kennel/{k}/hub|{type}/{id}/event with event="offline_recovered".
  struct Report {
    uint32_t wentOfflineAtS = 0;
    uint32_t cameOnlineAtS = 0;
    std::string cause;
    std::vector<MissedAction> actions;
    uint32_t dropped = 0;
  };

  Report closeOnline(uint32_t epochS) {
    Report r{ wentOfflineAtS_, epochS, cause_, actions_, dropped_ };
    open_ = false;
    actions_.clear();
    dropped_ = 0;
    return r;
  }

  // Compact NVS form:  "<offS>|<cause>|<n>;<atS>,<kind>,<amt>;..."
  std::string serialize() const {
    std::string s = u32(wentOfflineAtS_) + "|" + cause_ + "|" + u32((uint32_t)actions_.size());
    for (const auto& a : actions_) {
      s += ";" + u32(a.atEpochS) + "," + a.kind + "," + f2(a.amount);
    }
    return s;
  }

  bool deserialize(const std::string& s) {
    actions_.clear();
    size_t p1 = s.find('|');
    size_t p2 = s.find('|', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos) return false;
    wentOfflineAtS_ = (uint32_t)std::stoul(s.substr(0, p1));
    cause_ = s.substr(p1 + 1, p2 - p1 - 1);
    open_ = true;
    size_t pos = s.find(';', p2);
    while (pos != std::string::npos) {
      size_t end = s.find(';', pos + 1);
      std::string rec = s.substr(pos + 1, (end == std::string::npos ? s.size() : end) - pos - 1);
      size_t c1 = rec.find(','), c2 = rec.find(',', c1 + 1);
      if (c1 != std::string::npos && c2 != std::string::npos) {
        MissedAction a;
        a.atEpochS = (uint32_t)std::stoul(rec.substr(0, c1));
        a.kind = rec.substr(c1 + 1, c2 - c1 - 1);
        a.amount = std::stof(rec.substr(c2 + 1));
        actions_.push_back(a);
      }
      pos = end;
    }
    return true;
  }

  size_t pendingCount() const { return actions_.size(); }

 private:
  static constexpr size_t kMaxActions = 64;
  static std::string u32(uint32_t v) { return std::to_string(v); }
  static std::string f2(float v) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return std::string(buf);
  }

  bool open_ = false;
  uint32_t wentOfflineAtS_ = 0;
  std::string cause_;
  std::vector<MissedAction> actions_;
  uint32_t dropped_ = 0;
};

}  // namespace spd
