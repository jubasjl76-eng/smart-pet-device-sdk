// DoorModule.h — pen / run door: servo latch or maglock relay, with a reed
// switch for true open/closed state and a local audit hook.
//
// Fail-safe: on power loss a maglock de-energises (opens); a servo latch holds
// its last position. Pick `MaglockActiveLow` to match your relay board.
#pragma once
#include <Arduino.h>
#include <ESP32Servo.h>
#include <functional>

namespace spd {

enum class DoorKind { ServoLatch, Maglock };

struct DoorState {
  bool locked = true;
  bool open = false;
  uint32_t lastChangeS = 0;
  String lastReason;
};

class DoorModule {
 public:
  // actuatorPin: servo signal or relay control. reedPin: LOW when door closed.
  DoorModule(int actuatorPin, int reedPin, DoorKind kind = DoorKind::ServoLatch,
             bool maglockActiveLow = true)
      : actuatorPin_(actuatorPin), reedPin_(reedPin), kind_(kind), activeLow_(maglockActiveLow) {}

  void begin() {
    pinMode(reedPin_, INPUT_PULLUP);
    if (kind_ == DoorKind::Maglock) {
      pinMode(actuatorPin_, OUTPUT);
      energise(true);  // locked = energised
    } else {
      servo_.attach(actuatorPin_);
      servo_.write(lockAngle_);
    }
    state_.locked = true;
  }

  // action: "lock" | "unlock" | "open" | "close" | "noop"
  bool apply(const String& action, const String& reason, uint32_t holdMs = 0) {
    if (action == "noop") return true;
    if (action == "lock" || action == "close") { setLocked(true, reason); return true; }
    if (action == "unlock") { setLocked(false, reason); return true; }
    if (action == "open") {
      setLocked(false, reason);
      if (holdMs) { pulseOpenMs_ = holdMs; pulseStartMs_ = millis(); }
      return true;
    }
    return false;
  }

  // Call from loop(): re-latches after a timed open, and edge-detects the reed.
  void poll(std::function<void(const DoorState&)> onChange = nullptr) {
    if (pulseOpenMs_ && millis() - pulseStartMs_ > pulseOpenMs_) {
      pulseOpenMs_ = 0;
      setLocked(true, "auto-relatch");
      if (onChange) onChange(state_);
    }
    bool physicallyOpen = digitalRead(reedPin_) == HIGH;
    if (physicallyOpen != state_.open) {
      state_.open = physicallyOpen;
      state_.lastChangeS = nowS();
      if (onChange) onChange(state_);
    }
  }

  const DoorState& state() const { return state_; }

 private:
  void setLocked(bool locked, const String& reason) {
    state_.locked = locked;
    state_.lastReason = reason;
    state_.lastChangeS = nowS();
    if (kind_ == DoorKind::Maglock) energise(locked);
    else servo_.write(locked ? lockAngle_ : unlockAngle_);
  }
  void energise(bool on) {
    bool level = activeLow_ ? !on : on;
    digitalWrite(actuatorPin_, level ? HIGH : LOW);
  }
  static uint32_t nowS() { return (uint32_t)time(nullptr); }

  Servo servo_;
  int actuatorPin_, reedPin_;
  DoorKind kind_;
  bool activeLow_;
  int lockAngle_ = 0, unlockAngle_ = 90;
  DoorState state_;
  uint32_t pulseOpenMs_ = 0, pulseStartMs_ = 0;
};

}  // namespace spd
