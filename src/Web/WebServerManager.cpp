#include "Web/WebServerManager.h"

#include <ArduinoJson.h>
#include <Updater.h>

#include "WebAssets.h"
#include "config.h"

WebServerManager::WebServerManager(Settings &settings, NetworkManager &network,
                                   MqttManager &mqtt, ClimateController &climate,
                                   PowerManager &power, MideaIrController &ir)
    : _settings(settings), _network(network), _mqtt(mqtt), _climate(climate),
      _power(power), _ir(ir), _server(Config::kWebPort) {}

void WebServerManager::begin() {
  _server.on("/style.css", HTTP_GET, [&]() {
    sendWebAsset(WebAssets::kStyle, WebAssets::kStyleLength, "text/css");
  });
  _server.on("/", HTTP_GET, [&]() {
    if (_network.portalActive()) {
      _server.sendHeader("Location", "/setup", true);
      _server.send(302, "text/plain", "");
    } else {
      sendWebAsset(WebAssets::kIndex, WebAssets::kIndexLength, "text/html");
    }
  });
  _server.on("/setup", HTTP_GET, [&]() {
    if (!_network.portalActive()) {
      _server.sendHeader("Location", "/", true);
      _server.send(302, "text/plain", "");
      return;
    }
    _network.requestScan();
    sendWebAsset(WebAssets::kWifi, WebAssets::kWifiLength, "text/html");
  });
  _server.on("/api/networks", HTTP_GET, [&]() { handleNetworks(); });
  _server.on("/api/setup", HTTP_POST, [&]() { handleSetup(); });
  _server.on("/api/standalone", HTTP_POST, [&]() { handleStandalone(); });
  auto captive = [&]() {
    if (_network.portalActive())
      sendWebAsset(WebAssets::kWifi, WebAssets::kWifiLength, "text/html");
    else _server.send(204, "text/plain", "");
  };
  _server.on("/generate_204", HTTP_GET, captive);
  _server.on("/gen_204", HTTP_GET, captive);
  _server.on("/hotspot-detect.html", HTTP_GET, captive);
  _server.on("/library/test/success.html", HTTP_GET, captive);
  _server.on("/ncsi.txt", HTTP_GET, captive);
  _server.on("/connecttest.txt", HTTP_GET, captive);
  _server.on("/fwlink", HTTP_GET, captive);
  _server.on("/api/activity", HTTP_GET, [&]() {
    _power.noteBrowserActivity();
    _server.send(204, "text/plain", "");
  });
  _server.on("/api/status", HTTP_GET, [&]() { handleStatus(); });
  _server.on("/api/settings", HTTP_GET, [&]() { handleSettingsJson(); });
  _server.on("/api/resend", HTTP_POST, [&]() {
    const bool accepted = _climate.requestResend();
    _server.send(accepted ? 202 : 429, "text/plain",
                 accepted ? "Current settings sent by infrared."
                          : "IR transmitter is busy. Please wait a moment.");
  });
  _server.on("/api/ir-test", HTTP_POST, [&]() {
    const bool sent = _ir.sendLimitedHardwareTest();
    if (!sent) {
      _server.send(429, "text/plain",
                   "IR transmitter is busy. Please wait a moment.");
      return;
    }
    char result[160];
    snprintf(result, sizeof(result),
             "IR test #%lu completed: direct %lu us, carrier %lu us / %lu pulses, receiver %lu edges.",
             static_cast<unsigned long>(_ir.hardwareTestCount()),
             static_cast<unsigned long>(_ir.lastDirectTestUs()),
             static_cast<unsigned long>(_ir.lastCarrierTestUs()),
             static_cast<unsigned long>(_ir.lastCarrierPulses()),
             static_cast<unsigned long>(_ir.lastReceiverEdges()));
    _server.send(202, "text/plain", result);
  });
  _server.on("/api/power-toggle", HTTP_POST, [&]() {
    const char *value = _climate.state().power ? "OFF" : "ON";
    const bool accepted = _climate.handleSetting("power", value) &&
        _climate.requestResend();
    _server.send(accepted ? 202 : 429, "text/plain",
                 accepted ? "Power command sent by infrared."
                          : "IR transmitter is busy. Please wait a moment.");
  });
  _server.on("/api/climate", HTTP_POST, [&]() {
    const bool modeOk = _climate.handleSetting("mode", _server.arg("mode").c_str());
    const bool tempOk = _climate.handleSetting(
        "target_temperature", _server.arg("target").c_str());
    const bool sent = modeOk && tempOk && _climate.requestResend();
    _server.send(sent ? 202 : 429, "text/plain",
                 sent ? "Mode and temperature sent by infrared."
                      : "Check the values or wait until the IR transmitter is ready.");
  });
  _server.on("/api/follow", HTTP_POST, [&]() {
    const float value = _server.arg("temperature").toFloat();
    const bool accepted = _climate.requestFollowMeTest(value);
    _server.send(accepted ? 202 : 429, "text/plain",
                 accepted ? "Room temperature sent by infrared."
                          : "Check the temperature or wait until the IR transmitter is ready.");
  });
  _server.on("/settings", HTTP_GET, [&]() {
    sendWebAsset(WebAssets::kSettingsMenu, WebAssets::kSettingsMenuLength, "text/html");
  });
  _server.on("/settings/wifi", HTTP_GET, [&]() {
    _network.requestScan();
    sendWebAsset(WebAssets::kWifi, WebAssets::kWifiLength, "text/html");
  });
  _server.on("/settings/advanced", HTTP_GET, [&]() {
    sendWebAsset(WebAssets::kSettings, WebAssets::kSettingsLength, "text/html");
  });
  _server.on("/settings", HTTP_POST, [&]() { handleSave(); });
  _server.on("/update", HTTP_GET, [&]() {
    sendWebAsset(WebAssets::kUpdate, WebAssets::kUpdateLength, "text/html");
  });
  _server.on("/update", HTTP_POST, [&]() {
    const bool ok = _updateFileAccepted && _updateWriteOk && !Update.hasError();
    const int status = !_updateFileAccepted ? 415 : (ok ? 200 : 500);
    _server.send(status, "text/plain",
                 !_updateFileAccepted ? "Only valid .ota.gz firmware files are accepted." :
                 (ok ? "Update complete; restarting." : "Firmware update failed."));
    if (ok) _restartAtMs = millis() + 800;
  }, [&]() { handleUpdateUpload(); });
  _server.onNotFound([&]() {
    if (_network.portalActive()) {
      _server.sendHeader("Location", String("http://") + _network.ip().toString(), true);
      _server.send(302, "text/plain", "");
    } else {
      _server.send(404, "text/plain", "Not found");
    }
  });
  _server.begin();
  Serial.println(F("[WEB] HTTP server started"));
}

