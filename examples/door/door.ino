/**
 * Smart Pen Door — servo latch (or maglock) with reed switch + BLE presence.
 *
 * Backend sends: kennel/{k}/door/{id}/command {command:"door", params:{action, reason, holdMs}}
 * Emergency mode unlocks every door via the same command.
 * The BLE scan tells the backend which dog is at the door → wrong-pen detection.
 */
#include <SmartPetDevice.h>

constexpr int ACTUATOR_PIN = 4;   // servo signal, or relay for a maglock
constexpr int REED_PIN     = 13;  // LOW when the door is closed

spd::SmartPetDevice dev("door");
spd::DoorModule     door(ACTUATOR_PIN, REED_PIN, spd::DoorKind::ServoLatch);
spd::PresenceScanner presence;

void setup() {
  door.begin();
  presence.begin("spd-tag-");
  dev.begin();

  dev.onCommand("door", [](JsonObjectConst p, const String&) {
    const char* action = p["action"] | "noop";
    const char* reason = p["reason"] | "";
    uint32_t holdMs = p["holdMs"] | 0;
    bool ok = door.apply(action, reason, holdMs);
    if (ok) dev.reportAction(strcmp(action, "unlock") == 0 || strcmp(action, "open") == 0
                             ? "door_open" : "door_closed");
    return ok;
  });

  dev.onStatusFill([](JsonObject& s) {
    s["locked"] = door.state().locked;
    s["open"]   = door.state().open;
    s["lastReason"] = door.state().lastReason;
  });
}

void loop() {
  dev.loop();

  door.poll([](const spd::DoorState& st) {
    dev.publishEvent(st.open ? "door_open" : "door_closed", [&st](JsonObject& d) {
      d["reason"] = st.lastReason;
    });
  });

  static uint32_t lastScan = 0;
  if (dev.isOnline() && millis() - lastScan > 5000) {
    lastScan = millis();
    presence.scan(2);
    dev.publishPresence(presence.strongestTag(), presence.strongestRssi());
  }
}
