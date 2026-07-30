#pragma once

#include <Arduino.h>

#include "config.h"

enum class PowerMode : uint8_t { AlwaysOn, ModemSleep, DeepSleep };
enum class StaleTemperatureAction : uint8_t { StopSending, DisableISense };

struct SettingsData {
  uint32_t magic;
  uint16_t version;
  char deviceName[33];
  bool standaloneMode;
  char wifiSsid[33];
  char wifiPassword[65];
  char mqttHost[65];
  uint16_t mqttPort;
  char mqttUser[33];
  char mqttPassword[65];
  char mqttClientId[41];
  char mqttBaseTopic[65];
  bool mqttRetain;
  uint8_t irPin;
  bool irInverted;
  uint8_t irRepeats;
  uint16_t irRepeatPauseMs;
  uint16_t commandCoalesceMs;
  bool debug;
  bool iSenseEnabled;
  uint32_t followMeIntervalSec;
  uint32_t temperatureTimeoutSec;
  float temperatureMin;
  float temperatureMax;
  float temperatureCorrection;
  float immediateChange;
  bool followMeBeep;
  StaleTemperatureAction staleAction;
  PowerMode powerMode;
  uint32_t deepSleepIntervalSec;
  uint32_t maxAwakeSec;
  uint16_t wifiTimeoutSec;
  uint16_t mqttTimeoutSec;
  uint16_t retainedWaitMs;
  uint32_t safetyResendSec;
  bool fastConnect;
  uint8_t wifiChannel;
  uint8_t wifiBssid[6];
  bool wifiBssidValid;
  bool forceConfigNextBoot;
  bool batteryEnabled;
  float batteryFactor;
  float batteryWarningV;
  float batteryCriticalV;
  uint32_t lowBatterySleepSec;
  bool batteryPercentEnabled;
  float batteryEmptyV;
  float batteryFullV;
  bool haDiscovery;
  bool localSensorFallback;
  uint32_t crc;
};

class Settings {
 public:
  bool begin();
  bool save();
  void reset();
  bool validate(bool logErrors = true);
  SettingsData &data() { return _data; }
  const SettingsData &data() const { return _data; }

 private:
  SettingsData _data{};
  static uint32_t calculateCrc(const uint8_t *data, size_t length);
};
