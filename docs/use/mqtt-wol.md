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

The `esp32dev-multi_receiver-wol-gpio-ble` and
`esp32dev-multi_receiver-wol-gpio-no-ble` environments are ready-to-build
examples for an ESP32 Dev Module with a CC1101. They extend the official
`esp32dev-multi_receiver` environment rather than replacing it. Consequently,
the original preset and all other OpenMQTTGateway boards remain unchanged.

The example was created for a garage deployment that needed all four of these
capabilities at once:

* RF, RF2 and RTL_433 reception through a CC1101;
* two named contact inputs for a garage door, gate or similar sensors;
* two configurable outputs controlled as Home Assistant switches;
* controlled WOL recovery when the MQTT host is unreachable;
* up to four explicitly selected fixed-MAC BLE presence devices.

It is based on OpenMQTTGateway 1.8.1 because that release was stable on the
target ESP32/CC1101 hardware. The changes remain individually optional and are
not assumptions imposed on the other project environments.

## Build the example environment

From the repository root:

```text
platformio run -e esp32dev-multi_receiver-wol-gpio-ble
platformio run -e esp32dev-multi_receiver-wol-gpio-no-ble
```

The application image is written to:

```text
.pio/build/esp32dev-multi_receiver-wol-gpio-ble/firmware.bin
.pio/build/esp32dev-multi_receiver-wol-gpio-no-ble/firmware.bin
```

Two deliberately separate, descriptively named firmware variants are retained:
`WOL + 2 IN + 2 OUT + NO BLE` and `WOL + 2 IN + 2 OUT + BLE`. They use the same
code, RF/CC1101 modules, WOL policy, two GPIO inputs, two GPIO outputs, Home
Assistant discovery, WebUI, OTA and recovery safeguards. The only functional
difference is that the BLE build adds the selected-device observer. Its passive
scan duty cycle balances intermittent-beacon detection with MQTT and WebUI
responsiveness, and stalled scans are restarted automatically.

Each GitHub release contains a complete Windows USB ZIP for the first
installation. The ZIP includes standalone esptool, bootloader, partitions, OTA
data, firmware and an automatic COM-port selection script; PlatformIO and
Python are not needed. The separately named application `.bin` is for WebUI
updates only after one of these custom builds is already installed. The
original OpenMQTTGateway 1.8.1 WebUI cannot upload a local file. Local uploads
in this edition yield between flash blocks and restart only after the HTTP
response has closed, avoiding fast-LAN upload panics and incomplete result
pages. Disabled GPIO and BLE slots have their retained Home Assistant discovery
entries removed automatically.
BLE presence entities intentionally omit MQTT availability so their state is
always present/away instead of briefly becoming unavailable during a gateway
restart. The last retained state survives the restart and changes to away only
after a complete configured timeout with the scanner running and no match.
Home Assistant receives the same timeout as `off_delay`, so it can still change
the entity to away if the gateway itself stops publishing entirely.

The current image uses two inputs plus two outputs. Outputs default to disabled
and OFF, are limited to output-capable non-CC1101 pins, and expose independent
Home Assistant switches when enabled. Logical ON/OFF, active-level inversion,
retained state, restart restoration and Home Assistant discovery were validated
without an attached load; the connected circuit must still be checked for the
ESP32's 3.3 V limits before enabling an output.

A 60-second startup guard runs independently while the WebUI and RF modules are
initialized. If that phase stalls after a warm restart, the gateway records
requested restart reason `10` and automatically reboots instead of remaining
pingable with MQTT and HTTP unavailable. The guard is disarmed and releases its
task as soon as normal setup completes.

