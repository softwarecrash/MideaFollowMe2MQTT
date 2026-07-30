# Architecture

```text
MQTT/Web command
      |
      v
ClimateController -- validates/coalesces --> PortaSplitIrController
      |                                      | climate (priority)
      |                                      ` Follow-Me
      v
assumed state --> MqttManager --> state topics / HA discovery

Settings (EEPROM + CRC) --> Network / MQTT / IR / Power
PowerManager (RTC + CRC) --> wake FSM --> centralized deep sleep
WebServerManager --> status, configuration, IR test, firmware upload
```

- `Settings`: one versioned configuration structure; validates before use/save.
- `NetworkManager`: station reconnect, fast BSSID/channel attempt with fallback,
  captive DNS/AP, and ESP8266 modem-sleep selection.
- `MqttManager`: LWT, subscriptions, reconnect, small dispatch handlers, state
  and diagnostics, optional Home Assistant discovery.
- `ClimateController`: sole command parser/state owner. Individual and JSON
  commands share the same setters.
- `PortaSplitIrController`: the only Midea library boundary. Climate sends have
  priority; Follow-Me is separate and uses the library's native message type.
- `PowerManager`: battery sampling, maintenance decision, RTC state, command-ID
  deduplication, wake timeouts, and the only call to `ESP.deepSleep()`.
- `WebServerManager`: JSON APIs, routes for the English status/config/test UI,
  and browser firmware upload. Editable files in `web/` are deterministically
  gzip-compressed into a generated flash-resident header before compilation.

In DeepSleep, retained commands are collected during a bounded window. The RTC
copy decides whether the climate state changed; Follow-Me can still be sent on
each wake. The main controller only lets climate work run in the Execute/Publish
phases and sleeps even after connection failures or maximum-wake timeout.
