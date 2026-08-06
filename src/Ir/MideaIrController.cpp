#include "Ir/MideaIrController.h"

#include "config.h"

namespace {
volatile uint32_t receiverEdges = 0;
void IRAM_ATTR countReceiverEdge() { ++receiverEdges; }
}

MideaIrController::~MideaIrController() { delete _midea; }

void MideaIrController::begin(const SettingsData &settings) {
  _settings = &settings;
  delete _midea;
  _midea = new IRMideaAC(settings.irPin, settings.irInverted);
  _midea->begin();
  prepareOutput();
  _midea->stateReset();
  Serial.printf("[IR] Midea transmitter ready on GPIO%u, inverted=%s\n",
                settings.irPin, settings.irInverted ? "true" : "false");
}

void MideaIrController::prepareOutput() {
  if (!_settings) return;
  pinMode(_settings->irPin, OUTPUT);
  // IRsend's idle level must be restored explicitly. This also recovers if
  // another subsystem or an earlier diagnostic changed the GPIO mode/state.
  digitalWrite(_settings->irPin, _settings->irInverted ? HIGH : LOW);
}

bool MideaIrController::canSend(uint32_t now) const {
  const uint32_t gap = Config::kMinIrGapMs > _settings->irRepeatPauseMs
      ? Config::kMinIrGapMs : _settings->irRepeatPauseMs;
  return _midea && static_cast<uint32_t>(now - _lastSendMs) >= gap;
}

void MideaIrController::applyClimate(const MideaState &state) {
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

void MideaIrController::logState(const char *type, const MideaState &state,
                                      float sensorTemperature) {
  if (!_settings->debug) return;
  Serial.printf("[IR] type=%s protocol=MIDEA gpio=%u repeats=%u raw=0x%012llX "
                "power=%u mode=%s target=%u fan=%s sensor=%.1f\n",
                type, _settings->irPin, _settings->irRepeats,
                static_cast<unsigned long long>(_lastRaw), state.power,
                ClimateValues::toString(state.mode), state.targetTemperature,
                ClimateValues::toString(state.fanMode), sensorTemperature);
}

bool MideaIrController::sendClimate(const MideaState &state) {
  if (!canSend(millis())) return false;
  prepareOutput();
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

bool MideaIrController::sendFollowMe(const MideaState &state, float temperature) {
  if (!canSend(millis())) return false;
  prepareOutput();
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

bool MideaIrController::sendLimitedHardwareTest() {
  if (!canSend(millis())) return false;
  const uint8_t pin = _settings->irPin;
  ++_hardwareTestCount;
  Serial.printf("[IR-TEST] #%lu start: GPIO%u, configured inverted=%s\n",
                static_cast<unsigned long>(_hardwareTestCount), pin,
                _settings->irInverted ? "true" : "false");

  // Phase 1 bypasses IRremoteESP8266 completely. Ten-microsecond active-HIGH
  // pulses at 10 kHz exercise the GPIO and transistor while limiting average
  // power to 10%. This is intentionally long enough for a camera to integrate.
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  const uint32_t directStartedUs = micros();
  for (uint16_t pulse = 0; pulse < 10000; ++pulse) {
    digitalWrite(pin, HIGH);
    delayMicroseconds(10);
    digitalWrite(pin, LOW);
    delayMicroseconds(90);
    if ((pulse % 250U) == 249U) yield();
  }
  _lastDirectTestUs = micros() - directStartedUs;
  Serial.printf("[IR-TEST] direct GPIO phase complete: 10000 pulses in %lu us\n",
                static_cast<unsigned long>(_lastDirectTestUs));
  delay(250);

  // Phase 2 follows the exact sender path used by the Midea implementation:
  // active HIGH, software carrier modulation, 38 kHz and 50% carrier duty.
  IRsend testOutput(_settings->irPin, false, true);
  testOutput.begin();
  testOutput.enableIROut(38, 50);
  pinMode(Config::kIrReceiverPin, INPUT_PULLUP);
  receiverEdges = 0;
  attachInterrupt(digitalPinToInterrupt(Config::kIrReceiverPin),
                  countReceiverEdge, CHANGE);
  _lastCarrierPulses = 0;
  const uint32_t carrierStartedUs = micros();
  for (uint8_t burst = 0; burst < 25; ++burst) {
    _lastCarrierPulses += testOutput.mark(20000);
    testOutput.space(20000);
  }
  detachInterrupt(digitalPinToInterrupt(Config::kIrReceiverPin));
  _lastReceiverEdges = receiverEdges;
  _lastCarrierTestUs = micros() - carrierStartedUs;
  digitalWrite(pin, LOW);
  _lastSendMs = millis();
  _lastRaw = 0;
  _lastType = IrCommandType::Test;
  Serial.printf("[IR-TEST] carrier phase: %lu generated pulses in %lu us, receiver GPIO%u saw %lu edges; GPIO LOW\n",
                static_cast<unsigned long>(_lastCarrierPulses),
                static_cast<unsigned long>(_lastCarrierTestUs),
                Config::kIrReceiverPin,
                static_cast<unsigned long>(_lastReceiverEdges));
  return true;
}
