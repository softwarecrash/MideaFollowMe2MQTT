#pragma once

#include <Arduino.h>

class OneWire;
class DallasTemperature;

class LocalTemperatureSensor {
 public:
  ~LocalTemperatureSensor();
  void begin(uint8_t pin);
  void loop();
  bool detected() const { return _detected; }
  bool readingValid() const { return _readingValid; }
  float temperature() const { return _temperature; }
  uint32_t revision() const { return _revision; }

 private:
  OneWire *_oneWire = nullptr;
  DallasTemperature *_sensors = nullptr;
  uint8_t _address[8]{};
  uint8_t _pin = 0;
  bool _detected = false;
  bool _readingValid = false;
  bool _conversionPending = false;
  bool _scanAttempted = false;
  float _temperature = NAN;
  uint32_t _conversionStartedMs = 0;
  uint32_t _lastCycleMs = 0;
  uint32_t _lastScanMs = 0;
  uint32_t _revision = 0;

  void scan();
  void requestReading();
  void setUnavailable(const __FlashStringHelper *reason);
};
