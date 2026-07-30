#include <Arduino.h>
#include <ArduinoOTA.h>

#include "Climate/ClimateController.h"
#include "Ir/MideaIrController.h"
#include "Mqtt/MqttManager.h"
#include "Network/NetworkManager.h"
#include "Power/PowerManager.h"
#include "Sensor/LocalTemperatureSensor.h"
#include "Settings/Settings.h"
#include "Web/WebServerManager.h"

Settings settings;
MideaIrController irController;
LocalTemperatureSensor localTemperatureSensor;
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
uint32_t observedSensorRevision = UINT32_MAX;

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
  Serial.printf("[BOOT] MideaFollowMe2MQTT %s (%s %s)\n", MIDEAFOLLOWME_VERSION, __DATE__, __TIME__);

  const bool validSettings = settings.begin();
  const bool needsSetup = !validSettings ||
      (!settings.data().standaloneMode && !settings.data().wifiSsid[0]);
  power.begin(needsSetup);
  if (needsSetup) power.setMaintenance(true);

  irController.begin(settings.data());
  climate.begin();
  localTemperatureSensor.begin(Config::kLocalTemperaturePin);
  mqtt.begin();
  network.begin(power.maintenanceMode());
  web.begin();
  previousPhase = power.phase();
}

void loop() {
  localTemperatureSensor.loop();
  if (localTemperatureSensor.revision() != observedSensorRevision) {
    observedSensorRevision = localTemperatureSensor.revision();
    climate.updateLocalTemperature(
        localTemperatureSensor.detected(),
        localTemperatureSensor.readingValid(),
        localTemperatureSensor.temperature());
  }
  network.loop(power.energySavingAllowed());
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
