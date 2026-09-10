# smart-pet-device-sdk

[![CI](https://github.com/jubasjl76-eng/smart-pet-device-sdk/actions/workflows/ci.yml/badge.svg)](https://github.com/jubasjl76-eng/smart-pet-device-sdk/actions/workflows/ci.yml)

Shared ESP32 firmware base for Smart Pet devices. Firmware becomes a thin sketch:
pick a module, register a couple of callbacks, call `dev.loop()`.

The SDK owns the plumbing every device was re-implementing (with the same bugs —
hardcoded Wi-Fi creds, no NTP, no OTA, HTTP polling):

| Concern | SDK |
|---|---|
| Wi-Fi | STA from NVS; **SoftAP captive portal** provisioning if unset/failing — nothing hardcoded (`spd_wifi.h`) |
| Time | NTP + POSIX TZ; last-known-good epoch persisted to NVS + restored on boot; schedules refuse to fire until an NTP sync lands *this session* (`timeTrusted()`); first-sync clock offset reported as `clockOffsetS` (`spd_time.h`) |
| Transport | MQTT on `kennel/{kennelId}/{deviceType}/{deviceId}/{leaf}`, LWT, QoS/retain per the contract, exponential-backoff reconnect (`spd_mqtt.h`, `spd_topics.h`) |
| Commands | `command` → typed handler → auto `ack`; built-ins: `restart`, `ota`, `identify`, `schedule_set`, `set_interval` (`spd_device.h`) |
| Scheduling | schedule cache persisted to NVS; fires on RTC time even with no cloud (`spd_schedule.h`) |
| Outage | **offline journal** — actions recorded while dark, replayed as `offline_recovered` on reconnect (`spd_offline_journal.h`) |
| Updates | ArduinoOTA (LAN) + HTTP pull on the `ota` command (`spd_ota.h`) |
| Config | all identity/creds in NVS as one CRC-checked entry (atomic write; a power cut can't half-update it), legacy per-key format auto-migrated, corrupt config falls back to factory defaults (`spd_config.h`, `spd_config_codec.h`) |
| Power loss | HW brown-out detector resets before flash corruption; brown-out resets counted in RTC_NOINIT memory and reported as `brownouts` in status (`spd_brownout.h`) |
| Kill switch | subscribes retained `kennel/{k}/_control` — in `safeMode` the device stops all actuation (scheduled + app command handlers) but keeps reporting; `safeModeActive()` for the sketch (`spd_device.h`) |

Matches the TypeScript contract in **`smart-pet-mqtt`** exactly (`spd_topics.h` mirrors `topics.ts`).

## Modules

| Module | Hardware | Notes |
|---|---|---|
| `FeederModule` | SG90 servo auger + HC-SR04 | grams via calibrated grams/sec; jam flag |
| `WaterModule` | pump + HC-SR04 + TDS + NTC | level %, TDS ppm, °C; `start()/stop()` or timed `dispense()` |
| `DoorModule` | servo latch **or** maglock relay + reed | `lock/unlock/open/close`, timed auto-relatch, fail-safe notes, audit reason |
| `ScaleModule` | HX711 load cell | bowl "eaten vs dispensed"; stable body-weight gate |
| `PresenceScanner` | ESP32 BLE | strongest `spd-tag-*` advertiser → which dog is here (multi-dog id, wrong-pen) |
| `EnvSensorModule` | any (lambdas) | temp/humidity/airquality metrics + door/motion edges |
| `TwoWayAudioModule` | INMP441 mic + MAX98357A amp (I2S) | "talk to your dog": mu-law mic stream over UDP, raw-PCM HTTP clip player; signalling on the `audio` leaf |

## Quick start

```cpp
#include <SmartPetDevice.h>
spd::SmartPetDevice dev("feeder");
spd::FeederModule   feeder(4, 5, 18);

void setup() {
  feeder.begin();
  dev.begin();
  dev.onCommand("feed", [](JsonObjectConst p, const String&) {
    return feeder.dispense(p["amount"] | 40.0f);
  });
  dev.onScheduledAction([](float g){ feeder.dispense(g); dev.reportAction("fed", g); });
  dev.onStatusFill([](JsonObject& s){ s["foodLevel"] = feeder.foodLevelPct(); });
}
void loop() { dev.loop(); }
```

Full sketches: [`examples/feeder`](examples/feeder), [`examples/door`](examples/door), [`examples/scale`](examples/scale), [`examples/audio`](examples/audio) (door station + two-way audio).

## Build & test

```bash
# freestanding logic (topics, schedule, backoff, offline journal) — no toolchain needed
./test/run_native.sh

# device build (needs PlatformIO)
pio run -e feeder
pio run -e door
pio run -e scale
```

`test/run_native.sh` compiles `test/test_native/test_main.cpp` with `c++ -std=c++17`
and runs 50+ assertions against the pure headers.

## Layout

```
src/
  SmartPetDevice.h        umbrella include
  spd_topics.h  ┐ freestanding C++17 — host-tested, mirror smart-pet-mqtt
  spd_schedule.h│
  spd_backoff.h │
  spd_offline_journal.h ┘
  spd_config.h  ┐ Arduino (ESP32)
  spd_wifi.h    │
  spd_time.h    │
  spd_mqtt.h    │
  spd_ota.h     │
  spd_device.h  ┘ orchestrator
  modules/*.h
examples/{feeder,door,scale}/*.ino
test/test_native/test_main.cpp
```

## Status

The freestanding headers are covered by host tests (`./test/run_native.sh`).
CI (`.github/workflows/ci.yml`) also compiles the three examples for `esp32dev`
(`pio run -e feeder|door|scale`) on every push and PR, against a pinned ESP32
Arduino core 3.x. Not yet flashed on a physical board — `MIGRATION.md` lists
what still needs real hardware (servo throw, HX711 calibration, NTP, captive
portal, OTA).
