#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "Climate/MideaState.h"
#include "Ir/MideaIrController.h"
#include "Settings/Settings.h"

enum class TemperatureSource : uint8_t { None, Mqtt, Local };

class ClimateController {
 public:
  ClimateController(const SettingsData &settings, MideaIrController &ir);
  void begin();
  void loop();
  bool handleSetting(const char *key, const char *payload);
  bool handleJson(const uint8_t *payload, size_t length);
  bool requestResend();
  bool requestFollowMeTest(float temperature);
  void updateLocalTemperature(bool detected, bool valid, float temperature);
  void prepareWakeWork(bool climateStateChanged);
  const MideaState &state() const { return _state; }
  uint32_t revision() const { return _revision; }
  uint32_t roomTemperatureAgeMs() const;
  uint32_t lastTemperatureMs() const { return _temperatureReceivedMs; }
  TemperatureSource temperatureSource() const { return _temperatureSource; }
  const char *temperatureSourceName() const;
  bool localSensorDetected() const { return _localSensorDetected; }
  bool localTemperatureValid() const { return _localTemperatureValid; }
  float localTemperature() const { return _localTemperature; }
  bool mqttTemperatureValid() const { return _mqttTemperatureValid; }
  bool hasPendingWork() const { return _climatePending || _followMePending; }
  void toJson(JsonObject target) const;

 private:
  const SettingsData &_settings;
  MideaIrController &_ir;
  MideaState _state;
  bool _climatePending = false;
  bool _followMePending = false;
  uint32_t _climateDueMs = 0;
  uint32_t _temperatureReceivedMs = 0;
  uint32_t _mqttTemperatureReceivedMs = 0;
  uint32_t _localTemperatureReceivedMs = 0;
  uint32_t _revision = 1;
  uint32_t _lastCommandMs = 0;
  float _lastFollowMeTemperature = NAN;
  float _mqttTemperature = NAN;
  float _localTemperature = NAN;
  bool _mqttTemperatureValid = false;
  bool _localSensorDetected = false;
  bool _localTemperatureValid = false;
  TemperatureSource _temperatureSource = TemperatureSource::None;
  void changed(bool climateCommand);
  bool setRoomTemperature(const char *payload);
  bool setMqttTemperature(float temperature);
  void selectTemperature();
  void updateTemperatureValidity();
};
