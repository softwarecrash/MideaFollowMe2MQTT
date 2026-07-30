#include "Climate/PortaSplitState.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bool PortaSplitState::operator==(const PortaSplitState &o) const {
  return power == o.power && targetTemperature == o.targetTemperature &&
         mode == o.mode && fanMode == o.fanMode && swingMode == o.swingMode &&
         turbo == o.turbo && sleep == o.sleep && silent == o.silent &&
         display == o.display && iSense == o.iSense;
}

namespace ClimateValues {
bool parseBool(const char *v, bool &out) {
  if (!v) return false;
  if (!strcasecmp(v, "ON") || !strcasecmp(v, "true") || !strcmp(v, "1")) {
    out = true;
    return true;
  }
  if (!strcasecmp(v, "OFF") || !strcasecmp(v, "false") || !strcmp(v, "0")) {
    out = false;
    return true;
  }
  return false;
}

bool parseMode(const char *v, ClimateMode &out) {
  if (!v) return false;
  if (!strcasecmp(v, "auto")) out = ClimateMode::Auto;
  else if (!strcasecmp(v, "cool")) out = ClimateMode::Cool;
  else if (!strcasecmp(v, "heat")) out = ClimateMode::Heat;
  else if (!strcasecmp(v, "dry")) out = ClimateMode::Dry;
  else if (!strcasecmp(v, "fan_only")) out = ClimateMode::FanOnly;
  else return false;
  return true;
}

bool parseFan(const char *v, FanMode &out) {
  if (!v) return false;
  if (!strcasecmp(v, "auto")) out = FanMode::Auto;
  else if (!strcasecmp(v, "low")) out = FanMode::Low;
  else if (!strcasecmp(v, "medium")) out = FanMode::Medium;
  else if (!strcasecmp(v, "high")) out = FanMode::High;
  else return false;
  return true;
}

bool parseSwing(const char *v, SwingMode &out) {
  if (!v) return false;
  if (!strcasecmp(v, "off")) out = SwingMode::Off;
  else if (!strcasecmp(v, "vertical")) out = SwingMode::Vertical;
  else return false;
  return true;
}

const char *toString(ClimateMode v) {
  switch (v) {
    case ClimateMode::Auto: return "auto";
    case ClimateMode::Cool: return "cool";
    case ClimateMode::Heat: return "heat";
    case ClimateMode::Dry: return "dry";
    case ClimateMode::FanOnly: return "fan_only";
  }
  return "unknown";
}
const char *toString(FanMode v) {
  switch (v) {
    case FanMode::Auto: return "auto";
    case FanMode::Low: return "low";
    case FanMode::Medium: return "medium";
    case FanMode::High: return "high";
  }
  return "unknown";
}
const char *toString(SwingMode v) {
  return v == SwingMode::Vertical ? "vertical" : "off";
}

float clampTemperature(float v, float min, float max) {
  return v < min ? min : (v > max ? max : v);
}
float correctedTemperature(float value, float correction, float minimum, float maximum) {
  return clampTemperature(value + correction, minimum, maximum);
}
uint8_t roundSensorTemperature(float v) {
  return static_cast<uint8_t>(floorf(v + 0.5F));
}
bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
  return static_cast<uint32_t>(now - since) >= interval;
}
bool buildTopic(char *target, size_t size, const char *base, const char *suffix) {
  if (!target || !size || !base || !suffix) return false;
  const int written = snprintf(target, size, "%s/%s", base, suffix);
  return written > 0 && static_cast<size_t>(written) < size;
}
bool validate(PortaSplitState &state) {
  if (state.targetTemperature < 17) state.targetTemperature = 17;
  if (state.targetTemperature > 30) state.targetTemperature = 30;
  return true;
}
}  // namespace ClimateValues
