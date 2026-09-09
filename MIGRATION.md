# Porting a single-file firmware onto the SDK

Reference port: `smart-feeder` (`firmware/src/smart-feeder.cpp` → `firmware/src/main.cpp`
using the SDK; the old file is kept as `smart-feeder.legacy.cpp`).

## Steps

1. **Add the dependency.** In `platformio.ini`:
   ```ini
   lib_deps =
       symlink://../../smart-pet-device-sdk      ; or a git URL / registry once published
       bblanchon/ArduinoJson @ ^7
       knolleary/PubSubClient @ ^2.8
   ```

2. **Delete the plumbing.** Remove from the old firmware:
   - `WIFI_SSID` / `WIFI_PASSWORD` / `API_KEY` constants and all `WiFi.begin` code
   - `HTTPClient`, `POST /api/status`, `GET /api/schedule` polling
   - the hand-rolled `schedule[]` + `hour()/minute()` check (no NTP)
   - the local Express API assumptions (`:3002`)

3. **Keep the hardware code** — servo, ultrasonic, LED — or move it into the
   matching module (`FeederModule` already has servo + HC-SR04 + jam flag).

4. **Wire it up** (see `examples/feeder/feeder.ino`):
   ```cpp
   spd::SmartPetDevice dev("feeder");
   spd::FeederModule feeder(SERVO_PIN, TRIG_PIN, ECHO_PIN);
   dev.onCommand("feed", ...);
   dev.onScheduledAction(...);
   dev.onStatusFill(...);
   ```

5. **Provision once.** Flash, join the `smartpet-<mac>` AP, enter Wi-Fi + kennelId
   + deviceId + MQTT host/creds. Values persist in NVS; no rebuild to change them.

## Behaviour changes (intended)

| Before | After |
|---|---|
| HTTP poll `:3002` every 30 s | MQTT: retained `status`, `event` on action, `command` sub |
| `POST /api/feed` logged only; firmware never got it | `command {command:"feed"}` → dispense → `ack` |
| clock-based feeding unreliable (no NTP) | NTP + TZ; schedule fires on real wall-clock, cached in NVS for outages |
| Wi-Fi outage = missed feeds, silent | offline journal → `offline_recovered` event with what was missed |
| secrets in source | NVS, set via captive portal |
| no OTA | ArduinoOTA + `ota` command |

## Per-repo status

| Repo | Action |
|---|---|
| `smart-feeder` | ported (reference); legacy kept alongside |
| `smart-water-dispenser` | `WaterModule` ready; sketch = feeder pattern with `dispense`/`start`/`stop` |
| `gps-dog-collar` | move `dogs/{id}/…` → `kennel/{k}/gps/{id}/…`; use `dev("gps")`, publish `location`, keep adaptive intervals; `set_interval` built-in |
| `pet-iot-sensors-service` devices | `EnvSensorModule` with DHT/SHT readers; publishes `temperature`/`humidity`/`airquality` leaves |
| two-way-audio device | `TwoWayAudioModule` (I2S mic + amp); `dev.onAudioSignal()` on the `audio` leaf; `examples/audio` = door station. mu-law over UDP uplink + raw-PCM HTTP clip downlink. Full WebRTC stays in `pet-iot-camera-service` / the gateway. |

`pio-ci` now takes `example-dir: examples`, so each env builds its own sketch
(`door`/`scale`/`audio`), not always `examples/feeder`. `[esp32_base]` uses
`min_spiffs.csv` — the `door` sketch (BLE + WiFi + MQTT) overflows the default
partition.

## Open items before a device CI build — all resolved; CI is green

- **CI matrix — done.** `.github/workflows/ci.yml` calls
  `smart-pet-ci/pio-ci.yml` with `example-dir: examples`: `./test/run_native.sh`
  plus `pio run -e feeder|door|scale|audio`, each building its own sketch via
  `PLATFORMIO_SRC_DIR`, PlatformIO cached. Local default is `examples/feeder`;
  for the others set `PLATFORMIO_SRC_DIR=examples/<env>`.
- **C++ standard — fixed.** `platform = espressif32 @ ^6.9.0` resolves to Arduino
  core 2.x (GCC 8.4), which defaults to gnu++11/14 and rejected the SDK headers'
  C++17 aggregate initialisers. `[esp32_base]` now `build_unflags` the old
  standard and adds `-std=gnu++17`.
- **`ESP32Servo` — resolved.** `madhephaestus/ESP32Servo @ ^3.0.5` added to
  `lib_deps` and `library.json` (`FeederModule` / `DoorModule` include
  `<ESP32Servo.h>`).
- **HX711 — confirmed.** `bogde/HX711` ^0.7 is `begin(byte dout, byte sck,
  byte gain = 128)`, so `ScaleModule`'s `hx_.begin(dout_, sck_)` is correct.
- **BLE core 2.x vs 3.x — handled.** Core 2.x's `BLEScan::start()` returns
  `BLEScanResults` by value, 3.x returns a pointer. `PresenceScanner::scan()`
  binds the result to `auto` and passes it through `spd::detail::scanRef`
  (overloaded for value and pointer). The `door` example pulls `PresenceScanner`,
  so `pio run -e door` exercises it.

## Still needs real hardware

- A flash + run on an `esp32dev` board for each module (servo throw, HX711
  calibration factor, NTP sync, captive-portal provisioning, OTA pull).
- `pio test -e native` via PlatformIO is wired but `./test/run_native.sh` is the
  canonical fast path and what CI runs.
