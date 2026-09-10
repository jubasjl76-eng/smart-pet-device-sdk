// SmartPetDevice.h — umbrella include for the Smart Pet firmware SDK.
//
// A device sketch typically needs:
//   #include <SmartPetDevice.h>
//   spd::SmartPetDevice dev("feeder");
//   spd::FeederModule feeder(SERVO_PIN, TRIG_PIN, ECHO_PIN);
//
// See examples/ for feeder, door and scale sketches, and MIGRATION.md for porting
// a single-file firmware.
#pragma once

#include "spd_config.h"
#include "spd_topics.h"
#include "spd_schedule.h"
#include "spd_backoff.h"
#include "spd_offline_journal.h"
#include "spd_crash.h"
#include "spd_time.h"
#include "spd_wifi.h"
#include "spd_mqtt.h"
#include "spd_ota.h"
#include "spd_device.h"

#include "modules/FeederModule.h"
#include "modules/WaterModule.h"
#include "modules/DoorModule.h"
#include "modules/ScaleModule.h"
#include "modules/PresenceScanner.h"
#include "modules/EnvSensorModule.h"
#include "modules/TwoWayAudioModule.h"
