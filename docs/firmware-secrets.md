# Firmware secret model

Hardening Phase 12 (A8). How secrets are handled on-device. The short version:
**nothing sensitive is baked into the firmware image.**

## What is in the image

| Item | Where | Sensitive? |
|---|---|---|
| OTA server URL | build flag (`-D SPD_OTA_URL=...`) | no |
| Firmware **signing public key** | compiled in (`spd_ota.h`) | no — public half only |
| Device type, topic scheme | compiled in (`spd_topics.h`) | no |
| Default MQTT host/port | build flag / provisioning default | no |

## What is provisioned onto the device (not in the image)

| Item | Mechanism | Storage |
|---|---|---|
| WiFi SSID + password | SoftAP / BLE provisioning portal on first boot | NVS |
| MQTT per-device username + password | minted by the backend on `POST /api/devices/claim`, entered via the portal or a QR | NVS |
| Kennel id, device id | set at claim | NVS |

NVS is protected at rest by **Flash Encryption** (rolled out in Phase 19). Until
then, treat a physically recovered device as able to leak its own MQTT
credentials — which only scope it to its own topic subtree (broker ACL), and can
be rotated from the fleet screen.

## What stays server-side, never on the device

- The firmware **signing private key** — lives in CI (AWS Secrets Manager /
  a GitHub Environment secret). The device verifies an OTA image against the
  compiled-in public key before applying it.
- The backend's own MQTT credentials, DB, provider keys.

## Provisioning security (Phase 18/19)

First-setup currently sends WiFi credentials over the SoftAP/BLE link in the
clear. Phase 18/19 moves this to ESP-IDF `protocomm` with a proof-of-possession
(PoP) shown as a QR on the packaging, so the provisioning channel is encrypted
and authenticated.

## Rotation

- **MQTT device credentials** — re-mint from the fleet screen; the device picks
  up the new pair on its next claim/OTA cycle. Scheduled rotation is a Phase 21
  runbook.
- **Signing key** — a keyset with a `kid`; a new key is added, images are signed
  with it, the old public key stays compiled in for a grace window, then a
  firmware release drops it. Revocation = ship a build without the compromised
  public key.