void WebServerManager::loop() {
  _server.handleClient();
  if (_restartAtMs && static_cast<int32_t>(millis() - _restartAtMs) >= 0) {
    ESP.restart();
  }
}

void WebServerManager::sendWebAsset(const uint8_t *data, size_t length,
                                    const char *contentType) {
  _power.noteBrowserActivity();
  _server.sendHeader(F("Content-Encoding"), F("gzip"));
  _server.sendHeader(F("Cache-Control"), F("no-store, max-age=0"));
  _server.send_P(200, contentType, reinterpret_cast<PGM_P>(data), length);
}

void WebServerManager::handleNetworks() {
  _power.noteBrowserActivity();
  if (_server.hasArg("refresh")) _network.requestScan(true);
  const int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING || count == WIFI_SCAN_FAILED) {
    _network.requestScan();
    _server.send(200, "application/json", "{\"scanning\":true,\"networks\":[]}");
    return;
  }
  JsonDocument doc;
  doc["scanning"] = false;
  JsonArray networks = doc["networks"].to<JsonArray>();
  const int limit = min(count, 16);
  for (int i = 0; i < limit; ++i) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    bool duplicate = false;
    for (JsonObjectConst existing : networks) {
      if (existing["ssid"].as<String>() == ssid) { duplicate = true; break; }
    }
    if (duplicate) continue;
    JsonObject item = networks.add<JsonObject>();
    item["ssid"] = ssid;
    item["rssi"] = WiFi.RSSI(i);
    item["channel"] = WiFi.channel(i);
    item["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  String json;
  serializeJson(doc, json);
  _server.sendHeader("Cache-Control", "no-store");
  _server.send(200, "application/json", json);
}

void WebServerManager::handleSetup() {
  const String ssid = _server.arg("ssid");
  const String password = _server.arg("password");
  if (!ssid.length() || ssid.length() > 32 || password.length() > 64) {
    _server.send(400, "text/plain", "Invalid SSID or password length.");
    return;
  }
  SettingsData &s = _settings.data();
  strlcpy(s.wifiSsid, ssid.c_str(), sizeof(s.wifiSsid));
  strlcpy(s.wifiPassword, password.c_str(), sizeof(s.wifiPassword));
  s.standaloneMode = false;
  if (!_settings.save()) {
    _server.send(400, "text/plain", "Settings validation failed.");
    return;
  }
  _server.send(200, "application/json", "{\"success\":true,\"restarting\":true}");
  _restartAtMs = millis() + 1000;
}

