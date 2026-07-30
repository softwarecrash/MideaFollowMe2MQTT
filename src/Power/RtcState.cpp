#include "Power/RtcState.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>

namespace {
constexpr uint32_t kMagic = 0x50535254;  // "PSRT"
constexpr uint16_t kVersion = 1;
static_assert(sizeof(RtcState) <= 512, "RTC state exceeds ESP8266 RTC user memory");
static_assert(sizeof(RtcState) % 4 == 0, "RTC state size must be word-aligned");
}

uint32_t RtcStorage::crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  while (length--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
  }
  return ~crc;
}

void RtcStorage::defaults(RtcState &s) {
  s = RtcState{};
  s.magic = kMagic;
  s.version = kVersion;
  s.size = sizeof(s);
  s.lastSendReason = LastSendReason::RtcInvalid;
}

bool RtcStorage::load(RtcState &s) {
  if (!ESP.rtcUserMemoryRead(0, reinterpret_cast<uint32_t *>(&s), sizeof(s))) {
    defaults(s);
    return false;
  }
  const uint32_t stored = s.crc;
  s.crc = 0;
  const bool valid = s.magic == kMagic && s.version == kVersion &&
      s.size == sizeof(s) && stored == crc32(reinterpret_cast<uint8_t *>(&s), sizeof(s));
  if (!valid) defaults(s);
  else s.crc = stored;
  return valid;
}

bool RtcStorage::save(RtcState &s) {
  s.magic = kMagic;
  s.version = kVersion;
  s.size = sizeof(s);
  s.crc = 0;
  s.crc = crc32(reinterpret_cast<uint8_t *>(&s), sizeof(s));
  return ESP.rtcUserMemoryWrite(0, reinterpret_cast<uint32_t *>(&s), sizeof(s));
}
