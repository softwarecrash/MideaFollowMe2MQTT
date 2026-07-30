#include <unity.h>

#include "Climate/MideaState.h"

void test_payload_parsing() {
  bool value = false;
  TEST_ASSERT_TRUE(ClimateValues::parseBool("ON", value));
  TEST_ASSERT_TRUE(value);
  TEST_ASSERT_TRUE(ClimateValues::parseBool("false", value));
  TEST_ASSERT_FALSE(value);
  TEST_ASSERT_FALSE(ClimateValues::parseBool("maybe", value));
}

void test_enum_conversion() {
  ClimateMode mode;
  FanMode fan;
  SwingMode swing;
  TEST_ASSERT_TRUE(ClimateValues::parseMode("fan_only", mode));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ClimateMode::FanOnly),
                          static_cast<uint8_t>(mode));
  TEST_ASSERT_TRUE(ClimateValues::parseFan("medium", fan));
  TEST_ASSERT_EQUAL_STRING("medium", ClimateValues::toString(fan));
  TEST_ASSERT_TRUE(ClimateValues::parseSwing("vertical", swing));
  TEST_ASSERT_EQUAL_STRING("vertical", ClimateValues::toString(swing));
}

void test_temperature_math() {
  TEST_ASSERT_FLOAT_WITHIN(0.001F, 37.0F,
      ClimateValues::clampTemperature(39.0F, 0.0F, 37.0F));
  TEST_ASSERT_FLOAT_WITHIN(0.001F, 23.9F,
      ClimateValues::correctedTemperature(23.4F, 0.5F, 0.0F, 37.0F));
  TEST_ASSERT_EQUAL_UINT8(23, ClimateValues::roundSensorTemperature(23.49F));
  TEST_ASSERT_EQUAL_UINT8(24, ClimateValues::roundSensorTemperature(23.50F));
}

void test_timeout_wraparound() {
  TEST_ASSERT_TRUE(ClimateValues::elapsed(10U, 0xFFFFFFF0U, 20U));
  TEST_ASSERT_FALSE(ClimateValues::elapsed(10U, 0xFFFFFFF0U, 30U));
}

void test_topic_generation() {
  char topic[64];
  TEST_ASSERT_TRUE(ClimateValues::buildTopic(
      topic, sizeof(topic), "mideafollowme/living", "set/power"));
  TEST_ASSERT_EQUAL_STRING("mideafollowme/living/set/power", topic);
  char tooSmall[4];
  TEST_ASSERT_FALSE(ClimateValues::buildTopic(tooSmall, sizeof(tooSmall), "base", "state"));
}

void test_state_validation() {
  MideaState state;
  state.targetTemperature = 2;
  TEST_ASSERT_TRUE(ClimateValues::validate(state));
  TEST_ASSERT_EQUAL_UINT8(17, state.targetTemperature);
  state.targetTemperature = 99;
  ClimateValues::validate(state);
  TEST_ASSERT_EQUAL_UINT8(30, state.targetTemperature);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_payload_parsing);
  RUN_TEST(test_enum_conversion);
  RUN_TEST(test_temperature_math);
  RUN_TEST(test_timeout_wraparound);
  RUN_TEST(test_topic_generation);
  RUN_TEST(test_state_validation);
  return UNITY_END();
}
