#pragma once

#include <stdint.h>
#include <stddef.h>

#include "Climate/PortaSplitState.h"

enum class LastSendReason : uint8_t { None, Changed, Forced, RtcInvalid, SafetyResend, FollowMe };

struct RtcState {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t wakeCount;
  uint32_t lastCommandId;
  uint32_t followMeWakeCount;
  uint8_t wifiChannel;
  uint8_t wifiBssid[6];
  bool wifiBssidValid;
  PortaSplitState lastClimateState;
  float lastRoomTemperature;
  LastSendReason lastSendReason;
  uint32_t crc;
};

namespace RtcStorage {
bool load(RtcState &state);
bool save(RtcState &state);
void defaults(RtcState &state);
uint32_t crc32(const uint8_t *data, size_t length);
}  // namespace RtcStorage
