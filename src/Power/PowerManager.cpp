#include "Power/PowerManager.h"

#include "Climate/MideaState.h"

void PowerManager::begin(bool forceMaintenance) {
  _bootMs = millis();
  _energySavingReferenceMs = _bootMs;
  _rtcValid = RtcStorage::load(_rtc);
  ++_rtc.wakeCount;
  if (_rtcValid && _rtc.wifiBssidValid) {
    _settings.wifiChannel = _rtc.wifiChannel;
    memcpy(_settings.wifiBssid, _rtc.wifiBssid, sizeof(_settings.wifiBssid));
    _settings.wifiBssidValid = true;
  }
  _wakeReason = ESP.getResetReason().indexOf(F("Deep-Sleep")) >= 0
      ? "deep_sleep" : "power_on";
  if (!strcmp(_wakeReason, "deep_sleep"))
    _energySavingReferenceMs = _bootMs - Config::kEnergySavingGraceMs;
  _maintenance = forceMaintenance || _settings.forceConfigNextBoot;
  _settings.forceConfigNextBoot = false;
  readBattery();
  setPhase(_maintenance ? WakePhase::Maintenance : WakePhase::ConnectWifi);
  Serial.printf("[POWER] mode=%u wake=%s count=%lu rtc=%s battery=%.3fV\n",
                static_cast<unsigned>(_settings.powerMode), _wakeReason,
                static_cast<unsigned long>(_rtc.wakeCount),
                _rtcValid ? "valid" : "invalid", _batteryVoltage);
}

bool PowerManager::energySavingAllowed() const {
  return ClimateValues::elapsed(millis(), _energySavingReferenceMs,
                                Config::kEnergySavingGraceMs);
}

uint32_t PowerManager::energySavingDelayRemainingMs() const {
  const uint32_t age = millis() - _energySavingReferenceMs;
  return age >= Config::kEnergySavingGraceMs
      ? 0 : Config::kEnergySavingGraceMs - age;
}

void PowerManager::noteBrowserActivity() {
  _energySavingReferenceMs = millis();
}

void PowerManager::readBattery() {
  if (!_settings.batteryEnabled) return;
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 8; ++i) sum += analogRead(A0);
  _batteryVoltage = (sum / 8.0F) * (1.0F / 1023.0F) * _settings.batteryFactor;
}

float PowerManager::batteryPercent() const {
  if (!_settings.batteryEnabled || !_settings.batteryPercentEnabled ||
      !isfinite(_batteryVoltage)) return NAN;
  return ClimateValues::clampTemperature(
      100.0F * (_batteryVoltage - _settings.batteryEmptyV) /
          (_settings.batteryFullV - _settings.batteryEmptyV), 0, 100);
}

void PowerManager::setPhase(WakePhase phase) {
  _phase = phase;
  _phaseStartedMs = millis();
}

void PowerManager::loop(bool wifi, bool mqtt, bool workPending) {
  if (!deepSleepMode()) return;
  const uint32_t now = millis();
  if (ClimateValues::elapsed(now, _bootMs, _settings.maxAwakeSec * 1000UL)) {
    Serial.println(F("[POWER] Maximum wake time reached"));
    setPhase(WakePhase::Sleep);
    return;
  }
  switch (_phase) {
    case WakePhase::ConnectWifi:
      if (wifi) setPhase(WakePhase::ConnectMqtt);
      else if (ClimateValues::elapsed(now, _phaseStartedMs, _settings.wifiTimeoutSec * 1000UL))
        setPhase(WakePhase::Sleep);
      break;
    case WakePhase::ConnectMqtt:
      if (mqtt) setPhase(WakePhase::ReceiveRetained);
      else if (ClimateValues::elapsed(now, _phaseStartedMs, _settings.mqttTimeoutSec * 1000UL))
        setPhase(WakePhase::Sleep);
      break;
    case WakePhase::ReceiveRetained:
      if (ClimateValues::elapsed(now, _phaseStartedMs, _settings.retainedWaitMs))
        setPhase(WakePhase::Execute);
      break;
    case WakePhase::Execute:
      if (!workPending) setPhase(WakePhase::Publish);
      break;
    case WakePhase::Publish:
      if (ClimateValues::elapsed(now, _phaseStartedMs, 500)) setPhase(WakePhase::Sleep);
      break;
    default: break;
  }
}

bool PowerManager::acceptCommandId(uint32_t id) {
  if (id == 0 || id == _rtc.lastCommandId) return false;
  _rtc.lastCommandId = id;
  return true;
}

bool PowerManager::climateChanged(const MideaState &state) const {
  if (!_rtcValid || state != _rtc.lastClimateState) return true;
  if (_settings.safetyResendSec) {
    uint32_t wakes =
        (_settings.safetyResendSec + _settings.deepSleepIntervalSec - 1) /
        _settings.deepSleepIntervalSec;
    if (wakes < 1) wakes = 1;
    if (_rtc.wakeCount - _rtc.followMeWakeCount >= wakes) return true;
  }
  return false;
}

void PowerManager::rememberClimate(const MideaState &state, float roomTemperature,
                                   LastSendReason reason) {
  _rtc.lastClimateState = state;
  _rtc.lastRoomTemperature = roomTemperature;
  _rtc.lastSendReason = reason;
  _rtc.followMeWakeCount = _rtc.wakeCount;
}

void PowerManager::enterDeepSleep() {
  _rtc.wifiChannel = _settings.wifiChannel;
  memcpy(_rtc.wifiBssid, _settings.wifiBssid, sizeof(_rtc.wifiBssid));
  _rtc.wifiBssidValid = _settings.wifiBssidValid;
  RtcStorage::save(_rtc);
  uint32_t seconds = _settings.deepSleepIntervalSec;
  if (_settings.batteryEnabled && isfinite(_batteryVoltage) &&
      _batteryVoltage <= _settings.batteryCriticalV)
    seconds = max(seconds, _settings.lowBatterySleepSec);
  Serial.printf("[POWER] Deep sleep for %lu seconds\n", static_cast<unsigned long>(seconds));
  Serial.flush();
  ESP.deepSleep(static_cast<uint64_t>(seconds) * 1000000ULL, WAKE_RF_DEFAULT);
}