void WebServerManager::handleStandalone() {
  _settings.data().standaloneMode = true;
  _settings.data().powerMode = PowerMode::AlwaysOn;
  if (!_settings.save()) {
    _server.send(400, "text/plain", "Settings validation failed.");
    return;
  }
  _server.send(200, "application/json", "{\"success\":true,\"standalone\":true}");
  _restartAtMs = millis() + 1000;
}

void WebServerManager::handleStatus() {
  _power.noteBrowserActivity();
  JsonDocument doc;
  const SettingsData &s = _settings.data();
  doc["device"] = s.deviceName;
  doc["firmware"] = MIDEAFOLLOWME_VERSION;
  doc["uptime_s"] = millis() / 1000UL;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["wifi_rssi_dbm"] = WiFi.isConnected() ? WiFi.RSSI() : 0;
  doc["ip"] = _network.ip().toString();
  doc["mqtt_connected"] = _mqtt.connected();
  if (_climate.mqttTemperatureValid())
    doc["mqtt_temperature"] = _climate.mqttTemperature();
  else
    doc["mqtt_temperature"] = nullptr;
  doc["wifi_connect_ms"] = _network.connectionDurationMs();
  doc["mqtt_connect_ms"] = _mqtt.connectionDurationMs();
  doc["last_mqtt_contact_ms"] = _mqtt.lastContactMs();
  doc["last_ir_send_ms"] = _ir.lastSendMs();
  const char *lastIrType = "none";
  switch (_ir.lastType()) {
    case IrCommandType::Climate: lastIrType = "climate"; break;
    case IrCommandType::FollowMe: lastIrType = "follow_me"; break;
    case IrCommandType::Test: lastIrType = "hardware_test"; break;
    default: break;
  }
  doc["last_ir_type"] = lastIrType;
  doc["ir_gpio"] = s.irPin;
  doc["ir_inverted"] = s.irInverted;
  doc["ir_test_count"] = _ir.hardwareTestCount();
  doc["ir_test_direct_us"] = _ir.lastDirectTestUs();
  doc["ir_test_carrier_us"] = _ir.lastCarrierTestUs();
  doc["ir_test_carrier_pulses"] = _ir.lastCarrierPulses();
  doc["ir_test_receiver_edges"] = _ir.lastReceiverEdges();
  doc["last_follow_me_ms"] = _ir.lastFollowMeMs();
  doc["room_temperature"] = _climate.state().roomTemperatureValid
      ? _climate.state().roomTemperature : static_cast<float>(NAN);
  doc["temperature_age_s"] = _climate.state().roomTemperatureValid
      ? _climate.roomTemperatureAgeMs() / 1000UL : 0;
  doc["temperature_source"] = _climate.temperatureSourceName();
  doc["local_sensor_detected"] = _climate.localSensorDetected();
  if (_climate.localTemperatureValid())
    doc["local_temperature"] = _climate.localTemperature();
  else
    doc["local_temperature"] = nullptr;
  doc["isense"] = _climate.state().iSense;
  doc["power_mode"] = static_cast<uint8_t>(s.powerMode);
  doc["wake_reason"] = _power.wakeReason();
  doc["wake_count"] = _power.wakeCount();
  doc["awake_ms"] = _power.awakeMs();
  doc["energy_saving_delay_s"] =
      (_power.energySavingDelayRemainingMs() + 999UL) / 1000UL;
  doc["deep_sleep_interval_s"] = s.deepSleepIntervalSec;
  if (isfinite(_power.batteryVoltage())) doc["battery_voltage"] = _power.batteryVoltage();
  JsonObject climate = doc["climate"].to<JsonObject>();
  _climate.toJson(climate);
  String result;
  serializeJson(doc, result);
  _server.send(200, "application/json", result);
}

