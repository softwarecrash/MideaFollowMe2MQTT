#pragma once

#include <Arduino.h>

#ifndef PORTASPLIT_VERSION
#define PORTASPLIT_VERSION "0.1.0-dev"
#endif

namespace Config {
constexpr uint16_t kSettingsVersion = 3;
constexpr uint32_t kSettingsMagic = 0x5053324D;  // "PS2M"
#ifndef PORTASPLIT_DEFAULT_IR_PIN
#define PORTASPLIT_DEFAULT_IR_PIN 4
#endif
constexpr uint8_t kDefaultIrPin = PORTASPLIT_DEFAULT_IR_PIN;
constexpr uint32_t kMqttRetryMs = 5000;
constexpr uint32_t kWifiRetryMs = 10000;
constexpr uint32_t kMinIrGapMs = 150;
constexpr uint32_t kCommandRateLimitMs = 2000;
constexpr uint32_t kStatusPublishMs = 30000;
constexpr uint32_t kStandalonePortalMs = 10UL * 60UL * 1000UL;
constexpr uint16_t kMqttBufferSize = 1536;
constexpr uint16_t kWebPort = 80;
constexpr uint16_t kDnsPort = 53;
}  // namespace Config
