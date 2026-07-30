#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>

#include "Settings/Settings.h"

class NetworkManager {
 public:
  explicit NetworkManager(SettingsData &settings) : _settings(settings) {}
  void begin(bool forcePortal);
  void loop();
  bool connected() const { return WiFi.status() == WL_CONNECTED; }
  bool portalActive() const { return _portalActive; }
  IPAddress ip() const { return connected() ? WiFi.localIP() : WiFi.softAPIP(); }
  uint32_t connectionDurationMs() const { return _connectedMs ? _connectedMs - _attemptStartedMs : 0; }
  void processDns() { if (_portalActive) _dns.processNextRequest(); }
  void stop();
  void requestScan(bool force = false);

 private:
  SettingsData &_settings;
  DNSServer _dns;
  bool _portalActive = false;
  bool _standalonePortalTimed = false;
  bool _scanHandled = true;
  uint32_t _portalStartedMs = 0;
  bool _fastAttempt = false;
  uint32_t _attemptStartedMs = 0;
  uint32_t _lastAttemptMs = 0;
  uint32_t _connectedMs = 0;
  void connect(bool fast);
  void startPortal();
  void stopPortal();
};