The inherited CC1101 wiring is CS 5, GDO0 12 and GDO2 27. The first contact
input defaults to GPIO 4 in driven `INPUT` mode, preserving the original garage
sensor configuration. Electrical mode, active level, debounce and Home
Assistant type can be selected independently for both input channels. Outputs
default to GPIO 16 and 17 but remain electrically disabled until explicitly
enabled. Review the [GPIO input and output configuration](sensors.md#gpio-input)
before connecting sensors or loads.

## Configure selected BLE presence

Open **Device configuration > BLE presence devices**. The page exposes four
independent slots. For each slot you can:

* choose a recently observed MAC from the suggestions, or enter one manually;
* assign the friendly name used by Home Assistant;
* set the away timeout from 5 seconds to 24 hours;
* reject weak advertisements with a minimum RSSI threshold;
* enable or disable the Home Assistant entities without rebooting.

An enabled slot publishes a retained presence state and RSSI on
`home/<gateway>/BTtracker/<slot>`. MQTT discovery creates one presence binary
sensor and one signal-strength sensor. A detection sets presence immediately;
if no accepted advertisement arrives before the configured timeout, it changes
to away. The bounded candidate list, raw-report queue and drop counter prevent a
busy radio environment from consuming memory without limit.

This is deliberately not the full OpenMQTTGateway BLE decoder. The preset uses
the ESP32 controller's VHCI interface directly and accepts legacy advertising
reports only. It never connects to a peripheral and does not compile a BLE host,
GATT client or sensor decoder. That narrower design leaves enough flash and RAM
for RTL_433, CC1101 reception, MQTT, the Web UI and dual-slot OTA on the target
ESP32.

Reliable MAC tracking requires a beacon or tag whose BLE address remains fixed.
Many phones and privacy-oriented devices rotate random addresses and therefore
cannot be followed reliably by MAC alone.

The Bluetooth controller is initialized before WiFi and the memory-heavy RF
decoder tasks. The potentially blocking ESP-IDF initialization call is covered
by the ESP32 task watchdog and an RTC guard: if it ever fails to complete, the
next boot disables only the BLE observer and keeps WiFi, MQTT, RF, GPIO and the
WebUI available. Routine scan/detection diagnostics are verbose-level logs;
warnings and failures remain visible at the standard log level.

## Install a local firmware image

This is an update path, not the initial installation method. First install the
custom edition with its complete USB ZIP. Once this edition is running, open
**Firmware Upgrade** and use the local `.bin` upload form for later releases.
The stock OpenMQTTGateway 1.8.1 firmware does not contain this form.

The handler uses the same Web UI authentication, rejects non-ESP32 application
images, writes the image to the inactive OTA slot and restarts only after a
complete successful upload. BLE processing is stopped first to return
controller memory to the OTA operation. The online URL-based update path
remains available separately; only its automatic HTTPS manifest lookup at
startup is disabled for this dense RF-plus-BLE preset.

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

At startup the preset calls `WiFi.begin()` once and gives association plus DHCP
an uninterrupted 30-second window. Repeating `WiFi.begin()` every second can
restart that process before it completes. If saved WiFi still fails, the
password-protected `OMG_multi_receiver` recovery portal remains enabled for the
configured portal timeout even when `config.json` exists. This avoids a reboot
loop in which the gateway has no station IP, no MQTT connection and no visible
configuration access point.

Runtime losses use the same bounded strategy. Three complete 30-second
reconnect windows are attempted; if all fail, the firmware shuts the WiFi radio
down cleanly and performs a software restart into the startup recovery path.
Non-BLE builds use an OFF-to-STA transition to avoid reusing a half-open driver
or association state left by a warm reboot. The BLE presence preset instead
keeps the shared WiFi/Bluetooth controller in its coexistence-safe STA path.
Saved credentials are not erased.

For post-mortem diagnosis, system state includes `reset_reason`,
`requested_restart_reason`, `wifi_disconnects` and
`wifi_last_disconnect_reason`. The requested reason is kept in ESP32 RTC memory
only across an application-requested restart and is consumed at the next boot,
so a later watchdog or crash is not mislabeled as an older intentional restart.

The ESP32 also derives a standards-compliant network hostname from the gateway
name: uppercase letters become lowercase and separators such as underscores or
spaces become hyphens. For the supplied `OMG_multi_receiver` preset, the Web UI
can therefore be reached at `http://omg-multi-receiver.local/` through mDNS or
at `http://omg-multi-receiver/` when the router registers the DHCP hostname.
This remains useful when MQTT is unavailable and the dynamically assigned IP
cannot be read from the state topic. The active hostname is also included in
the system-state payload.

These are preset choices, not global defaults. They can be adapted in a custom
environment if lower power consumption or watchdog restarts are preferred.

Logs use stable prefixes: `[WIFI]`, `[MQTT]`, `[WOL]`, `[GPIO]`, `[BLE][ADV]`,
`[MEM]`, `[QUEUE]`, `[RF][CC1101]`, `[WebUI][OTA]` and `[DIAG]`. They include reconnect causes,
timers, queue pressure and memory information, but never print passwords,
private keys or certificate contents.

The implementation also preserves an existing MQTT password when a WebUI or
onboarding password field is submitted empty. Fixed-size configuration strings
are bounds checked, and the actual MQTT CONNACK result is retained so WOL can
distinguish transport, broker and authentication failures without opening a
second probe connection.

The information page is a read-only snapshot. Opening or refreshing it does
not republish SYS/RF/WebUI state on MQTT or fill the console with duplicate
diagnostic payloads. If a temporary WebUI allocation leaves too little
contiguous memory for a queued JSON message, the queue retains that message and
retries later instead of discarding discovery or state data.

## Validation status

Both custom environments and the unchanged upstream `esp32dev-multi_receiver`
environment compile successfully. The BLE image
was exercised on an ESP32-D0WD-V3 with a CP2102 interface and CC1101 at
433.92 MHz. Tests covered authenticated MQTT, RF initialization, retained GPIO
input state, BLE presence/discovery, local OTA, repeated warm boots, console
paging, progressive GPIO/BLE pages and sustained mixed WebUI traffic. The 2+2
page loaded all four cards, and a final 40-request mixed-page run completed
without an HTTP failure while MQTT stayed connected. Output discovery, logical
ON/OFF and restore-after-restart were verified without an attached load. A
deliberately induced end-to-end broker outage/WOL cycle, an electrically loaded
output test and a long-duration RF soak are still recommended for each
deployment.
