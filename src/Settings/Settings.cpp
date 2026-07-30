#include "Settings/Settings.h"

#include <EEPROM.h>

namespace {
constexpr size_t kEepromSize = sizeof(SettingsData);
template <size_t N>
void copyText(char (&target)[N], const char *source) {
  strlcpy(target, source, N);
}
}

uint32_t Settings::calculateCrc(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  while (length--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
  }
  return ~crc;
}

void Settings::reset() {
  memset(&_data, 0, sizeof(_data));
  _data.magic = Config::kSettingsMagic;
  _data.version = Config::kSettingsVersion;
  copyText(_data.deviceName, "PortaSplit2MQTT");
  _data.standaloneMode = false;
  copyText(_data.mqttClientId, "portasplit");
  copyText(_data.mqttBaseTopic, "portasplit/portasplit");
  _data.mqttPort = 1883;
  _data.mqttRetain = true;
  _data.irPin = Config::kDefaultIrPin;
  _data.irRepeats = 0;
  _data.irRepeatPauseMs = 150;
  _data.commandCoalesceMs = 250;
  _data.iSenseEnabled = true;
  _data.followMeIntervalSec = 120;
  _data.temperatureTimeoutSec = 600;
  _data.temperatureMin = 0;
  _data.temperatureMax = 37;
  _data.immediateChange = 0.5F;
  _data.staleAction = StaleTemperatureAction::StopSending;
  _data.powerMode = PowerMode::AlwaysOn;
  _data.deepSleepIntervalSec = 120;
  _data.maxAwakeSec = 30;
  _data.wifiTimeoutSec = 15;
  _data.mqttTimeoutSec = 10;
  _data.retainedWaitMs = 2000;
  _data.safetyResendSec = 0;
  _data.batteryFactor = 1.0F;
  _data.batteryWarningV = 3.5F;
  _data.batteryCriticalV = 3.3F;
  _data.lowBatterySleepSec = 900;
  _data.batteryEmptyV = 3.2F;
  _data.batteryFullV = 4.2F;
}

bool Settings::validate(bool logErrors) {
  bool ok = true;
  auto fail = [&](const __FlashStringHelper *message) {
    ok = false;
    if (logErrors) { Serial.print(F("[CONFIG] ")); Serial.println(message); }
  };
  if (!_data.deviceName[0]) fail(F("Device name is required"));
  if (!_data.standaloneMode && _data.mqttPort == 0) fail(F("MQTT port must be non-zero"));
  if (_data.irPin > 16 || _data.irPin == 16) fail(F("IR pin must be GPIO 0..15 and not GPIO16/D0"));
  if (_data.commandCoalesceMs < 50 || _data.commandCoalesceMs > 2000) fail(F("Coalesce window must be 50..2000 ms"));
  if (_data.followMeIntervalSec < 10 || _data.followMeIntervalSec > 86400) fail(F("Follow-Me interval must be 10..86400 s"));
  if (_data.temperatureTimeoutSec < _data.followMeIntervalSec) fail(F("Temperature timeout must cover Follow-Me interval"));
  if (_data.temperatureMin < 0 || _data.temperatureMax > 37 || _data.temperatureMin >= _data.temperatureMax)
    fail(F("Sensor range must be within 0..37 C"));
  if (_data.followMeBeep) fail(F("Follow-Me beep is not exposed by IRremoteESP8266 and cannot be enabled safely"));
  if (_data.deepSleepIntervalSec < 10 || _data.maxAwakeSec < 5) fail(F("Deep-sleep timings are too short"));
  if (_data.powerMode == PowerMode::DeepSleep && _data.irPin == 16) fail(F("GPIO16 is reserved for deep-sleep wake"));
  if (_data.batteryPercentEnabled && _data.batteryFullV <= _data.batteryEmptyV) fail(F("Battery voltage curve is invalid"));
  return ok;
}

bool Settings::begin() {
  EEPROM.begin(kEepromSize);
  EEPROM.get(0, _data);
  EEPROM.end();
  const uint32_t stored = _data.crc;
  _data.crc = 0;
  const uint32_t calculated = calculateCrc(reinterpret_cast<const uint8_t *>(&_data), sizeof(_data));
  if (_data.magic != Config::kSettingsMagic || _data.version != Config::kSettingsVersion ||
      stored != calculated || !validate(false)) {
    Serial.println(F("[CONFIG] No valid settings found; using safe defaults"));
    reset();
    return false;
  }
  _data.crc = stored;
  Serial.println(F("[CONFIG] Settings loaded"));
  return true;
}

bool Settings::save() {
  if (!validate()) return false;
  _data.magic = Config::kSettingsMagic;
  _data.version = Config::kSettingsVersion;
  _data.crc = 0;
  _data.crc = calculateCrc(reinterpret_cast<const uint8_t *>(&_data), sizeof(_data));
  EEPROM.begin(kEepromSize);
  EEPROM.put(0, _data);
  const bool ok = EEPROM.commit();
  EEPROM.end();
  Serial.println(ok ? F("[CONFIG] Settings saved") : F("[CONFIG] EEPROM commit failed"));
  return ok;
}
