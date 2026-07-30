#include "Sensor/LocalTemperatureSensor.h"

#include <DallasTemperature.h>
#include <OneWire.h>

#include "Climate/MideaState.h"
#include "config.h"

namespace {
constexpr uint8_t kResolutionBits = 11;
constexpr uint32_t kConversionMs = 400;
}

LocalTemperatureSensor::~LocalTemperatureSensor() {
  delete _sensors;
  delete _oneWire;
}

void LocalTemperatureSensor::begin(uint8_t pin) {
  _pin = pin;
  pinMode(_pin, INPUT_PULLUP);
  _oneWire = new OneWire(_pin);
  _sensors = new DallasTemperature(_oneWire);
  _sensors->begin();
  _sensors->setWaitForConversion(false);
  scan();
}

void LocalTemperatureSensor::scan() {
  _lastScanMs = millis();
  _sensors->begin();
  const bool firstScan = !_scanAttempted;
  _scanAttempted = true;
  const bool wasDetected = _detected;
  _detected = _sensors && _sensors->getAddress(_address, 0);
  if (!_detected) {
    _readingValid = false;
    _conversionPending = false;
    _temperature = NAN;
    if (wasDetected) ++_revision;
    if (firstScan || wasDetected)
      Serial.printf("[SENSOR] No DS18B20 detected on GPIO%u\n", _pin);
    return;
  }
  _sensors->setResolution(_address, kResolutionBits);
  Serial.printf("[SENSOR] DS18B20 detected on GPIO%u\n", _pin);
  ++_revision;
  requestReading();
}

void LocalTemperatureSensor::requestReading() {
  if (!_detected || !_sensors) return;
  _sensors->requestTemperaturesByAddress(_address);
  _conversionStartedMs = millis();
  _conversionPending = true;
}

void LocalTemperatureSensor::setUnavailable(
    const __FlashStringHelper *reason) {
  const bool changed = _detected || _readingValid;
  _detected = false;
  _readingValid = false;
  _conversionPending = false;
  _temperature = NAN;
  if (changed) ++_revision;
  Serial.print(F("[SENSOR] "));
  Serial.println(reason);
}

void LocalTemperatureSensor::loop() {
  if (!_sensors) return;
  const uint32_t now = millis();
  if (!_detected) {
    if (ClimateValues::elapsed(now, _lastScanMs,
                               Config::kLocalTemperatureRescanMs))
      scan();
    return;
  }
  if (_conversionPending) {
    if (!ClimateValues::elapsed(now, _conversionStartedMs, kConversionMs))
      return;
    _conversionPending = false;
    _lastCycleMs = now;
    const float value = _sensors->getTempC(_address);
    if (value == DEVICE_DISCONNECTED_C || !isfinite(value)) {
      setUnavailable(F("DS18B20 disconnected or returned an invalid value"));
      return;
    }
    _temperature = value;
    _readingValid = true;
    ++_revision;
    return;
  }
  if (ClimateValues::elapsed(now, _lastCycleMs,
                             Config::kLocalTemperatureReadMs))
    requestReading();
}
