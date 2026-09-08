/**
 * Smart Scale Bowl — HX711 load cell under a feeder/water bowl.
 *
 * Publishes:
 *   kennel/{k}/scale/{id}/weight   grams on the platform (for body-weight, when stable)
 *   kennel/{k}/scale/{id}/event    {event:"consumed", data:{grams}} after a meal
 * Pair with a PresenceScanner (or the feeder's) so the backend attributes it to a dog.
 */
#include <SmartPetDevice.h>

constexpr int HX_DOUT = 16;
constexpr int HX_SCK  = 17;
constexpr float SCALE_FACTOR = 420.0f;  // calibrate: raw units per gram

spd::SmartPetDevice dev("scale");
spd::ScaleModule    scale(HX_DOUT, HX_SCK);

void setup() {
  scale.begin(SCALE_FACTOR);
  dev.begin();

  // Backend marks "meal served" so we can measure what was actually eaten.
  dev.onCommand("mark_meal", [](JsonObjectConst, const String&) {
    scale.markBefore();
    return true;
  });
  dev.onCommand("measure_consumed", [](JsonObjectConst, const String&) {
    float eaten = scale.consumedSinceMark();
    dev.publishEvent("consumed", [eaten](JsonObject& d) { d["grams"] = eaten; });
    return true;
  });
  dev.onCommand("tare", [](JsonObjectConst, const String&) { scale.tare(); return true; });

  dev.onStatusFill([](JsonObject& s) { s["grams"] = scale.lastGrams(); });
}

void loop() {
  dev.loop();

  static uint32_t last = 0;
  if (dev.isOnline() && millis() - last > 2000) {
    last = millis();
    float bw = scale.stableWeight();          // NAN until a steady load is present
    if (!isnan(bw)) dev.publishMetric("weight", bw, "g");
  }
}
