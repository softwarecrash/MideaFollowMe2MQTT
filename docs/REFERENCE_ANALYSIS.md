# Reference analysis

Analysis date: 2026-07-30. The default branches and latest commits were inspected,
not copied wholesale.

| Project | Observed role | Useful patterns | Patterns not adopted |
|---|---|---|---|
| Solar2MQTT | Newest architecture; ESP32-only | Small core services, separate web assets, settings schema, MQTT handler, state snapshot | ESP32 Preferences, STL-heavy runtime paths, ESP32-only network/OTA code |
| Victron2MQTT | Current ESP8266 family member | ESP8266 PlatformIO 4.2.1, captive setup, RTC-memory precedent, WebSerial/OTA conventions | Monolithic `main.cpp`, blocking setup manager, unvalidated raw EEPROM layout |
| EPEVER2MQTT | Mature ESP8266 deployment | PubSubClient, ArduinoJson 7, web settings, MQTT discovery conventions | Large callback/functions, device-specific Modbus code |
| Daly2MQTT | Older/smaller ESP8266 baseline | Familiar UI vocabulary, status and discovery topics | ArduinoJson 6, synchronous connection assumptions, tightly coupled globals |

The implementation therefore uses Solar2MQTT's service-oriented layout as the
main structural reference and the ESP8266 toolchain/library choices from
Victron2MQTT and EPEVER2MQTT. It deliberately replaces blocking `autoConnect()`
with a millis-driven connection process and adds CRC/version validation to both
EEPROM and RTC state.

IRremoteESP8266 2.9.0 provides `IRMideaAC`, complete Midea state frames,
`setSensorTemp()`/`setEnableSensorTemp()` Follow-Me support, and ESP8266
transmission. Its own class documentation calls Midea support alpha. The
supported-device list includes several Midea/Comfee portable units but does not
name PortaSplit. No PortaSplit-specific raw code is invented here.

Project-specific new work comprises the climate domain model, command
coalescing, prioritized IR scheduler, Follow-Me heartbeat/timeout logic,
deep-sleep wake state machine, duplicate command-ID protection, and assumed
state MQTT climate mapping.

