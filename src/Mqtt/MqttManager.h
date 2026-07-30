#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "Climate/ClimateController.h"
#include "Power/PowerManager.h"
#include "Settings/Settings.h"

class MqttManager {
 public:
  MqttManager(SettingsData &settings, ClimateController &climate,
              PowerManager &power, PortaSplitIrController &ir);
  void begin();
  void loop(bool networkConnected);
  bool connected() { return _mqtt.connected(); }
  void publishState(bool force = false);
  void publishDiagnostics();
  void disconnect();
  uint32_t lastContactMs() const { return _lastContactMs; }
  uint32_t connectionDurationMs() const { return _connectedMs ? _connectedMs - _connectStartedMs : 0; }

 private:
  static MqttManager *_instance;
  SettingsData &_settings;
  ClimateController &_climate;
  PowerManager &_power;
  PortaSplitIrController &_ir;
  WiFiClient _network;
  PubSubClient _mqtt;
  uint32_t _lastConnectAttemptMs = 0;
  uint32_t _connectStartedMs = 0;
  uint32_t _connectedMs = 0;
  uint32_t _lastContactMs = 0;
  uint32_t _lastStatusMs = 0;
  uint32_t _publishedRevision = 0;
  bool connect();
  void subscribe();
  bool publish(const char *suffix, const char *payload, bool retain);
  void handle(char *topic, uint8_t *payload, unsigned int length);
  void handleCommand(const char *suffix, const uint8_t *payload, size_t length);
  void publishDiscovery();
  static void callback(char *topic, uint8_t *payload, unsigned int length);
};
