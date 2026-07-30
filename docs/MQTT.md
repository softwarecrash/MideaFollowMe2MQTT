# MQTT topics

The default base is `mideafollowme/mideafollowme`; all examples below use
`mideafollowme/<device>`.

## Commands

| Topic suffix | Payload |
|---|---|
| `set/power` | `ON`, `OFF` |
| `set/mode` | `auto`, `cool`, `heat`, `dry`, `fan_only`, `off` |
| `set/target_temperature` | integer `17`–`30` |
| `set/fan` | `auto`, `low`, `medium`, `high` |
| `set/swing` | `off`, `vertical` |
| `set/turbo`, `set/sleep`, `set/silent`, `set/display`, `set/isense` | boolean |
| `set/room_temperature` | decimal Celsius |
| `set` | JSON object using the same keys |
| `set/maintenance_mode` | retained `ON`/`OFF` |
| `command/resend` | `{"id":184,"command":"resend"}` in DeepSleep |
| `command/send_test` | `{"id":185,"command":"send_test"}` in DeepSleep |
| `command/restart` | any payload |

For DeepSleep, retain every persistent `set/*` value. One-shot commands require
a non-zero ID. The last executed ID is held in validated RTC memory and an
acknowledgement is published to `command/ack`.

## State

- `availability`: retained `online`, `maintenance`, or LWT `offline`
- `state`: retained JSON state with `assumed_state:true`
- `state/power`, `state/mode`, `state/target_temperature`
- `state/room_temperature`, `state/room_temperature_valid`
- `state/temperature_source`: `mqtt`, `local_ds18b20`, or `none`
- `state/local_sensor_detected`, `state/local_temperature`
- `state/fan`, `state/swing`, `state/isense`, `state/power_mode`
- `status`: firmware, build, chip/reset, heap/uptime, wake and battery JSON

Additional timestamps are present in the JSON state. Since IR is unidirectional,
state means “last state managed/sent by the ESP,” not feedback from the AC.

MQTT temperature has priority in network mode. If it expires, an enabled and
valid local DS18B20 fallback takes over. Standalone mode always uses the local
sensor and does not require MQTT.

Example:

```sh
mosquitto_pub -r -t mideafollowme/living/set/power -m ON
mosquitto_pub -r -t mideafollowme/living/set/mode -m cool
mosquitto_pub -r -t mideafollowme/living/set/target_temperature -m 22
mosquitto_pub -r -t mideafollowme/living/set/room_temperature -m 23.4
```
