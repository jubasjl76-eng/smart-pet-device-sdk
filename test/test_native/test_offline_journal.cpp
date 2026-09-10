#include <gtest/gtest.h>
#include <string>
#include "../../src/spd_offline_journal.h"

using namespace spd;

TEST(OfflineJournal, ignoresRecordsWhileClosed) {
  OfflineJournal j;
  EXPECT_FALSE(j.isOpen());
  j.record(100, "fed", 40);
  EXPECT_EQ(j.pendingCount(), 0u);
}

TEST(OfflineJournal, recordsWhileOpen) {
  OfflineJournal j;
  j.markOffline(1000, "power");
  ASSERT_TRUE(j.isOpen());
  j.record(1100, "fed", 40);
  j.record(1200, "fed", 40);
  j.record(1500, "door_open", 0);
  EXPECT_EQ(j.pendingCount(), 3u);
}

TEST(OfflineJournal, serializeRoundTripThenCloseReport) {
  OfflineJournal j;
  j.markOffline(1000, "power");
  j.record(1100, "fed", 40);
  j.record(1200, "fed", 40);
  j.record(1500, "door_open", 0);

  const std::string blob = j.serialize();
  OfflineJournal restored;
  ASSERT_TRUE(restored.deserialize(blob));
  EXPECT_TRUE(restored.isOpen());
  EXPECT_EQ(restored.pendingCount(), 3u);

  const auto rep = restored.closeOnline(2000);
  EXPECT_EQ(rep.wentOfflineAtS, 1000);
  EXPECT_EQ(rep.cameOnlineAtS, 2000);
  EXPECT_EQ(rep.cause, "power");
  ASSERT_EQ(rep.actions.size(), 3u);
  EXPECT_EQ(rep.actions[0].kind, "fed");
  EXPECT_FLOAT_EQ(rep.actions[0].amount, 40.0f);
  EXPECT_EQ(rep.actions[2].kind, "door_open");
  EXPECT_FALSE(restored.isOpen());
}
