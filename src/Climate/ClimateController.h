#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "Climate/PortaSplitState.h"
#include "Ir/PortaSplitIrController.h"
#include "Settings/Settings.h"

class ClimateController {
 public:
  ClimateController(const SettingsData &settings, PortaSplitIrController &ir);
  void begin();
  void loop();
  bool handleSetting(const char *key, const char *payload);
  bool handleJson(const uint8_t *payload, size_t length);
  bool requestResend();
  bool requestFollowMeTest(float temperature);
  void prepareWakeWork(bool climateStateChanged);
  const PortaSplitState &state() const { return _state; }
  uint32_t revision() const { return _revision; }
  uint32_t roomTemperatureAgeMs() const;
  uint32_t lastTemperatureMs() const { return _temperatureReceivedMs; }
  bool hasPendingWork() const { return _climatePending || _followMePending; }
  void toJson(JsonObject target) const;

 private:
  const SettingsData &_settings;
  PortaSplitIrController &_ir;
  PortaSplitState _state;
  bool _climatePending = false;
  bool _followMePending = false;
  uint32_t _climateDueMs = 0;
  uint32_t _temperatureReceivedMs = 0;
  uint32_t _revision = 1;
  uint32_t _lastCommandMs = 0;
  float _lastFollowMeTemperature = NAN;
  void changed(bool climateCommand);
  bool setRoomTemperature(const char *payload);
  void updateTemperatureValidity();
};
