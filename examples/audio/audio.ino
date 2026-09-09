/**
 * Door station — two-way audio + a door strike relay, on smart-pet-device-sdk.
 *
 * "Talk to your dog": the pet-owner app and the camera-service exchange
 * signalling on kennel/{k}/{type}/{id}/audio; this device acts on:
 *   talk {state:start|end}  → open the mic, stream mu-law over UDP
 *   play {url}              → play a raw 16-bit mono PCM clip through the amp
 *   stop                    → close everything
 * The media transport itself is UDP PCM here (full WebRTC lives in the gateway).
 *
 * Wiring: INMP441 mic on I2S0 (BCLK 32 / WS 25 / DIN 33), MAX98357A amp on I2S1
 * (BCLK 27 / WS 26 / DOUT 14), door strike relay GPIO4, call button GPIO0.
 */
#include <SmartPetDevice.h>

constexpr int RELAY_PIN = 4;
constexpr int BUTTON_PIN = 0;

spd::SmartPetDevice   dev("door");
spd::DoorModule       door(RELAY_PIN, /*reedPin*/13, spd::DoorKind::Maglock);
spd::TwoWayAudioModule audio(32, 25, 33,   // mic  BCLK / WS / DIN
                             27, 26, 14);  // amp  BCLK / WS / DOUT

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  door.begin();
  audio.begin();
  dev.begin();

  dev.onCommand("door", [](JsonObjectConst p, const String&) {
    return door.apply(p["action"] | "unlock", p["reason"] | "console", p["holdMs"] | 0);
  });

  dev.onAudioSignal([](const String& kind, JsonObjectConst sig) {
    if (kind == "talk") {
      if (String((const char*)(sig["state"] | "")) == "start") {
        audio.startTalk(sig["host"] | "0.0.0.0", sig["port"] | 6000);
      } else {
        audio.stopTalk();
      }
    } else if (kind == "play") {
      audio.play(sig["url"] | "");
    } else if (kind == "stop") {
      audio.stopTalk();
    }
  });

  dev.onStatusFill([](JsonObject& s) {
    s["talking"] = audio.talking();
    s["doorLocked"] = door.state().locked;
  });
}

void loop() {
  dev.loop();
  door.poll();
  audio.pumpTalk();

  // Call button → let the console/app know someone is at the door.
  static int lastBtn = HIGH;
  int btn = digitalRead(BUTTON_PIN);
  if (lastBtn == HIGH && btn == LOW && dev.isOnline()) {
    dev.publishEvent("call", [](JsonObject& d) { d["source"] = "button"; });
  }
  lastBtn = btn;

  delay(2);
}
