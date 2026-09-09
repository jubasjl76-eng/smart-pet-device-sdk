// Host unit tests for the freestanding parts of the SDK.
//   ./test/run_native.sh      (clang++/g++ -std=c++17, no Arduino, no PlatformIO)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "../../src/spd_topics.h"
#include "../../src/spd_schedule.h"
#include "../../src/spd_backoff.h"
#include "../../src/spd_offline_journal.h"
#include "../../src/spd_ulaw.h"

static int g_fails = 0;
static int g_checks = 0;
#define CHECK(cond) do { \
  ++g_checks; \
  if (!(cond)) { ++g_fails; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)
#define SECTION(name) std::printf("• %s\n", name)

using namespace spd;

static void test_topics() {
  SECTION("topics");
  CHECK(buildTopic("home", "feeder", "feeder-01", "command") == "kennel/home/feeder/feeder-01/command");

  auto p = parseTopic("kennel/home/feeder/feeder-01/status");
  CHECK(p.valid);
  CHECK(p.kennelId == "home" && p.deviceType == "feeder" && p.deviceId == "feeder-01" && p.leaf == "status");

  CHECK(!parseTopic("kennel/home/feeder/feeder-01").valid);          // too short
  CHECK(!parseTopic("kennel/home/toaster/x/status").valid);           // unknown type
  CHECK(!parseTopic("dogs/collar-01/location").valid);
  CHECK(!parseTopic("kennel/home/feeder/bad id/status").valid);       // bad segment

  CHECK(isLegacyTopic("dogs/collar-01/location"));
  CHECK(!isLegacyTopic("kennel/home/gps/collar-01/location"));

  CHECK(deliveryFor("command").qos == Qos::ExactlyOnce);
  CHECK(deliveryFor("command").retain == false);
  CHECK(deliveryFor("status").qos == Qos::AtLeastOnce);
  CHECK(deliveryFor("status").retain == true);
  CHECK(deliveryFor("temperature").retain == false);

  CHECK(topicMatches("kennel/+/feeder/+/status", "kennel/home/feeder/f1/status"));
  CHECK(!topicMatches("kennel/+/feeder/+/status", "kennel/home/water/w1/status"));
  CHECK(topicMatches("kennel/home/#", "kennel/home/gps/c1/location"));
  CHECK(!topicMatches("kennel/home/#", "kennel/away/gps/c1/location"));
}

static void test_schedule() {
  SECTION("schedule");
  CHECK(parseHhMm("08:00") == 480);
  CHECK(parseHhMm("23:59") == 23 * 60 + 59);
  CHECK(parseHhMm("24:00") == -1);
  CHECK(parseHhMm("8:00") == -1);

  ScheduleCache sc;
  sc.set({
    { "b", 20, 0, 30, true },
    { "a", 8, 0, 40, true },
    { "c", 12, 30, 25, false },  // disabled
  });
  CHECK(sc.size() == 3);
  CHECK(sc.entries()[0].id == "a");  // sorted by time

  // 08:00 day 100 → entry "a" due; mark fired; not due again same slot
  const ScheduleEntry* d = sc.due(8 * 60, 100);
  CHECK(d != nullptr && d->id == "a");
  sc.markFired(*d, 100);
  CHECK(sc.due(8 * 60, 100) == nullptr);
  // next day, same slot → due again
  CHECK(sc.due(8 * 60, 101) != nullptr);
  // disabled entry never fires
  CHECK(sc.due(12 * 60 + 30, 101) == nullptr);
  // window tolerance: 3 minutes late still counts
  CHECK(sc.due(20 * 60 + 3, 101) != nullptr);
  // 6 minutes late is outside the default 5-min window
  CHECK(sc.due(20 * 60 + 6, 102) == nullptr);

  CHECK(sc.minutesUntilNext(7 * 60 + 0) == 60);        // to 08:00
  CHECK(sc.minutesUntilNext(21 * 60) == (8 * 60) + (3 * 60));  // wraps to 08:00 next day
}

static void test_backoff() {
  SECTION("backoff");
  Backoff b(1000, 60000);
  CHECK(b.peekCeiling() == 1000);
  b.next(0.5); CHECK(b.peekCeiling() == 2000);
  b.next(0.5); CHECK(b.peekCeiling() == 4000);
  for (int i = 0; i < 20; ++i) b.next(0.9);
  CHECK(b.peekCeiling() == 60000);   // capped
  // full jitter stays within [base/4, ceiling]
  Backoff c(1000, 60000);
  uint32_t d0 = c.next(0.0);
  CHECK(d0 == 250);                   // floor = base/4
  uint32_t d1 = c.next(1.0);
  CHECK(d1 <= 2000 && d1 >= 250);
  c.reset();
  CHECK(c.attempt() == 0);
}

static void test_offline_journal() {
  SECTION("offline journal");
  OfflineJournal j;
  CHECK(!j.isOpen());
  j.record(100, "fed", 40);           // ignored while closed
  CHECK(j.pendingCount() == 0);

  j.markOffline(1000, "power");
  CHECK(j.isOpen());
  j.record(1100, "fed", 40);
  j.record(1200, "fed", 40);
  j.record(1500, "door_open", 0);
  CHECK(j.pendingCount() == 3);

  std::string blob = j.serialize();
  OfflineJournal restored;
  CHECK(restored.deserialize(blob));
  CHECK(restored.isOpen());
  CHECK(restored.pendingCount() == 3);

  auto rep = restored.closeOnline(2000);
  CHECK(rep.wentOfflineAtS == 1000);
  CHECK(rep.cameOnlineAtS == 2000);
  CHECK(rep.cause == "power");
  CHECK(rep.actions.size() == 3);
  CHECK(rep.actions[0].kind == "fed" && rep.actions[0].amount == 40.0f);
  CHECK(rep.actions[2].kind == "door_open");
  CHECK(!restored.isOpen());
}

static void test_ulaw() {
  SECTION("ulaw");
  // round-trip stays close for a range of amplitudes (mu-law is lossy but monotonic)
  for (int s = -32000; s <= 32000; s += 250) {
    int16_t back = ulawDecode(ulawEncode((int16_t)s));
    int err = std::abs((int)back - s);
    CHECK(err <= (std::abs(s) / 8) + 260);   // ~mu-law step size
  }
  CHECK(ulawEncode(0) == 0xFF);              // silence
  CHECK(std::abs((int)ulawDecode(ulawEncode(0))) <= 8);
  // sign preserved
  CHECK(ulawDecode(ulawEncode(-12345)) < 0);
  CHECK(ulawDecode(ulawEncode(12345)) > 0);
  // block helpers
  int16_t pcm[4] = {0, 1000, -1000, 30000};
  uint8_t u[4];
  int16_t out[4];
  ulawEncodeBlock(pcm, u, 4);
  ulawDecodeBlock(u, out, 4);
  CHECK(out[0] == ulawDecode(u[0]) && out[3] > 20000);
}

int main() {
  std::printf("smart-pet-device-sdk native tests\n");
  test_topics();
  test_schedule();
  test_backoff();
  test_offline_journal();
  test_ulaw();
  std::printf("\n%d checks, %d failures\n", g_checks, g_fails);
  return g_fails == 0 ? 0 : 1;
}
