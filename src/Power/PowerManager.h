#pragma once

#include <Arduino.h>

#include "Power/RtcState.h"
#include "Settings/Settings.h"

enum class WakePhase : uint8_t { ConnectWifi, ConnectMqtt, ReceiveRetained, Execute, Publish, Sleep, Maintenance };

class PowerManager {
 public:
  explicit PowerManager(SettingsData &settings) : _settings(settings) {}
  void begin(bool forceMaintenance);
  void loop(bool wifiConnected, bool mqttConnected, bool workPending);
  bool maintenanceMode() const { return _maintenance; }
  bool energySavingAllowed() const;
  uint32_t energySavingDelayRemainingMs() const;
  void noteBrowserActivity();
  bool deepSleepMode() const {
    return _settings.powerMode == PowerMode::DeepSleep && !_maintenance &&
           energySavingAllowed();
  }
  bool shouldSleep() const { return deepSleepMode() && _phase == WakePhase::Sleep; }
  bool retainedWindowOpen() const { return _phase == WakePhase::ReceiveRetained; }
  WakePhase phase() const { return _phase; }
  const char *wakeReason() const { return _wakeReason; }
  uint32_t awakeMs() const { return millis() - _bootMs; }
  uint32_t wakeCount() const { return _rtc.wakeCount; }
  float batteryVoltage() const { return _batteryVoltage; }
  float batteryPercent() const;
  uint32_t lastCommandId() const { return _rtc.lastCommandId; }
  bool acceptCommandId(uint32_t id);
  void setMaintenance(bool enabled) { _maintenance = enabled; if (enabled) _phase = WakePhase::Maintenance; }
  bool climateChanged(const MideaState &state) const;
  void rememberClimate(const MideaState &state, float roomTemperature, LastSendReason reason);
  void enterDeepSleep();

 private:
  SettingsData &_settings;
  RtcState _rtc{};
  bool _rtcValid = false;
  bool _maintenance = false;
  uint32_t _bootMs = 0;
  uint32_t _phaseStartedMs = 0;
  uint32_t _energySavingReferenceMs = 0;
  WakePhase _phase = WakePhase::ConnectWifi;
  float _batteryVoltage = NAN;
  const char *_wakeReason = "power_on";
  void setPhase(WakePhase phase);
  void readBattery();
};
