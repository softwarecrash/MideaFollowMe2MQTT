#include "Climate/ClimateController.h"

#include <stdlib.h>

ClimateController::ClimateController(const SettingsData &settings,
                                     MideaIrController &ir)
    : _settings(settings), _ir(ir) {}

void ClimateController::begin() {
  _state.iSense = _settings.iSenseEnabled;
  ClimateValues::validate(_state);
}

void ClimateController::changed(bool climateCommand) {
  ++_revision;
  if (climateCommand) {
    _climatePending = true;
    _climateDueMs = millis() + _settings.commandCoalesceMs;
  }
}

bool ClimateController::setRoomTemperature(const char *payload) {
  if (!payload || !*payload) return false;
  char *end = nullptr;
  const float parsed = strtof(payload, &end);
  if (end == payload || *end != '\0' || !isfinite(parsed)) return false;
  return setMqttTemperature(parsed);
}

bool ClimateController::setMqttTemperature(float temperature) {
  const float corrected = temperature + _settings.temperatureCorrection;
  if (!isfinite(temperature) || corrected < _settings.temperatureMin ||
      corrected > _settings.temperatureMax)
    return false;
  _mqttTemperature = corrected;
  _mqttTemperatureValid = true;
  _mqttTemperatureReceivedMs = millis();
  selectTemperature();
  return true;
}

void ClimateController::updateLocalTemperature(bool detected, bool valid,
                                               float temperature) {
  const bool correctedValid = detected && valid && isfinite(temperature) &&
      temperature + _settings.temperatureCorrection >= _settings.temperatureMin &&
      temperature + _settings.temperatureCorrection <= _settings.temperatureMax;
  const bool metadataChanged = detected != _localSensorDetected ||
      correctedValid != _localTemperatureValid;
  _localSensorDetected = detected;
  _localTemperatureValid = correctedValid;
  if (correctedValid) {
    _localTemperature = temperature + _settings.temperatureCorrection;
    _localTemperatureReceivedMs = millis();
  } else {
    _localTemperature = NAN;
    _localTemperatureReceivedMs = 0;
  }
  const uint32_t before = _revision;
  selectTemperature();
  if (metadataChanged && _revision == before) changed(false);
}

void ClimateController::selectTemperature() {
  TemperatureSource selected = TemperatureSource::None;
  float selectedTemperature = NAN;
  uint32_t selectedReceivedMs = 0;
  if (!_settings.standaloneMode && _mqttTemperatureValid) {
    selected = TemperatureSource::Mqtt;
    selectedTemperature = _mqttTemperature;
    selectedReceivedMs = _mqttTemperatureReceivedMs;
  } else if (_localTemperatureValid &&
             (_settings.standaloneMode || _settings.localSensorFallback)) {
    selected = TemperatureSource::Local;
    selectedTemperature = _localTemperature;
    selectedReceivedMs = _localTemperatureReceivedMs;
  }

  if (selected == TemperatureSource::None) {
    if (_state.roomTemperatureValid ||
        _temperatureSource != TemperatureSource::None) {
      _state.roomTemperatureValid = false;
      _temperatureSource = TemperatureSource::None;
      _temperatureReceivedMs = 0;
      _followMePending = false;
      changed(false);
      Serial.println(F("[ISENSE] No valid temperature source; transmission stopped"));
    }
    return;
  }

  const bool sourceChanged = selected != _temperatureSource;
  const bool immediate = !_state.roomTemperatureValid || sourceChanged ||
      fabsf(selectedTemperature - _state.roomTemperature) >=
          _settings.immediateChange;
  const bool valueChanged = !_state.roomTemperatureValid ||
      selectedTemperature != _state.roomTemperature;
  _state.roomTemperature = selectedTemperature;
  _state.roomTemperatureValid = true;
  _temperatureSource = selected;
  _temperatureReceivedMs = selectedReceivedMs;
  if (_state.iSense && immediate) _followMePending = true;
  if (sourceChanged || valueChanged) {
    changed(false);
    Serial.printf("[ISENSE] Temperature source: %s (%.2f C)\n",
                  temperatureSourceName(), selectedTemperature);
  }
}

bool ClimateController::handleSetting(const char *key, const char *payload) {
  if (!key || !payload) return false;
  bool boolean = false;
  bool climate = true;
  if (!strcmp(key, "power") && ClimateValues::parseBool(payload, boolean)) _state.power = boolean;
  else if (!strcmp(key, "mode") && !strcasecmp(payload, "off")) _state.power = false;
  else if (!strcmp(key, "mode") && ClimateValues::parseMode(payload, _state.mode)) {}
  else if (!strcmp(key, "fan") && ClimateValues::parseFan(payload, _state.fanMode)) {}
  else if (!strcmp(key, "swing") && ClimateValues::parseSwing(payload, _state.swingMode)) {}
  else if (!strcmp(key, "target_temperature")) {
    char *end = nullptr;
    const long value = strtol(payload, &end, 10);
    if (*payload == '\0' || *end != '\0' || value < 17 || value > 30) return false;
    _state.targetTemperature = static_cast<uint8_t>(value);
  } else if (!strcmp(key, "turbo") && ClimateValues::parseBool(payload, boolean)) _state.turbo = boolean;
  else if (!strcmp(key, "sleep") && ClimateValues::parseBool(payload, boolean)) _state.sleep = boolean;
  else if (!strcmp(key, "silent") && ClimateValues::parseBool(payload, boolean)) _state.silent = boolean;
  else if (!strcmp(key, "display") && ClimateValues::parseBool(payload, boolean)) _state.display = boolean;
  else if (!strcmp(key, "isense") && ClimateValues::parseBool(payload, boolean)) {
    _state.iSense = boolean;
    _followMePending = boolean && _state.roomTemperatureValid;
    climate = false;
  } else if (!strcmp(key, "room_temperature")) return setRoomTemperature(payload);
  else return false;
  ClimateValues::validate(_state);
  changed(climate);
  return true;
}

