#include "Mqtt/MqttManager.h"

#include <ArduinoJson.h>

#include "Mqtt/MqttTopics.h"
#include "config.h"

MqttManager *MqttManager::_instance = nullptr;

MqttManager::MqttManager(SettingsData &settings, ClimateController &climate,
                         PowerManager &power, PortaSplitIrController &ir)
    : _settings(settings), _climate(climate), _power(power), _ir(ir), _mqtt(_network) {}

void MqttManager::begin() {
  _instance = this;
  _mqtt.setCallback(callback);
  _mqtt.setBufferSize(Config::kMqttBufferSize);
  _mqtt.setSocketTimeout(2);
}

void MqttManager::callback(char *topic, uint8_t *payload, unsigned int length) {
  if (_instance) _instance->handle(topic, payload, length);
}

bool MqttManager::connect() {
  if (!_settings.mqttHost[0]) return false;
  _mqtt.setServer(_settings.mqttHost, _settings.mqttPort);
  const String availability = MqttTopics::topic(_settings.mqttBaseTopic, "availability");
  _connectStartedMs = millis();
  const bool ok = _mqtt.connect(
      _settings.mqttClientId[0] ? _settings.mqttClientId : _settings.deviceName,
      _settings.mqttUser, _settings.mqttPassword,
      availability.c_str(), 1, true, "offline");
  if (!ok) {
    Serial.printf("[MQTT] Connect failed, state=%d\n", _mqtt.state());
    return false;
  }
  _connectedMs = millis();
  Serial.println(F("[MQTT] Connected"));
  publish("availability", _power.maintenanceMode() ? "maintenance" : "online", true);
  subscribe();
  publishState(true);
  publishDiagnostics();
  if (_settings.haDiscovery) publishDiscovery();
  return true;
}

void MqttManager::subscribe() {
  const String base = MqttTopics::topic(_settings.mqttBaseTopic, "set/#");
  const String command = MqttTopics::topic(_settings.mqttBaseTopic, "command/#");
  _mqtt.subscribe(base.c_str());
  _mqtt.subscribe(command.c_str());
  Serial.println(F("[MQTT] Command topics subscribed"));
}

bool MqttManager::publish(const char *suffix, const char *payload, bool retain) {
  const String full = MqttTopics::topic(_settings.mqttBaseTopic, suffix);
  return _mqtt.publish(full.c_str(), payload, retain);
}

void MqttManager::loop(bool networkConnected) {
  const uint32_t now = millis();
  if (!networkConnected) {
    if (_mqtt.connected()) _mqtt.disconnect();
    _connectedMs = 0;
    return;
  }
  if (!_mqtt.connected()) {
    _connectedMs = 0;
    if (ClimateValues::elapsed(now, _lastConnectAttemptMs, Config::kMqttRetryMs)) {
      _lastConnectAttemptMs = now;
      connect();
    }
    return;
  }
  _mqtt.loop();
  if (_climate.revision() != _publishedRevision) publishState();
  if (ClimateValues::elapsed(now, _lastStatusMs, Config::kStatusPublishMs))
    publishDiagnostics();
}

void MqttManager::handle(char *topic, uint8_t *payload, unsigned int length) {
  _lastContactMs = millis();
  if (length >= 512) {
    Serial.println(F("[MQTT] Rejected oversized payload"));
    return;
  }
  char text[512];
  memcpy(text, payload, length);
  text[length] = '\0';
  const size_t baseLength = strlen(_settings.mqttBaseTopic);
  if (strncmp(topic, _settings.mqttBaseTopic, baseLength) || topic[baseLength] != '/') return;
  const char *suffix = topic + baseLength + 1;
  bool accepted = false;
  if (!strcmp(suffix, "set")) accepted = _climate.handleJson(payload, length);
  else if (!strncmp(suffix, "set/", 4)) {
    const char *key = suffix + 4;
    if (!strcmp(key, "maintenance_mode")) {
      bool enabled;
      accepted = ClimateValues::parseBool(text, enabled);
      if (accepted) _power.setMaintenance(enabled);
    } else {
      accepted = _climate.handleSetting(key, text);
    }
  } else if (!strncmp(suffix, "command/", 8)) {
    handleCommand(suffix + 8, payload, length);
    return;
  }
  Serial.printf(accepted ? "[MQTT] Accepted %s\n" : "[MQTT] Rejected %s\n", suffix);
  if (accepted) publishState();
}

