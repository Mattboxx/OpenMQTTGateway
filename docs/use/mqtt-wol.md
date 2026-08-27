# MQTT outage Wake-on-LAN

## Why this feature exists

An OpenMQTTGateway is often installed away from the computer that runs its
MQTT broker: in a garage, shed, gate controller or remote equipment cabinet.
If that computer is asleep or has stopped responding, the gateway is one of
the devices that can still detect the outage. Optional MQTT outage
Wake-on-LAN (WOL) lets an ESP32 send a magic packet after a configurable series
of connection failures.

This is intentionally different from sending WOL after every disconnect. WiFi
roaming, a broker restart, invalid credentials and a powered-off server are
not the same event. The gateway classifies the MQTT connection result, waits
for both a failure threshold and an outage delay, then enforces a repeat
interval. A successful MQTT connection clears all outage timers and counters.
This avoids the unsolicited packets and stale timers common in simple WOL
implementations.

Typical uses include:

* waking a Home Assistant, NAS or small server that also hosts MQTT;
* recovering a remote installation without an always-on second controller;
* combining RF reception and local dry-contact monitoring on one ESP32;
* diagnosing intermittent WiFi, MQTT authentication or queue-pressure issues.

## Scope and compatibility

WOL is an opt-in ESP32 feature enabled with `MQTT_WOL_ENABLED`. It is not tied
to one OpenMQTTGateway model and can be added to another ESP32 environment with
a build flag. When the flag is absent, the WOL UI, state machine and Home
Assistant entities are not compiled.

The `esp32dev-multi_receiver-wol-gpio` environment is a ready-to-build example
for an ESP32 Dev Module with a CC1101. It extends the official
`esp32dev-multi_receiver` environment rather than replacing it. Consequently,
the original preset and all other OpenMQTTGateway boards remain unchanged.

The example was created for a garage deployment that needed all three of these
capabilities at once:

* RF, RF2 and RTL_433 reception through a CC1101;
* up to four named contact inputs for a garage door, gate or similar sensors;
* controlled WOL recovery when the MQTT host is unreachable.

It is based on OpenMQTTGateway 1.8.1 because that release was stable on the
target ESP32/CC1101 hardware. The changes remain individually optional and are
not assumptions imposed on the other project environments.

## Build the example environment

From the repository root:

```text
platformio run -e esp32dev-multi_receiver-wol-gpio
```

The application image is written to:

```text
.pio/build/esp32dev-multi_receiver-wol-gpio/firmware.bin
```

The inherited CC1101 wiring is CS 5, GDO0 12 and GDO2 27. The first contact
input defaults to GPIO 4 in driven `INPUT` mode, preserving the original garage
sensor configuration. Electrical mode, active level, debounce and Home
Assistant type can be selected independently for every channel. Review the
[GPIO input configuration](sensors.md#gpio-input) before connecting additional
sensors.

## Configure WOL

Open **Configuration > MQTT** in the WebUI. The WOL section contains:

* the destination MAC address and an enable switch;
* the initial outage delay;
* the minimum number of consecutive MQTT failures;
* the repeat interval, where `0` means only once per outage;
* independent triggers for transport/TLS errors, broker rejection and
  authentication/authorization errors.

WOL is disabled and the destination MAC is empty on a fresh installation. The
safe default enables only transport/TLS failures as a trigger. Authentication
failures normally indicate incorrect credentials and therefore should not wake
a computer continuously.

The same values can be persisted over MQTT:

```json
{
  "mqtt_wol_enabled": true,
  "mqtt_wol_mac": "AA:BB:CC:DD:EE:FF",
  "mqtt_wol_delay_s": 60,
  "mqtt_wol_failures": 3,
  "mqtt_wol_repeat_s": 1200,
  "mqtt_wol_transport": true,
  "mqtt_wol_broker": false,
  "mqtt_wol_auth": false,
  "save": true
}
```

Publish the object to `home/<gateway>/commands/MQTTtoSYS/config`. With MQTT
discovery enabled, Home Assistant also exposes **WOL: Destination MAC** and
**WOL: Enabled** on the gateway device.

::: warning Network requirement
The magic packet is sent to `255.255.255.255` on UDP port 9. The target network
must allow local broadcast WOL, and the target computer must have WOL enabled.
Routers generally do not forward this packet to another subnet.
:::

## Connection resilience and diagnostics

The example preset uses a unique MQTT client-ID suffix derived from the ESP32
MAC, a five-second reconnect interval, a 60-second keepalive and a ten-second
socket timeout. WiFi automatic reconnect is enabled and power saving is
disabled. It also avoids restarting the ESP32 solely because the broker remains
offline, preserving RF and contact-input operation during a long outage.

These are preset choices, not global defaults. They can be adapted in a custom
environment if lower power consumption or watchdog restarts are preferred.

Logs use stable prefixes: `[WIFI]`, `[MQTT]`, `[WOL]`, `[GPIO]`, `[QUEUE]`,
`[RF][CC1101]`, `[WebUI][OTA]` and `[DIAG]`. They include reconnect causes,
timers, queue pressure and memory information, but never print passwords,
private keys or certificate contents.

The implementation also preserves an existing MQTT password when a WebUI or
onboarding password field is submitted empty. Fixed-size configuration strings
are bounds checked, and the actual MQTT CONNACK result is retained so WOL can
distinguish transport, broker and authentication failures without opening a
second probe connection.

## Validation status

The example compiles for its dedicated environment and has been smoke-tested
on an ESP32-D0WD-V3 with a CP2102 interface and CC1101 at 433.92 MHz. WiFi,
authenticated MQTT, RF initialization, retained GPIO state and the WebUI were
verified. A long-duration RF soak test and a deliberately induced end-to-end
MQTT outage/WOL cycle are still recommended before relying on it in an
unattended installation.
