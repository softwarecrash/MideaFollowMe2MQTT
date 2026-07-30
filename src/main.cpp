#include <Arduino.h>
#include <ArduinoOTA.h>

#include "Climate/ClimateController.h"
#include "Ir/PortaSplitIrController.h"
#include "Mqtt/MqttManager.h"
#include "Network/NetworkManager.h"
#include "Power/PowerManager.h"
#include "Settings/Settings.h"
#include "Web/WebServerManager.h"

Settings settings;
PortaSplitIrController irController;
ClimateController climate(settings.data(), irController);
PowerManager power(settings.data());
NetworkManager network(settings.data());
MqttManager mqtt(settings.data(), climate, power, irController);
WebServerManager web(settings, network, mqtt, climate, power, irController);

namespace {
bool otaStarted = false;
bool sleepCommitted = false;
WakePhase previousPhase = WakePhase::ConnectWifi;
uint32_t observedIrSend = 0;

void startOta() {
  if (otaStarted || !network.connected() || power.deepSleepMode()) return;
  ArduinoOTA.setHostname(settings.data().deviceName);
  ArduinoOTA.onStart([]() { Serial.println(F("[OTA] Update started")); });
  ArduinoOTA.onEnd([]() { Serial.println(F("[OTA] Update complete")); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("[OTA] Error %u\n", error); });
  ArduinoOTA.begin();
  otaStarted = true;
  Serial.println(F("[OTA] ArduinoOTA ready"));
}
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.printf("[BOOT] PortaSplit2MQTT %s (%s %s)\n", PORTASPLIT_VERSION, __DATE__, __TIME__);

  const bool validSettings = settings.begin();
  const bool needsSetup = !validSettings ||
      (!settings.data().standaloneMode && !settings.data().wifiSsid[0]);
  power.begin(needsSetup);
  if (needsSetup) power.setMaintenance(true);

  irController.begin(settings.data());
  climate.begin();
  mqtt.begin();
  network.begin(power.maintenanceMode());
  web.begin();
  previousPhase = power.phase();
}

void loop() {
  network.loop();
  web.loop();
  mqtt.loop(network.connected());
  startOta();
  if (otaStarted) ArduinoOTA.handle();

  power.loop(network.connected(), mqtt.connected(), climate.hasPendingWork());
  if (power.phase() != previousPhase) {
    Serial.printf("[POWER] Wake phase %u -> %u\n",
                  static_cast<unsigned>(previousPhase), static_cast<unsigned>(power.phase()));
    if (power.phase() == WakePhase::Execute) {
      climate.prepareWakeWork(power.climateChanged(climate.state()));
    }
    previousPhase = power.phase();
  }

  if (!power.deepSleepMode() || power.phase() == WakePhase::Execute ||
      power.phase() == WakePhase::Publish)
    climate.loop();

  if (irController.lastSendMs() != observedIrSend) {
    observedIrSend = irController.lastSendMs();
    power.rememberClimate(climate.state(), climate.state().roomTemperature,
        irController.lastType() == IrCommandType::FollowMe
            ? LastSendReason::FollowMe : LastSendReason::Changed);
    mqtt.publishState(true);
  }

  if (power.shouldSleep() && !sleepCommitted) {
    sleepCommitted = true;
    mqtt.publishDiagnostics();
    mqtt.disconnect();
    network.stop();
    power.enterDeepSleep();
  }
  yield();
}
