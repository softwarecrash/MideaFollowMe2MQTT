# PortaSplit2MQTT

Standalone ESP8266 firmware that controls a Midea PortaSplit-compatible air
conditioner over infrared and exposes an assumed climate state through MQTT.
It also transmits an external room temperature using Midea Follow-Me/iSense.
ESPHome is not used.

> **Compatibility status:** IRremoteESP8266 supports the Midea frames used here,
> including Follow-Me, but its supported-device list does not explicitly name
> PortaSplit. No command in this project is proof that the appliance received or
> applied it. Real PortaSplit hardware verification is still required.

## Features

- ESP8266/Wemos D1 mini PlatformIO target
- non-blocking WiFi reconnect, captive setup AP, MQTT reconnect and LWT
- complete Midea climate state with a 250 ms command coalescing window
- priority-controlled climate and Follow-Me sends with minimum packet spacing
- external temperature correction, range validation, timeout and heartbeat
- retained state topics and optional optimistic Home Assistant discovery
- status/configuration/IR-test UI, browser OTA and ArduinoOTA
- CRC- and version-protected EEPROM settings
- AlwaysOn, ModemSleep and state-machine-driven DeepSleep modes
- CRC-protected RTC state, unchanged-state suppression and command-ID deduplication
- optional calibrated A0 voltage reading; no universal battery percentage claim
- native unit tests for parsing and hardware-independent climate helpers

Design notes and the required reference review are in
[docs/REFERENCE_ANALYSIS.md](docs/REFERENCE_ANALYSIS.md) and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build and flash

Install PlatformIO, connect a D1 mini, then run:

```sh
pio run -e d1_mini
pio run -e d1_mini -t upload
pio device monitor -b 115200
```

The verified build uses Espressif8266 platform 4.2.1 / Arduino core 3.1.2.
Firmware output is `.pio/build/d1_mini/firmware.bin`.

Every successful PlatformIO build also exports two named artifacts to
`.firmware/`:

- `PortaSplit2MQTT_<environment>_V<version>.bin` for serial flashing
- `PortaSplit2MQTT_<environment>_V<version>.bin.ota.gz` for the web updater

The OTA artifact is deterministically gzip-compressed at the highest compression
level. The ESP8266 bootloader decompresses it while installing the update. The
web updater intentionally accepts only valid gzip files ending in `.ota.gz`;
selecting a normal serial `.bin` or merely renaming one is rejected.

### Editing the web interface

The web interface is maintained as normal source files in `web/`:

- `index.html` — dashboard and IR test
- `wifi.html` — captive setup and Wi-Fi manager
- `settings-menu.html` and `settings.html` — configuration
- `update.html` — browser OTA
- `style.css` — shared BambuBeacon-style presentation

Do not hand-edit a generated C++ byte array. Before every PlatformIO build,
`tools/embed_web.py` reads these files, gzip-compresses each one deterministically
and writes `WebAssets.h` inside the environment's `.pio/build/` directory. The
firmware streams those compressed assets directly from flash with the
`Content-Encoding: gzip` response header. Dynamic status and form values are
loaded separately from JSON endpoints, so the HTML itself remains static,
editable and compressible.

In Standalone mode MQTT stays disabled, but configuration remains recoverable:
after every reset the setup AP is available for ten minutes before the Wi-Fi
radio is switched off. No button or dedicated maintenance pin is required.

### ESP_IR_TR_V1.1 / ESP-01M

The compact ESP8285 transceiver board has its own 1 MB build:

```sh
pio run -e esp_ir_tr_v1_1
pio run -e esp_ir_tr_v1_1 -t upload --upload-port COM5
```

Its firmware is generated at
`.pio/build/esp_ir_tr_v1_1/firmware.bin`. This environment uses PlatformIO's
`esp8285` board, DOUT flash mode, a 1 MB layout with only 64 KB reserved for the
unused filesystem (leaving room for OTA staging), and a conservative 115200
baud upload speed.

Board wiring is fixed by the PCB:

| Function | ESP8285 pin |
|---|---:|
| IR transmitter | GPIO4 |
| IR receiver (reserved for a later capture feature) | GPIO14 |
| Flash-mode input | GPIO0 |
| Supply | 5 V board input |

GPIO14 remains reserved for the later IR receiver/capture feature. GPIO0 is used
only for serial flashing; holding it low during reset selects the ROM bootloader.

To flash through the board header, use a **3.3 V logic-level** USB-UART adapter,
connect GND/RX/TX, hold GPIO0 low during reset, upload, then release GPIO0 and
reset normally. Power the assembled board according to its marked 5 V input;
do not apply 5 V logic to ESP8285 RX/TX.

The common ESP_IR_TR_V1.1 programming header does not expose GPIO16 and RST as
a ready-made wake pair. Use AlwaysOn or ModemSleep unless those ESP-01M signals
are made accessible with a verified hardware modification; selecting DeepSleep
without a GPIO16-to-RST connection will not provide timed wake-up.

On the first boot (or invalid configuration), connect to
`PortaSplit2MQTT-AP` and open `http://192.168.4.1/`. The captive setup wizard
lists nearby networks and lets you select one and enter its password. If a saved
network cannot be reached, the setup AP starts automatically while the ESP keeps
retrying the saved network every ten seconds. As soon as it becomes reachable,
the ESP connects and closes the setup AP automatically.

## Wiring

Do not drive a high-power IR LED directly from an ESP8266 GPIO. Use a ready-made
3.3 V-compatible IR transmitter with a transistor driver, or wire one:

```text
D1 mini D2 / GPIO4 ---- resistor ---- transistor base/gate
D1 mini GND ------------------------- module/transistor GND
5 V or 3.3 V (module-dependent) ----- IR module VCC
transistor output ------------------- IR LED + series resistor
```

