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

- BLE include names differ across ESP32 Arduino core 2.x vs 3.x (`BLEDevice.h` path, `BLEScanResults*` vs value). `PresenceScanner` targets 3.x.
- `ESP32Servo` vs core `Servo` — `library.json` pulls `ESP32Servo`; confirm on the target core.
- HX711 constructor/`begin(dout, sck)` arg order matches `bogde/HX711` ^0.7.
- Add a GitHub Actions matrix (`pio run -e feeder|door|scale`, `pio test -e native`).
