#pragma once

#include <stdint.h>
#include <stddef.h>

enum class ClimateMode : uint8_t { Auto, Cool, Heat, Dry, FanOnly };
enum class FanMode : uint8_t { Auto, Low, Medium, High };
enum class SwingMode : uint8_t { Off, Vertical };

struct MideaState {
  bool power = false;
  uint8_t targetTemperature = 22;
  float roomTemperature = 0.0F;
  bool roomTemperatureValid = false;
  ClimateMode mode = ClimateMode::Cool;
  FanMode fanMode = FanMode::Auto;
  SwingMode swingMode = SwingMode::Off;
  bool turbo = false;
  bool sleep = false;
  bool silent = false;
  bool display = true;
  bool iSense = false;

  bool operator==(const MideaState &other) const;
  bool operator!=(const MideaState &other) const { return !(*this == other); }
};

namespace ClimateValues {
bool parseBool(const char *value, bool &out);
bool parseMode(const char *value, ClimateMode &out);
bool parseFan(const char *value, FanMode &out);
bool parseSwing(const char *value, SwingMode &out);
const char *toString(ClimateMode value);
const char *toString(FanMode value);
const char *toString(SwingMode value);
float clampTemperature(float value, float minimum, float maximum);
float correctedTemperature(float value, float correction, float minimum, float maximum);
uint8_t roundSensorTemperature(float value);
bool elapsed(uint32_t now, uint32_t since, uint32_t interval);
bool buildTopic(char *target, size_t size, const char *base, const char *suffix);
bool validate(MideaState &state);
}  // namespace ClimateValues