bool ClimateController::handleJson(const uint8_t *payload, size_t length) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, length) != DeserializationError::Ok ||
      !doc.is<JsonObject>()) return false;
  bool accepted = false;
  for (JsonPairConst item : doc.as<JsonObjectConst>()) {
    char value[24];
    if (item.value().is<bool>()) strlcpy(value, item.value().as<bool>() ? "ON" : "OFF", sizeof(value));
    else if (item.value().is<const char *>()) strlcpy(value, item.value().as<const char *>(), sizeof(value));
    else if (item.value().is<float>() || item.value().is<int>())
      snprintf(value, sizeof(value), "%.2f", item.value().as<float>());
    else continue;
    accepted |= handleSetting(item.key().c_str(), value);
  }
  return accepted;
}

bool ClimateController::requestResend() {
  const uint32_t now = millis();
  if (!ClimateValues::elapsed(now, _lastCommandMs, Config::kCommandRateLimitMs)) return false;
  _lastCommandMs = now;
  _climatePending = true;
  _climateDueMs = now;
  return true;
}

bool ClimateController::requestFollowMeTest(float temperature) {
  return setMqttTemperature(temperature);
}

void ClimateController::prepareWakeWork(bool climateStateChanged) {
  _climatePending = climateStateChanged;
  _climateDueMs = millis();
  _followMePending = _state.iSense && _state.roomTemperatureValid;
}

void ClimateController::updateTemperatureValidity() {
  if (_mqttTemperatureValid &&
      ClimateValues::elapsed(millis(), _mqttTemperatureReceivedMs,
                             _settings.temperatureTimeoutSec * 1000UL)) {
    _mqttTemperatureValid = false;
    Serial.println(F("[ISENSE] MQTT temperature is stale"));
    selectTemperature();
    if (_temperatureSource == TemperatureSource::None &&
        _settings.staleAction == StaleTemperatureAction::DisableISense) {
      _state.iSense = false;
      changed(false);
    }
  }
}

void ClimateController::loop() {
  updateTemperatureValidity();
  const uint32_t now = millis();
  if (_state.iSense && _state.roomTemperatureValid &&
      ClimateValues::elapsed(now, _ir.lastFollowMeMs(), _settings.followMeIntervalSec * 1000UL))
    _followMePending = true;
  if (_climatePending && static_cast<int32_t>(now - _climateDueMs) >= 0 && _ir.canSend(now)) {
    if (_ir.sendClimate(_state)) {
      _climatePending = false;
      if (_state.iSense && _state.roomTemperatureValid) _followMePending = true;
      ++_revision;
    }
    return;  // Climate has priority.
  }
  if (!_climatePending && _followMePending && _state.iSense &&
      _state.roomTemperatureValid && _ir.canSend(now)) {
    if (_ir.sendFollowMe(_state, _state.roomTemperature)) {
      _lastFollowMeTemperature = _state.roomTemperature;
      _followMePending = false;
      ++_revision;
    }
  }
}

uint32_t ClimateController::roomTemperatureAgeMs() const {
  return _state.roomTemperatureValid ? millis() - _temperatureReceivedMs : UINT32_MAX;
}

const char *ClimateController::temperatureSourceName() const {
  switch (_temperatureSource) {
    case TemperatureSource::Mqtt: return "mqtt";
    case TemperatureSource::Local: return "local_ds18b20";
    default: return "none";
  }
}

void ClimateController::toJson(JsonObject o) const {
  o["power"] = _state.power;
  o["mode"] = ClimateValues::toString(_state.mode);
  o["target_temperature"] = _state.targetTemperature;
  if (_state.roomTemperatureValid) o["room_temperature"] = _state.roomTemperature;
  else o["room_temperature"] = nullptr;
  o["room_temperature_valid"] = _state.roomTemperatureValid;
  o["temperature_source"] = temperatureSourceName();
  o["mqtt_temperature_valid"] = _mqttTemperatureValid;
  o["local_sensor_detected"] = _localSensorDetected;
  o["local_temperature_valid"] = _localTemperatureValid;
  if (_localTemperatureValid) o["local_temperature"] = _localTemperature;
  else o["local_temperature"] = nullptr;
  o["fan"] = ClimateValues::toString(_state.fanMode);
  o["swing"] = ClimateValues::toString(_state.swingMode);
  o["turbo"] = _state.turbo;
  o["sleep"] = _state.sleep;
  o["silent"] = _state.silent;
  o["display"] = _state.display;
  o["isense"] = _state.iSense;
}
