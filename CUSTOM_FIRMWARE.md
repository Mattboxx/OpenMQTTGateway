# OpenMQTTGateway 1.8.1 WOL and multi-GPIO variant

This branch extends the `esp32dev-multi_receiver` environment with configurable
Wake-on-LAN recovery, multiple contact inputs and additional diagnostics. It is
based on OpenMQTTGateway 1.8.1.

## Build

```text
platformio run -e esp32dev-multi_receiver
```

The application image is generated at:

```text
.pio/build/esp32dev-multi_receiver/firmware.bin
```

## Hardware

The `esp32dev-multi_receiver` environment enables:

- CC1101 on CS 5, GDO0 12 and GDO2 27;
- RF, RF2 and RTL_433 receivers;
- up to four independent contact inputs using `INPUT` mode.

Open **Configuration**, then **GPIO Inputs** in the WebUI. Each channel has an
enable switch, a name and a pin selector. The selector omits unavailable ESP32
pins and pins already reserved by flash, SPI, CC1101 or the enabled RF modules.
Duplicate enabled pins are rejected. Changes are persisted and applied after a
controlled restart.

Channel 1 defaults to GPIO 4 and retains the original `GPIOInput` Home Assistant
identity and `/GPIOInputtoMQTT` topic. Channels 2-4 receive separate discovery
entities and MQTT topics. Every enabled channel publishes its current level on
startup, on a debounced change and after an MQTT reconnection.

## Wake-on-LAN

Open the gateway WebUI and choose **Configuration**, then **MQTT**. The page
can persistently configure:

- enabled state and destination MAC address;
- initial delay and minimum consecutive MQTT failures;
- repeat interval (`0` means once per outage);
- transport/TLS, broker-rejection and authentication/authorization triggers.

The PicoMQTT reconnect loop has been extended locally to retain the MQTT
CONNACK return code. This lets the three trigger classes remain accurate
without performing a second probe connection.

WOL is disabled and has no destination MAC in a fresh installation. Set the
MAC before enabling it. With MQTT discovery enabled, Home Assistant creates a
`WOL: Destination MAC` text entity and a `WOL: Enabled` switch under the gateway
device. MAC input is checked both by Home Assistant and by the firmware.

## Connection hardening

- MQTT reconnect attempts are non-blocking and limited to one every 5 seconds.
- Keepalive is 60 seconds and socket timeout is 10 seconds.
- The MQTT client ID receives a suffix derived from the ESP32 MAC address.
- A prolonged broker outage does not cause repeated ESP32 restarts.
- WiFi automatic reconnect is enabled and power saving is disabled.
- Empty password fields preserve the saved MQTT password.
- Runtime and stored fixed-size configuration strings are bounds checked.
- The official 1.8.1 CC1101 backoff, RTL_433 stack/OOM and MQTT queue fixes are
  retained.

## Local WebUI firmware update

Open **Firmware Upgrade**, then **Upgrade from Local File** to upload an
`esp32dev-multi_receiver` application `.bin` directly from the browser. The
existing URL and release-level update methods remain available.

The upload is streamed into the inactive OTA partition and never buffered as a
complete file in RAM. The handler requires WebUI authentication when enabled,
checks the `.bin` extension and ESP32 image header, reports flash/validation
errors, and changes the boot partition only after the complete image validates.
The board restarts into the previous firmware after a failed or interrupted
upload and into the new firmware after success. Do not remove power during the
upload, and only install firmware built for the same board and partition layout.

## Diagnostic logs

Serial logs use stable prefixes so a captured session can be filtered easily:

- `[BOOT]`: firmware/environment, ESP32 reset reason, SDK, flash and heap;
- `[WIFI]`: reconnect attempts and final radio/network parameters;
- `[WIFI]` also records DHCP address changes that occur without a disconnect;
- `[MQTT]`: active profile (without secrets), CONNACK reason, TLS errors and
  reconnect context;
- `[WOL]`: loaded/saved policy, outage timer, trigger cause and packet result;
- `[GPIO]`: channel setup, debounced transitions and forced republishing;
- `[WebUI][OTA]`: local upload authorization, progress, byte count, MD5 and
  validation errors;
- `[RF][CC1101]`: pins, frequency, SPI retries and terminal failure;
- `[QUEUE]`: capacity pressure, blocked messages and mutex timeouts;
- `[DIAG]`: periodic system snapshot, including current/high-water queue size,
  MQTT/network failure counters, WiFi state/channel, heap minimum and largest
  free allocation.

Passwords, WiFi PSKs, private keys and certificate contents are never printed.
The default notice level records state changes and failures; compiling with
`LOG_LEVEL_TRACE` additionally records raw GPIO edges, MQTT attempt durations
and WOL waiting conditions.

## Hardware validation

Validated on an ESP32-D0WD-V3 over its CP2102 serial interface:

- WiFi configuration survived both USB uploads and mDNS followed the DHCP
  address correctly;
- authenticated MQTT connected on the first attempt without exposing stored
  passwords or certificate contents;
- CC1101 connected on the first SPI probe at 433.92 MHz using CS 5, GDO0 12
  and GDO2 27;
- the primary GPIO input initialized HIGH and its retained state was
  republished after MQTT connected;
- the WebUI MQTT/WOL page loaded successfully and left the stored MQTT password
  out of the HTML form;
- the original startup discovery burst reached 18 queued messages. This
  variant allows for WOL and four GPIO discovery records with a 32-message
  queue and a 2048-byte maximum JSON buffer;
- the final observation interval reported zero MQTT failures, zero network
  failures, an empty queue and about 115 KB free heap.

This image has been compilation-tested and smoke-tested on the physical board.
Long-duration RF reception and a deliberately induced MQTT outage/WOL cycle
remain useful soak tests; the 1.7.0 image is retained as a fallback.
