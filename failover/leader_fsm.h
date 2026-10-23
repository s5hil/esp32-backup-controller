#pragma once
#include <cstdint>

enum class Role {
  STANDBY,
  ACTIVE
};

class LeaderFsm {
  public:
    LeaderFsm(uint32_t leaseMs, uint32_t bootWaitMs, uint32_t nowMs);

    void onWire(uint32_t nowMs); // heartbeat edge see non wire
    void onRadio(uint32_t nowMs); // state message received over esp now
    Role update(uint32_t nowMs);
    Role role() const { return role_; }

    bool wireSilent(uint32_t nowMs) const;
    bool radioSilent(uint32_t nowMs) const;

  private:
    struct Channel {
      uint32_t lastMs;
      uint32_t timeoutMs; // boot wait until first heard, then lease
    };

    static bool silent(const Channel& c, uint32_t nowMs) {
      return (nowMs - c.lastMs) > c.timeoutMs;
    }

    uint32_t leaseMs_;
    Channel wire_;
    Channel radio_;
    Role role_;
};