The default IR output is D2/GPIO4 and can be changed in the UI. Confirm the
module's active polarity and supply voltage before connecting it. All grounds
must be common.

For timed DeepSleep wake, connect **D0/GPIO16 to RST**. GPIO16 is rejected as an
IR output.

## MQTT

Full topic and payload documentation is in [docs/MQTT.md](docs/MQTT.md).
A minimal retained setup:

```sh
mosquitto_pub -r -t portasplit/living/set/power -m ON
mosquitto_pub -r -t portasplit/living/set/mode -m cool
mosquitto_pub -r -t portasplit/living/set/target_temperature -m 22
mosquitto_pub -r -t portasplit/living/set/fan -m auto
mosquitto_pub -r -t portasplit/living/set/swing -m off
mosquitto_pub -r -t portasplit/living/set/isense -m ON
mosquitto_pub -r -t portasplit/living/set/room_temperature -m 23.4
```

Follow-Me is sent immediately after a meaningful change and periodically even
when the value is unchanged. The default heartbeat is 120 seconds, timeout is
10 minutes, immediate threshold is 0.5 °C, and the library-supported sensor
range is 0–37 °C. Conversion to the integer protocol value is centralized and
rounds half upward.

The IRremoteESP8266 API has no public Follow-Me beep setter. Enabling that
setting is therefore rejected rather than modifying guessed raw bits.

## Power modes

### AlwaysOn

WiFi/MQTT, web and OTA remain available. Follow-Me defaults to 120 seconds.

### ModemSleep

Uses the ESP8266 Arduino core's `WIFI_MODEM_SLEEP`; MQTT, web and OTA remain
available. Savings are much smaller than DeepSleep and depend on access-point
DTIM and traffic.

### DeepSleep

Default wake cycle: WiFi timeout 15 s, MQTT timeout 10 s, retained receive
window 2 s, maximum awake time 30 s, sleep 120 s. It runs:

```text
WiFi -> MQTT -> retained window -> validate/compare RTC -> IR work
     -> publish -> disconnect -> DeepSleep
```

While asleep there is no WiFi/MQTT connection and no web or OTA access.
Commands are processed on the next wake, so persistent set topics must be
retained. One-shot commands require a JSON ID. A retained
`set/maintenance_mode=ON` keeps the next wake online for configuration/OTA.
The MQTT flag can only be read on a scheduled wake.

Fast Connect keeps channel/BSSID in RTC memory and falls back to a normal
association after five seconds. Normal RAM state is not trusted across sleep:
RTC data has a magic, version, size and CRC. The last climate state, last room
temperature, wake counter, Follow-Me wake and last command ID are retained
without repeated flash writes.

Suggested profiles:

| Profile | Mode | Wake | Behavior |
|---|---|---:|---|
| Mains | AlwaysOn | — | immediate MQTT, web/OTA always available |
| Battery/responsive | DeepSleep | 120 s | Follow-Me each wake, climate only if changed |
| Battery/long life | DeepSleep | 300 s | enable Fast Connect, retained state required |

## Battery measurement

It is disabled by default. A bare ESP8266 ADC usually accepts only 0–1.0 V;
development boards such as a D1 mini may contain a divider and have a different
A0 board-level maximum. Verify the exact board schematic. Use an external
divider where needed and account for its quiescent current.

`batteryFactor` is the voltage represented by a full-scale ADC reading (1.0 for
a bare 0–1 V ADC, approximately the documented A0 full-scale value for a board
with divider). Eight quick samples are averaged. Percentage reporting stays
disabled until empty/full voltages are configured; a linear estimate is not a
chemistry-accurate state of charge. At critical voltage the longer configured
sleep interval is used to avoid a rapid battery-draining wake loop.

## Tests

```sh
pio test -e native
```

The host needs a C/C++ compiler in `PATH`. Tests cover boolean/MQTT payload
parsing, enum conversion, correction/clamping/rounding, rollover-safe timeout,
topic construction and state validation. The ESP8266 firmware build remains the
authoritative integration compile.

Hardware test procedure:

1. First test with the IR LED viewed through a phone camera.
2. Open the web IR-test page and send a known climate state.
3. Place the transmitter close to the PortaSplit receiver and test power/mode.
4. Subscribe to `<base>/#`; publish retained room temperature and verify the
   immediate and 120-second Follow-Me logs.
5. Stop temperature updates and confirm `room_temperature_valid=false` after
   timeout with no further Follow-Me sends.
6. For DeepSleep, wire D0 to RST, use retained topics, and verify unchanged
   climate state is not resent while Follow-Me is.
7. Capture the original remote with IRrecvDumpV3 in a separate debug build and
   compare protocol, raw timings and decoded Midea data before enabling
   experimental toggles.

## Still requiring real PortaSplit verification

- base Midea power, cool/heat/auto/dry/fan-only and 17–30 °C frames
- Follow-Me frame acceptance, integer rounding and required heartbeat interval
- fan levels
- vertical swing (a Midea toggle, so physical state can drift)
- Turbo and display/light (Midea toggle commands)
- Sleep
- Quiet/Silent (library-supported but model-dependent)
- repeat count, transmitter placement and reliable optical range
- whether Follow-Me is silent on this remote/unit

The optional IR receiver is intentionally not coupled to climate state or the
transmitter. A later capture service can be added beside
`PortaSplitIrController` without changing MQTT or climate validation.

## Project layout

```text
src/
  Climate/   ClimateController, PortaSplitState
  Ir/        PortaSplitIrController
  Mqtt/      MqttManager, topics
  Network/   NetworkManager
  Power/     PowerManager, validated RTC state
  Settings/  validated EEPROM settings
  Web/       server and embedded pages
  config.h
  main.cpp
test/test_climate/
docs/
```