void MqttManager::handleCommand(const char *suffix, const uint8_t *payload, size_t length) {
  if (!strcmp(suffix, "restart")) {
    publish("command/ack", "{\"result\":\"restarting\"}", false);
    delay(10);
    ESP.restart();
  }
  JsonDocument doc;
  uint32_t id = 0;
  const char *command = suffix;
  if (deserializeJson(doc, payload, length) == DeserializationError::Ok) {
    id = doc["id"] | 0U;
    command = doc["command"] | suffix;
    if (!_power.acceptCommandId(id)) {
      Serial.println(F("[MQTT] Duplicate or invalid command id ignored"));
      return;
    }
  } else if (_power.deepSleepMode()) {
    Serial.println(F("[MQTT] Deep-sleep one-shot commands require a JSON id"));
    return;
  }
  bool executed = false;
  if (!strcmp(command, "resend")) executed = _climate.requestResend();
  else if (!strcmp(command, "send_test")) executed = _climate.requestFollowMeTest(23.0F);
  char ack[96];
  snprintf(ack, sizeof(ack), "{\"id\":%lu,\"result\":\"%s\"}",
           static_cast<unsigned long>(id), executed ? "executed" : "rejected");
  publish("command/ack", ack, false);
}

void MqttManager::publishState(bool force) {
  if (!_mqtt.connected()) return;
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  _climate.toJson(root);
  root["assumed_state"] = true;
  root["last_ir_send_ms"] = _ir.lastSendMs();
  root["last_follow_me_send_ms"] = _ir.lastFollowMeMs();
  char json[768];
  serializeJson(doc, json, sizeof(json));
  publish("state", json, _settings.mqttRetain);
  char value[32];
  const PortaSplitState &s = _climate.state();
  publish("state/power", s.power ? "ON" : "OFF", _settings.mqttRetain);
  publish("state/mode", ClimateValues::toString(s.mode), _settings.mqttRetain);
  snprintf(value, sizeof(value), "%u", s.targetTemperature);
  publish("state/target_temperature", value, _settings.mqttRetain);
  if (s.roomTemperatureValid) {
    snprintf(value, sizeof(value), "%.1f", s.roomTemperature);
    publish("state/room_temperature", value, _settings.mqttRetain);
  }
  publish("state/room_temperature_valid", s.roomTemperatureValid ? "true" : "false", _settings.mqttRetain);
  publish("state/fan", ClimateValues::toString(s.fanMode), _settings.mqttRetain);
  publish("state/swing", ClimateValues::toString(s.swingMode), _settings.mqttRetain);
  publish("state/isense", s.iSense ? "ON" : "OFF", _settings.mqttRetain);
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(_ir.lastSendMs()));
  publish("state/last_ir_send", value, _settings.mqttRetain);
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(_ir.lastFollowMeMs()));
  publish("state/last_follow_me_send", value, _settings.mqttRetain);
  _publishedRevision = _climate.revision();
  (void)force;
}

