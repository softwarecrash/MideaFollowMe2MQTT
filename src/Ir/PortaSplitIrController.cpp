#include "Ir/PortaSplitIrController.h"

#include "config.h"

PortaSplitIrController::~PortaSplitIrController() { delete _midea; }

void PortaSplitIrController::begin(const SettingsData &settings) {
  _settings = &settings;
  delete _midea;
  _midea = new IRMideaAC(settings.irPin, settings.irInverted);
  _midea->begin();
  _midea->stateReset();
  Serial.printf("[IR] Midea transmitter ready on GPIO%u, inverted=%s\n",
                settings.irPin, settings.irInverted ? "true" : "false");
}

bool PortaSplitIrController::canSend(uint32_t now) const {
  const uint32_t gap = Config::kMinIrGapMs > _settings->irRepeatPauseMs
      ? Config::kMinIrGapMs : _settings->irRepeatPauseMs;
  return _midea && static_cast<uint32_t>(now - _lastSendMs) >= gap;
}

void PortaSplitIrController::applyClimate(const PortaSplitState &state) {
  _midea->setEnableSensorTemp(false);
  _midea->setPower(state.power);
  _midea->setUseCelsius(true);
  _midea->setTemp(state.targetTemperature, true);
  switch (state.mode) {
    case ClimateMode::Auto: _midea->setMode(kMideaACAuto); break;
    case ClimateMode::Cool: _midea->setMode(kMideaACCool); break;
    case ClimateMode::Heat: _midea->setMode(kMideaACHeat); break;
    case ClimateMode::Dry: _midea->setMode(kMideaACDry); break;
    case ClimateMode::FanOnly: _midea->setMode(kMideaACFan); break;
  }
  switch (state.fanMode) {
    case FanMode::Auto: _midea->setFan(kMideaACFanAuto); break;
    case FanMode::Low: _midea->setFan(kMideaACFanLow); break;
    case FanMode::Medium: _midea->setFan(kMideaACFanMed); break;
    case FanMode::High: _midea->setFan(kMideaACFanHigh); break;
  }
  _midea->setSleep(state.sleep);
}

void PortaSplitIrController::logState(const char *type, const PortaSplitState &state,
                                      float sensorTemperature) {
  if (!_settings->debug) return;
  Serial.printf("[IR] type=%s protocol=MIDEA gpio=%u repeats=%u raw=0x%012llX "
                "power=%u mode=%s target=%u fan=%s sensor=%.1f\n",
                type, _settings->irPin, _settings->irRepeats,
                static_cast<unsigned long long>(_lastRaw), state.power,
                ClimateValues::toString(state.mode), state.targetTemperature,
                ClimateValues::toString(state.fanMode), sensorTemperature);
}

bool PortaSplitIrController::sendClimate(const PortaSplitState &state) {
  if (!canSend(millis())) return false;
  applyClimate(state);
  // These are one-shot/toggle commands in the library and remain unverified on PortaSplit.
  if ((!_hasLastApplied && state.swingMode == SwingMode::Vertical) ||
      (_hasLastApplied && state.swingMode != _lastApplied.swingMode))
    _midea->setSwingVToggle(true);
  if ((!_hasLastApplied && state.turbo) ||
      (_hasLastApplied && state.turbo != _lastApplied.turbo))
    _midea->setTurboToggle(true);
  if ((!_hasLastApplied && !state.display) ||
      (_hasLastApplied && state.display != _lastApplied.display))
    _midea->setLightToggle(true);
  if ((!_hasLastApplied && state.silent) ||
      (_hasLastApplied && state.silent != _lastApplied.silent))
    _midea->setQuiet(state.silent, _hasLastApplied && _lastApplied.silent);
  _lastRaw = _midea->getRaw();
  _midea->send(_settings->irRepeats);
  _lastSendMs = millis();
  _lastType = IrCommandType::Climate;
  _lastApplied = state;
  _hasLastApplied = true;
  logState("climate", state, NAN);
  return true;
}

bool PortaSplitIrController::sendFollowMe(const PortaSplitState &state, float temperature) {
  if (!canSend(millis())) return false;
  const uint8_t sensor = ClimateValues::roundSensorTemperature(
      ClimateValues::clampTemperature(temperature, 0.0F, 37.0F));
  applyClimate(state);
  _midea->setSensorTemp(sensor, true);  // Selects Midea's Follow-Me message type.
  _lastRaw = _midea->getRaw();
  _midea->send(_settings->irRepeats);
  _lastSendMs = millis();
  _lastFollowMeMs = _lastSendMs;
  _lastType = IrCommandType::FollowMe;
  logState("follow_me", state, sensor);
  return true;
}
