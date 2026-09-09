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

## Open items before a device CI build

- **CI matrix — done.** `.github/workflows/ci.yml`: `./test/run_native.sh`
  plus `pio run -e feeder|door|scale` on `ubuntu-latest`, PlatformIO cached.
  `platformio.ini` now sets `src_dir = examples` with a per-env
  `build_src_filter`, so `pio run -e door` builds `examples/door` (it was
  always building the feeder before).
- **`ESP32Servo` — resolved.** `FeederModule` / `DoorModule` include
  `<ESP32Servo.h>` and use class `Servo`; `madhephaestus/ESP32Servo @ ^3.0.5`
  is now in `lib_deps` and `library.json`.
- **HX711 — confirmed.** `bogde/HX711` ^0.7 is `begin(byte dout, byte sck,
  byte gain = 128)`, so `ScaleModule`'s `hx_.begin(dout_, sck_)` is correct.
- **BLE core 2.x vs 3.x — mostly resolved.** `platform` is pinned to
  `espressif32 @ ^6.9.0` (core 3.x), which is what `PresenceScanner` was
  written against. `BLEScan::start()` returning value (2.x) vs pointer (3.x) is
  now handled by `spd::detail::asScanResults`. The `door` example pulls
  `PresenceScanner`, so the `pio run -e door` job exercises this path. If a
  future core bump changes `BLEScanResults::getDevice()` (value vs pointer),
  adjust it in that one spot.

## Still needs real hardware

- A flash + run on an `esp32dev` board for each module (servo throw, HX711
  calibration factor, NTP sync, captive-portal provisioning, OTA pull).
- `pio test -e native` via PlatformIO is wired but `./test/run_native.sh` is the
  canonical fast path and what CI runs.