void MqttManager::publishDiagnostics() {
  if (!_mqtt.connected()) return;
  _lastStatusMs = millis();
  JsonDocument doc;
  doc["firmware"] = PORTASPLIT_VERSION;
  doc["build_date"] = __DATE__ " " __TIME__;
  char chip[12];
  snprintf(chip, sizeof(chip), "%06X", ESP.getChipId());
  doc["chip_id"] = chip;
  doc["reset_reason"] = ESP.getResetReason();
  doc["uptime_s"] = millis() / 1000UL;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["power_mode"] = static_cast<uint8_t>(_settings.powerMode);
  doc["wake_reason"] = _power.wakeReason();
  doc["wake_duration_ms"] = _power.awakeMs();
  doc["wake_count"] = _power.wakeCount();
  if (isfinite(_power.batteryVoltage())) doc["battery_voltage"] = _power.batteryVoltage();
  if (isfinite(_power.batteryPercent())) doc["battery_percent"] = _power.batteryPercent();
  char json[512];
  serializeJson(doc, json, sizeof(json));
  publish("status", json, _settings.mqttRetain);
  publish("state/power_mode",
          _settings.powerMode == PowerMode::AlwaysOn ? "always_on" :
          (_settings.powerMode == PowerMode::ModemSleep ? "modem_sleep" : "deep_sleep"), true);
  publish("state/wake_reason", _power.wakeReason(), true);
  char value[32];
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(_power.awakeMs()));
  publish("state/wake_duration", value, true);
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(_power.wakeCount()));
  publish("state/wake_count", value, true);
  if (isfinite(_power.batteryVoltage())) {
    snprintf(value, sizeof(value), "%.3f", _power.batteryVoltage());
    publish("state/battery_voltage", value, true);
  }
  if (isfinite(_power.batteryPercent())) {
    snprintf(value, sizeof(value), "%.1f", _power.batteryPercent());
    publish("state/battery_percent", value, true);
  }
}

void MqttManager::publishDiscovery() {
  JsonDocument doc;
  const String prefix = String("homeassistant/climate/") + _settings.deviceName + "/config";
  doc["name"] = _settings.deviceName;
  doc["unique_id"] = String("portasplit_") + String(ESP.getChipId(), HEX);
  doc["availability_topic"] = MqttTopics::topic(_settings.mqttBaseTopic, "availability");
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";
  doc["power_command_topic"] = MqttTopics::topic(_settings.mqttBaseTopic, "set/power");
  doc["power_state_topic"] = MqttTopics::topic(_settings.mqttBaseTopic, "state/power");
  doc["mode_command_topic"] = MqttTopics::topic(_settings.mqttBaseTopic, "set/mode");
  doc["mode_state_topic"] = MqttTopics::topic(_settings.mqttBaseTopic, "state/mode");
  JsonArray modes = doc["modes"].to<JsonArray>();
  modes.add("off"); modes.add("auto"); modes.add("cool"); modes.add("heat");
  modes.add("dry"); modes.add("fan_only");
  doc["temperature_command_topic"] = MqttTopics::topic(_settings.mqttBaseTopic, "set/target_temperature");
  doc["temperature_state_topic"] = MqttTopics::topic(_settings.mqttBaseTopic, "state/target_temperature");
  doc["current_temperature_topic"] = MqttTopics::topic(_settings.mqttBaseTopic, "state/room_temperature");
  doc["fan_mode_command_topic"] = MqttTopics::topic(_settings.mqttBaseTopic, "set/fan");
  doc["fan_mode_state_topic"] = MqttTopics::topic(_settings.mqttBaseTopic, "state/fan");
  JsonArray fans = doc["fan_modes"].to<JsonArray>();
  fans.add("auto"); fans.add("low"); fans.add("medium"); fans.add("high");
  doc["swing_mode_command_topic"] = MqttTopics::topic(_settings.mqttBaseTopic, "set/swing");
  doc["swing_mode_state_topic"] = MqttTopics::topic(_settings.mqttBaseTopic, "state/swing");
  JsonArray swings = doc["swing_modes"].to<JsonArray>();
  swings.add("off"); swings.add("vertical");
  doc["min_temp"] = 17;
  doc["max_temp"] = 30;
  doc["temp_step"] = 1;
  doc["temperature_unit"] = "C";
  doc["optimistic"] = true;
  doc["retain"] = _settings.mqttRetain;
  JsonObject device = doc["device"].to<JsonObject>();
  device["name"] = _settings.deviceName;
  device["manufacturer"] = "softwarecrash";
  device["model"] = "PortaSplit2MQTT (assumed-state IR bridge)";
  device["sw_version"] = PORTASPLIT_VERSION;
  char payload[1400];
  serializeJson(doc, payload, sizeof(payload));
  _mqtt.publish(prefix.c_str(), payload, true);
}

void MqttManager::disconnect() {
  if (!_mqtt.connected()) return;
  publish("availability", "offline", true);
  _mqtt.disconnect();
  _network.stop();
}
