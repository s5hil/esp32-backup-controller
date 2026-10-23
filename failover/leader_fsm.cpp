#include "leader_fsm.h"

LeaderFsm::LeaderFsm(uint32_t leaseMs, uint32_t bootWaitMs, uint32_t nowMs)
  : leaseMs_(leaseMs),
    wire_{nowMs, bootWaitMs},
    radio_{nowMs, bootWaitMs},
    role_(Role::STANDBY) {}

void LeaderFsm::onWire(uint32_t nowMs) {
  wire_.lastMs = nowMs;
  wire_.timeoutMs = leaseMs_;
}

void LeaderFsm::onRadio(uint32_t nowMs) {
  radio_.lastMs = nowMs;
  radio_.timeoutMs = leaseMs_;
}

bool LeaderFsm::wireSilent(uint32_t nowMs) const {
  return silent(wire_, nowMs);
}

bool LeaderFsm::radioSilent(uint32_t nowMs) const {
  return silent(radio_, nowMs);
}

Role LeaderFsm::update(uint32_t nowMs) {
  if (role_ == Role::STANDBY && wireSilent(nowMs) && radioSilent(nowMs)) {
    role_ = Role::ACTIVE;
  }

  return role_;
}