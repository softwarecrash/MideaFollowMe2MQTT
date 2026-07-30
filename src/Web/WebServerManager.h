#pragma once

#include <ESP8266WebServer.h>

#include "Climate/ClimateController.h"
#include "Mqtt/MqttManager.h"
#include "Network/NetworkManager.h"
#include "Power/PowerManager.h"
#include "Settings/Settings.h"

class WebServerManager {
 public:
  WebServerManager(Settings &settings, NetworkManager &network, MqttManager &mqtt,
                   ClimateController &climate, PowerManager &power,
                   MideaIrController &ir);
  void begin();
  void loop();

 private:
  Settings &_settings;
  NetworkManager &_network;
  MqttManager &_mqtt;
  ClimateController &_climate;
  PowerManager &_power;
  MideaIrController &_ir;
  ESP8266WebServer _server;
  bool _updateFileAccepted = false;
  bool _updateHeaderChecked = false;
  bool _updateWriteOk = false;
  uint32_t _restartAtMs = 0;
  void sendWebAsset(const uint8_t *data, size_t length, const char *contentType);
  void handleStatus();
  void handleSettingsJson();
  void handleSave();
  void handleNetworks();
  void handleSetup();
  void handleStandalone();
  void handleUpdateUpload();
};
