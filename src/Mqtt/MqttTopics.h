#pragma once

#include <Arduino.h>

namespace MqttTopics {
inline String topic(const char *base, const char *suffix) {
  String result;
  result.reserve(strlen(base) + strlen(suffix) + 2);
  result += base;
  result += '/';
  result += suffix;
  return result;
}
}  // namespace MqttTopics