void WebServerManager::handleSettingsJson() {
  const SettingsData &s = _settings.data();
  JsonDocument doc;
  doc["firmware"] = MIDEAFOLLOWME_VERSION;
  doc["mqttHost"] = s.mqttHost;
  doc["mqttPort"] = s.mqttPort;
  doc["mqttUser"] = s.mqttUser;
  doc["mqttBaseTopic"] = s.mqttBaseTopic;
  doc["haDiscovery"] = s.haDiscovery ? 1 : 0;
  doc["irPin"] = s.irPin;
  doc["irRepeats"] = s.irRepeats;
  doc["irInverted"] = s.irInverted ? 1 : 0;
  doc["coalesce"] = s.commandCoalesceMs;
  doc["followInterval"] = s.followMeIntervalSec;
  doc["tempTimeout"] = s.temperatureTimeoutSec;
  doc["tempCorrection"] = s.temperatureCorrection;
  doc["localSensorFallback"] = s.localSensorFallback ? 1 : 0;
  doc["debug"] = s.debug ? 1 : 0;
  doc["powerMode"] = static_cast<uint8_t>(s.powerMode);
  doc["sleepInterval"] = s.deepSleepIntervalSec;
  doc["maxAwake"] = s.maxAwakeSec;
  String result;
  serializeJson(doc, result);
  _server.sendHeader(F("Cache-Control"), F("no-store"));
  _server.send(200, "application/json", result);
}

void WebServerManager::handleSave() {
  SettingsData &s = _settings.data();
  auto copyArg = [&](const char *name, char *target, size_t size, bool blankAllowed = true) {
    if (!_server.hasArg(name)) return;
    const String value = _server.arg(name);
    if (blankAllowed || value.length()) strlcpy(target, value.c_str(), size);
  };
  copyArg("wifiSsid", s.wifiSsid, sizeof(s.wifiSsid));
  copyArg("wifiPassword", s.wifiPassword, sizeof(s.wifiPassword), false);
  copyArg("mqttHost", s.mqttHost, sizeof(s.mqttHost));
  copyArg("mqttUser", s.mqttUser, sizeof(s.mqttUser));
  copyArg("mqttPassword", s.mqttPassword, sizeof(s.mqttPassword), false);
  copyArg("mqttBaseTopic", s.mqttBaseTopic, sizeof(s.mqttBaseTopic));
  s.mqttPort = _server.arg("mqttPort").toInt();
  s.irPin = _server.arg("irPin").toInt();
  s.irRepeats = _server.arg("irRepeats").toInt();
  s.irInverted = _server.arg("irInverted") == "1";
  s.debug = _server.arg("debug") == "1";
  s.haDiscovery = _server.arg("haDiscovery") == "1";
  if (_server.hasArg("standalone"))
    s.standaloneMode = _server.arg("standalone") == "1";
  s.commandCoalesceMs = _server.arg("coalesce").toInt();
  s.followMeIntervalSec = _server.arg("followInterval").toInt();
  s.temperatureTimeoutSec = _server.arg("tempTimeout").toInt();
  s.temperatureCorrection = _server.arg("tempCorrection").toFloat();
  s.localSensorFallback = _server.arg("localSensorFallback") == "1";
  const int mode = _server.arg("powerMode").toInt();
  if (mode >= 0 && mode <= 2) s.powerMode = static_cast<PowerMode>(mode);
  s.deepSleepIntervalSec = _server.arg("sleepInterval").toInt();
  s.maxAwakeSec = _server.arg("maxAwake").toInt();
  if (!_settings.save()) {
    _server.send(400, "text/plain", "Settings rejected. Check serial log for validation errors.");
    return;
  }
  _server.send(200, "application/json", "{\"success\":true,\"restarting\":true}");
  _restartAtMs = millis() + 800;
}

void WebServerManager::handleUpdateUpload() {
  HTTPUpload &upload = _server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[WEB] OTA upload: %s\n", upload.filename.c_str());
    String filename = upload.filename;
    filename.toLowerCase();
    _updateFileAccepted = filename.endsWith(".ota.gz");
    _updateHeaderChecked = false;
    _updateWriteOk = false;
    if (!_updateFileAccepted) {
      Serial.println(F("[WEB] OTA rejected: filename must end in .ota.gz"));
      return;
    }
    _updateWriteOk = Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (_updateFileAccepted && !_updateHeaderChecked) {
      _updateHeaderChecked = true;
      if (upload.currentSize < 2 || upload.buf[0] != 0x1F || upload.buf[1] != 0x8B) {
        Serial.println(F("[WEB] OTA rejected: missing GZIP header"));
        Update.end(false);
        _updateFileAccepted = false;
        _updateWriteOk = false;
        return;
      }
    }
    if (_updateFileAccepted && _updateWriteOk &&
        Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      _updateWriteOk = false;
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (_updateFileAccepted && _updateWriteOk) {
      _updateWriteOk = Update.end(true);
      if (!_updateWriteOk) Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (_updateFileAccepted) Update.end(false);
    _updateWriteOk = false;
  }
}
