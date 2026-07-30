#include "Network/NetworkManager.h"

#include "Climate/MideaState.h"
#include "config.h"

void NetworkManager::begin(bool forcePortal) {
  WiFi.persistent(false);
  // The SDK reconnect policy continuously hops channels and destabilizes a
  // simultaneous setup AP. Reconnection is scan-driven in loop() instead.
  WiFi.setAutoReconnect(false);
  if (_settings.standaloneMode) {
    startPortal();
    _standalonePortalTimed = true;
    Serial.println(F("[WIFI] Standalone setup AP active for 10 minutes"));
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  if (forcePortal || !_settings.wifiSsid[0]) startPortal();
  else connect(_settings.fastConnect && _settings.wifiBssidValid);
}

void NetworkManager::connect(bool fast) {
  const WiFiMode_t wantedMode = _portalActive ? WIFI_AP_STA : WIFI_STA;
  if (WiFi.getMode() != wantedMode) WiFi.mode(wantedMode);
  _fastAttempt = fast;
  _attemptStartedMs = millis();
  _lastAttemptMs = _attemptStartedMs;
  if (fast)
    WiFi.begin(_settings.wifiSsid, _settings.wifiPassword, _settings.wifiChannel,
               _settings.wifiBssid, true);
  else
    WiFi.begin(_settings.wifiSsid, _settings.wifiPassword);
  Serial.printf("[WIFI] Connecting to %s%s\n", _settings.wifiSsid,
                fast ? " (fast connect)" : "");
}

void NetworkManager::startPortal() {
  if (_portalActive || _portalExpired) return;
  _portalActive = true;
  _portalStartedMs = millis();
  WiFi.mode(WIFI_AP_STA);
  char apName[48];
  snprintf(apName, sizeof(apName), "%s-AP", _settings.deviceName);
  WiFi.softAP(apName);
  _dns.start(Config::kDnsPort, "*", WiFi.softAPIP());
  // With saved credentials, let the station association finish without
  // overlapping it with an SDK scan. The wizard starts a scan on demand.
  if (!_settings.wifiSsid[0]) requestScan(true);
  else _lastAttemptMs = millis();
  Serial.printf("[WIFI] Setup portal '%s' at %s\n", apName,
                WiFi.softAPIP().toString().c_str());
}

void NetworkManager::stopPortal() {
  if (!_portalActive) return;
  _dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  _portalActive = false;
  Serial.println(F("[WIFI] Setup AP stopped"));
}

void NetworkManager::requestScan(bool force) {
  if (WiFi.getMode() == WIFI_OFF) return;
  const int state = WiFi.scanComplete();
  if (force && state != WIFI_SCAN_RUNNING) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
    _scanHandled = false;
    _lastAttemptMs = millis();
  } else if (state == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true, true);
    _scanHandled = false;
    _lastAttemptMs = millis();
  }
}

void NetworkManager::loop(bool allowEnergySaving) {
  processDns();
  const uint32_t now = millis();
  if (_settings.standaloneMode) {
    if (_portalActive && _standalonePortalTimed &&
        allowEnergySaving) {
      Serial.println(F("[WIFI] Standalone setup window ended; radio disabled"));
      _dns.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
      _portalActive = false;
    }
    return;
  }
  if (_portalActive && allowEnergySaving && _settings.wifiSsid[0]) {
    _portalExpired = true;
    stopPortal();
  }

  const bool wantModemSleep =
      _settings.powerMode == PowerMode::ModemSleep && allowEnergySaving &&
      !_portalActive;
  if (wantModemSleep != _modemSleepActive) {
    WiFi.setSleepMode(wantModemSleep ? WIFI_MODEM_SLEEP : WIFI_NONE_SLEEP);
    _modemSleepActive = wantModemSleep;
    Serial.println(wantModemSleep
        ? F("[POWER] Wi-Fi modem sleep enabled after inactivity window")
        : F("[POWER] Wi-Fi modem sleep paused by browser activity"));
  }

  if (connected()) {
    if (!_connectedMs) {
      _connectedMs = now;
      _settings.wifiChannel = WiFi.channel();
      memcpy(_settings.wifiBssid, WiFi.BSSID(), 6);
      _settings.wifiBssidValid = true;
      Serial.printf("[WIFI] Connected, IP=%s RSSI=%d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    if (_portalActive) stopPortal();
    return;
  }
  _connectedMs = 0;
  if (_fastAttempt && ClimateValues::elapsed(now, _attemptStartedMs, 5000)) {
    Serial.println(F("[WIFI] Fast connect failed; falling back to normal association"));
    connect(false);
    return;
  }
  if ((_settings.powerMode != PowerMode::DeepSleep || !allowEnergySaving) &&
      !_portalActive &&
      ClimateValues::elapsed(now, _attemptStartedMs, _settings.wifiTimeoutSec * 1000UL)) {
    startPortal();
    return;
  }
  if ((_settings.powerMode != PowerMode::DeepSleep || !allowEnergySaving) &&
      _portalActive &&
      _settings.wifiSsid[0]) {
    const int scanCount = WiFi.scanComplete();
    if (scanCount >= 0 && !_scanHandled) {
      _scanHandled = true;
      for (int i = 0; i < scanCount; ++i) {
        if (WiFi.SSID(i) != _settings.wifiSsid) continue;
        _settings.wifiChannel = WiFi.channel(i);
        memcpy(_settings.wifiBssid, WiFi.BSSID(i), 6);
        _settings.wifiBssidValid = true;
        Serial.println(F("[WIFI] Saved network found in scan"));
        connect(true);
        // This BSSID came from a fresh scan, so an all-channel fallback is
        // unnecessary and would disturb the setup AP.
        _fastAttempt = false;
        return;
      }
    }
    if (scanCount != WIFI_SCAN_RUNNING &&
        ClimateValues::elapsed(now, _lastAttemptMs, Config::kWifiRetryMs))
      requestScan(true);
  }
}

void NetworkManager::stop() {
  _dns.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}
