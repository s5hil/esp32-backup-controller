#include <gtest/gtest.h>
#include <cstdint>
#include "leader_fsm.h"

constexpr uint32_t LEASE = 50;
constexpr uint32_t BOOT = 550; // lease + 500 ms tiebreak margin

static void both(LeaderFsm& f, uint32_t t) {
  f.onWire(t);
  f.onRadio(t);
}

TEST(LeaderFsm, StartsInStandby) {
    LeaderFsm fsm(LEASE, BOOT, 0);
    EXPECT_EQ(fsm.role(), Role::STANDBY);
}

TEST(LeaderFsm, WaitsFullBootWaitBeforeClaiming) {
  LeaderFsm fsm(LEASE, BOOT, 0);
  EXPECT_EQ(fsm.update(BOOT), Role::STANDBY);     // exactly at the limit: not yet
  EXPECT_EQ(fsm.update(BOOT + 1), Role::ACTIVE);
}

TEST(LeaderFsm, TakesOverWhenBothChannelsSilent) {
  LeaderFsm fsm(LEASE, BOOT, 0);
  both(fsm, 100);
  EXPECT_EQ(fsm.update(100 + LEASE), Role::STANDBY);
  EXPECT_EQ(fsm.update(100 + LEASE + 1), Role::ACTIVE);
}

TEST(LeaderFsm, WireLossAloneNeverTakesOver) {
  LeaderFsm fsm(LEASE, BOOT, 0);
  both(fsm, 0);
  for (uint32_t t = 20; t <= 10000; t += 20) {   // radio keeps arriving, wire never does
    fsm.onRadio(t);
    ASSERT_EQ(fsm.update(t + 5), Role::STANDBY) << "at t=" << t;
  }
  EXPECT_TRUE(fsm.wireSilent(10005));
  EXPECT_FALSE(fsm.radioSilent(10005));
}

TEST(LeaderFsm, RadioLossAloneNeverTakesOver) {
  LeaderFsm fsm(LEASE, BOOT, 0);
  both(fsm, 0);
  for (uint32_t t = 10; t <= 10000; t += 10) {   // wire keeps toggling, radio never arrives
    fsm.onWire(t);
    ASSERT_EQ(fsm.update(t + 5), Role::STANDBY) << "at t=" << t;
  }
  EXPECT_TRUE(fsm.radioSilent(10005));
}

TEST(LeaderFsm, TakeoverWaitsForTheLaterChannel) {
  LeaderFsm fsm(LEASE, BOOT, 0);
  fsm.onWire(100);
  fsm.onRadio(130);
  EXPECT_EQ(fsm.update(151), Role::STANDBY);   // wire silent, radio only 21 ms
  EXPECT_EQ(fsm.update(181), Role::ACTIVE);    // now both silent
}

TEST(LeaderFsm, ActiveStaysActive) {
  LeaderFsm fsm(LEASE, BOOT, 0);
  fsm.update(BOOT + 1);
  both(fsm, BOOT + 10);
  EXPECT_EQ(fsm.update(BOOT + 20), Role::ACTIVE);
}

TEST(LeaderFsm, SurvivesMillisWraparound) {
  const uint32_t nearMax = UINT32_MAX - 20;
  LeaderFsm fsm(LEASE, BOOT, 0);
  both(fsm, nearMax);
  EXPECT_EQ(fsm.update(nearMax + 10), Role::STANDBY);
  EXPECT_EQ(fsm.update(nearMax + 30), Role::STANDBY);
  EXPECT_EQ(fsm.update(nearMax + LEASE + 1), Role::ACTIVE);
}