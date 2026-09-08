/**
 * Smart Pet Feeder — reference sketch on smart-pet-device-sdk.
 *
 * The SDK handles: Wi-Fi provisioning (SoftAP portal, no hardcoded creds), NTP,
 * MQTT on kennel/{kennelId}/feeder/{deviceId}/*, LWT, OTA, command/ack, schedule
 * caching, and the offline journal. This file is just the feeder behaviour.
 *
 * First boot: connect to the "smartpet-<mac>" Wi-Fi AP and fill in the form.
 */
#include <SmartPetDevice.h>

constexpr int SERVO_PIN = 4;
constexpr int TRIG_PIN  = 5;
constexpr int ECHO_PIN  = 18;
constexpr int LED_PIN   = 2;

spd::SmartPetDevice dev("feeder");
spd::FeederModule   feeder(SERVO_PIN, TRIG_PIN, ECHO_PIN);

static float readLevel() { return feeder.foodLevelPct(); }

void setup() {
  pinMode(LED_PIN, OUTPUT);
  feeder.begin();
  feeder.setCalibration(20.0f);          // grams/second — calibrate for your auger
  dev.begin();

  // Manual "Feed Now" from the backend.
  dev.onCommand("feed", [](JsonObjectConst p, const String&) {
    float grams = p["amount"] | 40.0f;
    digitalWrite(LED_PIN, HIGH);
    bool ok = feeder.dispense(grams);
    digitalWrite(LED_PIN, LOW);
    if (ok) dev.reportAction("fed", grams);
    return ok;
  });

  // Scheduled feeds (schedule_set is stored + cached by the SDK; this fires it).
  dev.onScheduledAction([](float grams) {
    feeder.dispense(grams);
    dev.reportAction("fed", grams);
  });

  // Extra fields on every retained status message.
  dev.onStatusFill([](JsonObject& s) {
    float lvl = readLevel();
    s["foodLevel"] = lvl;
    s["isFoodLow"] = lvl < 20;
    s["jammed"]    = feeder.jammed();
  });

  dev.identifyFn_ = [](int secs) {
    for (int i = 0; i < secs * 2; ++i) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(250); }
  };
}

void loop() {
  dev.loop();

  static uint32_t lastLevelPub = 0;
  if (dev.isOnline() && millis() - lastLevelPub > 60000) {
    lastLevelPub = millis();
    dev.publishMetric("level", readLevel(), "percent");
  }
}
