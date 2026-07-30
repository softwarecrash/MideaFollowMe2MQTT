#include "Climate/ClimateController.h"

#include <stdlib.h>

ClimateController::ClimateController(const SettingsData &settings,
                                     PortaSplitIrController &ir)
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
  const float unbounded = parsed + _settings.temperatureCorrection;
  if (unbounded < _settings.temperatureMin || unbounded > _settings.temperatureMax) return false;
  const float corrected = ClimateValues::correctedTemperature(
      parsed, _settings.temperatureCorrection, _settings.temperatureMin,
      _settings.temperatureMax);
  const bool immediate = !_state.roomTemperatureValid ||
      fabsf(corrected - _state.roomTemperature) >= _settings.immediateChange;
  _state.roomTemperature = corrected;
  _state.roomTemperatureValid = true;
  _temperatureReceivedMs = millis();
  if (_state.iSense && immediate) _followMePending = true;
  changed(false);
  return true;
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
  if (temperature < 0 || temperature > 37) return false;
  _state.roomTemperature = temperature;
  _state.roomTemperatureValid = true;
  _temperatureReceivedMs = millis();
  _followMePending = true;
  changed(false);
  return true;
}

void ClimateController::prepareWakeWork(bool climateStateChanged) {
  _climatePending = climateStateChanged;
  _climateDueMs = millis();
  _followMePending = _state.iSense && _state.roomTemperatureValid;
}

void ClimateController::updateTemperatureValidity() {
  if (_state.roomTemperatureValid &&
      ClimateValues::elapsed(millis(), _temperatureReceivedMs,
                             _settings.temperatureTimeoutSec * 1000UL)) {
    _state.roomTemperatureValid = false;
    _followMePending = false;
    if (_settings.staleAction == StaleTemperatureAction::DisableISense) _state.iSense = false;
    changed(false);
    Serial.println(F("[ISENSE] External temperature is stale; transmission stopped"));
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

void ClimateController::toJson(JsonObject o) const {
  o["power"] = _state.power;
  o["mode"] = ClimateValues::toString(_state.mode);
  o["target_temperature"] = _state.targetTemperature;
  if (_state.roomTemperatureValid) o["room_temperature"] = _state.roomTemperature;
  else o["room_temperature"] = nullptr;
  o["room_temperature_valid"] = _state.roomTemperatureValid;
  o["fan"] = ClimateValues::toString(_state.fanMode);
  o["swing"] = ClimateValues::toString(_state.swingMode);
  o["turbo"] = _state.turbo;
  o["sleep"] = _state.sleep;
  o["silent"] = _state.silent;
  o["display"] = _state.display;
  o["isense"] = _state.iSense;
}
