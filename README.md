# OpenMQTTGateway - MQTT recovery and configurable GPIO I/O edition

> **This branch is a clearly scoped, optional extension of OpenMQTTGateway
> 1.8.1.** It was created for an ESP32 garage gateway that receives 433 MHz
> devices through a CC1101, monitors wired sensors, detects selected BLE
> devices and can wake the MQTT host.
> It does not replace or redefine the many other boards and gateway presets in
> the upstream project.

The ready-to-build presets are `esp32dev-multi_receiver-wol-gpio-ble` and
`esp32dev-multi_receiver-wol-gpio-no-ble`. They keep the official
`esp32dev-multi_receiver` preset unchanged and add features useful when the
gateway is installed away from the MQTT/Home Assistant server.

## What this edition adds

| Area | Upstream 1.8.1 behaviour | This optional edition |
| --- | --- | --- |
| Wake-on-LAN | No MQTT-outage recovery policy | Configurable target MAC, outage delay, failure threshold, repeat interval and error categories; a successful MQTT connection resets every WOL timer |
| Wired sensors | One build-time GPIO input | Two independently enabled and named inputs configured in the Web UI |
| Input electronics | Pin mode fixed at compile time | Per-input `INPUT`, `PULLUP` or `PULLDOWN`, active HIGH/LOW, debounce, retained MQTT state and Home Assistant device class |
| GPIO outputs | A separate actuator build is needed | Two independently enabled Home Assistant switches for relays, LEDs, active buzzers or 3.3 V logic inputs; each supports push-pull/open-drain, active HIGH/LOW, safe OFF/ON/restore startup and retained state |
| GPIO safety | Generic board-level validation | The supplied CC1101 preset exposes only pins that do not collide with flash, SPI, CC1101 or ESP32 boot-strapping duties |
| BLE presence | BLE presets normally publish/ decode the devices they hear | Up to four fixed-MAC BLE devices selected in the Web UI; each gets a retained Home Assistant presence entity plus RSSI, its own away timeout and minimum-signal threshold |
| MQTT resilience | Standard reconnect behaviour | Unique client ID suffix, uninterrupted WiFi association window, protected recovery portal, predictable DHCP/mDNS hostname, bounded MQTT reconnect timing, clean warm-reboot radio shutdown, runtime recovery watchdog, longer keepalive, preserved operation while the broker is offline and safer password updates |
| Diagnostics | General OpenMQTTGateway logs | Stable `[BOOT]`, `[WIFI]`, `[MQTT]`, `[WOL]`, `[GPIO]`, `[BLE][ADV]`, `[MEM]`, `[QUEUE]`, `[RF][CC1101]`, `[WebUI][OTA]` and `[DIAG]` events with failure causes and memory/queue context; a bounded startup guard automatically recovers from a stalled module initialization |
| Web interface | Original compact Web UI | Responsive card layout, live GPIO/BLE state, nearby-BLE suggestions, contextual wiring hints and local `.bin` OTA upload after this custom edition has been installed once by USB |

The WOL logic deliberately does not send a magic packet after every ordinary
disconnect. It distinguishes transport failures, broker refusals and
authentication errors, waits for the configured outage conditions and clears
stale state after reconnection. This addresses repeated or apparently random
WOL packets and timers that did not reset correctly.

Typical uses include a garage, gate, shed or equipment cabinet where one ESP32
must receive RF devices, expose door/contact and selected BLE presence sensors,
and control two small external loads or logic inputs from Home Assistant while
also waking the machine hosting MQTT when it is genuinely unavailable. BLE scanning is passive and duty-limited to reduce contention with
the ESP32 WiFi radio. A controller-only VHCI observer avoids the NimBLE/Bluedroid
host, connections, GATT and decoder tasks. This preset intentionally tracks
selected devices rather than embedding the full Theengs decoder, because
CC1101 + RTL_433 + the full BLE pipeline leaves too little memory for reliable
operation on this 4 MB ESP32.

Configure BLE from **Device configuration → BLE presence devices**. Wait for a
scan, choose one of the recently seen MAC suggestions (or type a fixed MAC),
give it a name, set the away timeout and RSSI threshold, then enable the slot.
No reboot is required. Phones and privacy-oriented devices often rotate their
BLE address; use a beacon/tag with a fixed MAC when reliable presence is needed.

Start here:

* [Detailed use case, configuration and design notes](docs/use/mqtt-wol.md)
* [GPIO input and output wiring](docs/use/sensors.md#gpio-input)
* [Firmware with BLE](https://github.com/Mattboxx/OpenMQTTGateway/releases/tag/v1.8.1-esp32-cc1101-wol-2in-2out-ble)
* [Firmware without BLE](https://github.com/Mattboxx/OpenMQTTGateway/releases/tag/v1.8.1-esp32-cc1101-wol-2in-2out-no-ble)

## Firmware variants

| Variant | Intended use |
| --- | --- |
| `WOL + 2 IN + 2 OUT + NO BLE` | RF/CC1101, WOL, two configurable inputs and two Home Assistant-controlled outputs. It contains all fixes and features of the BLE edition except the BLE observer, leaving more free memory. |
| `WOL + 2 IN + 2 OUT + BLE` | The same RF, WOL and GPIO feature set plus selected fixed-MAC BLE presence, balanced scanning, automatic scan recovery and cleanup of disabled BLE entities. |

The two outputs are disabled by default. Their logical command, retained state,
active-level inversion and restore-after-restart paths were exercised through
MQTT/Home Assistant without an attached load; electrically validate any relay,
LED or buzzer circuit before enabling it.

Build the dedicated preset with:

```text
platformio run -e esp32dev-multi_receiver-wol-gpio-ble
platformio run -e esp32dev-multi_receiver-wol-gpio-no-ble
```

Every release provides a complete Windows USB ZIP for the **first installation**.
It contains the portable flashing tool, bootloader, partition table, OTA data,
application image and a double-click guided script; PlatformIO and Python are
not required. The separately named WebUI `.bin` is only for later updates from
a version of this custom edition that already shows **Local firmware file**.
The original OpenMQTTGateway 1.8.1 WebUI does not provide that local-file form.

The custom preset is intentionally opt-in. All upstream environments remain
available, and changes that are not relevant to them stay behind build flags.
The supplied binary was validated on the hardware described below. Verify your
own wiring, GPIO choices, broker and RF configuration before relying on it in
an unattended installation.

---

## About the upstream project

[![Community forum](https://img.shields.io/badge/community-forum-brightgreen.svg)](https://community.openmqttgateway.com)

![Build](https://github.com/1technophile/OpenMQTTGateway/workflows/Build/badge.svg?branch=development)
![Check Code Format](https://github.com/1technophile/OpenMQTTGateway/workflows/Check%20Code%20Format/badge.svg?branch=development)

[![OpenMQTTGateway capabilities](https://github.com/1technophile/OpenMQTTGateway/blob/development/docs/.vuepress/public/img/OpenMQTTGateway.png)](https://community.openmqttgateway.com)

OpenMQTTGateway aims to unify various technologies and protocols into a single firmware. This reduces the need for multiple physical bridges and streamlines diverse technologies under the widely-used [MQTT](http://mqtt.org/) protocol.

## Sponsors

<a href = "https://www.emqx.com/en?utm_source=github.com&utm_medium=referral&utm_campaign=OpenMQTTGateway-github-to-emqx-home"><img src="https://github.com/1technophile/OpenMQTTGateway/blob/development/docs/img/EMQ.png"  height="50"/></a>

## Documentation

The documentation is [here](https://docs.openmqttgateway.com)

The reference sheet, with the list of all functions, pinouts is [here](https://docs.google.com/spreadsheets/d/1_5fQjAixzRtepkykmL-3uN3G5bLfQ0zMajM9OBZ1bx0/edit#gid=0)

## Upload

Upload OpenMQTTGateway directly from the [upload page](https://docs.openmqttgateway.com/upload/web-install.html) (no additional software required) or [build your own configuration](https://docs.openmqttgateway.com/upload/builds.html) with [PlatformIO](https://platformio.org/).

## Using OpenMQTTGateway ?
Support open-source development through sponsorship and gain exclusive access to our private forum. Your questions, issues, and feature requests will receive priority attention, plus you'll gain insider access to our roadmap.

[![](https://img.shields.io/static/v1?label=Sponsor&message=%E2%9D%A4&logo=GitHub&color=%23fe8e86)](https://github.com/sponsors/theengs)

## Products powered by OpenMQTTGateway

### Theengs Bridge, Bluetooth gateway (BLE) with external antenna

[Theengs bridge](https://shop.theengs.io/products/theengs-bridge-esp32-ble-mqtt-gateway-with-ethernet-and-external-antenna) is a powerfull BLE to MQTT gateway for over [100 sensors](https://decoder.theengs.io/devices/devices.html). Equipped with an Ethernet port, and external antenna, ensuring an enhanced range for your BLE sensors. It supports also WiFi connectivity.

[![Theengs bridge view](./docs/.vuepress/public/img/Theengs-Bridge-ble-gateway.png)](https://shop.theengs.io/products/theengs-bridge-esp32-ble-mqtt-gateway-with-ethernet-and-external-antenna)

### Theengs Plug, Bluetooth gateway (BLE) gateway and Smart Plug

[Theengs plug](https://shop.theengs.io/products/theengs-plug-smart-plug-ble-gateway-and-energy-consumption) brings the following features:
* BLE to MQTT gateway, tens of [Bluetooth devices](https://compatible.openmqttgateway.com/index.php/devices/ble-devices/) supported thanks to Theengs Decoder library. The plug uses an ESP32 acting as a BLE to Wifi gateway to scan, decode and forward the data of the nearby sensors,
* Smart plug that can be controlled remotely,
* Energy consumption monitoring,
* Device tracker,
* Presence detection (beta),
* Local connectivity first.

[![Theengs plug view](./docs/.vuepress/public/img/Theengs-Plug-OpenMQTTGateway.png)](https://shop.theengs.io/products/theengs-plug-smart-plug-ble-gateway-and-energy-consumption)

Support the project by purchasing the [Theengs bridge](https://shop.theengs.io/products/theengs-bridge-esp32-ble-mqtt-gateway-with-ethernet-and-external-antenna) or the [Theengs plug](https://shop.theengs.io/products/theengs-plug-smart-plug-ble-gateway-and-energy-consumption)

## Compatible items

* [List of supported devices](https://compatible.openmqttgateway.com/index.php/devices/), door/window sensors, PIR sensors, smoke detectors, weather stations...

* [List of compatible boards (Off the shelf or DIY) is available](https://compatible.openmqttgateway.com/index.php/boards/), RF Bridge, IR, BLE gateways...

*Running on a computer*
If you want to use the BLE decoding capabilities of OpenMQTTGateway with a Raspberry Pi, Windows or Unix PC you can now leverage [Theengs Gateway](https://theengs.github.io/gateway/).

* [List of compatible components to build your gateway](https://compatible.openmqttgateway.com/index.php/parts/), DHT, RF, IR emitters and receivers...

## Compatible controllers, saas or software

* [Home Assistant](https://docs.openmqttgateway.com/integrate/home_assistant.html)

* [OpenHAB](https://docs.openmqttgateway.com/integrate/openhab2.html)

* [NodeRed](https://docs.openmqttgateway.com/integrate/node_red.html)

* [AWS-IOT](https://docs.openmqttgateway.com/integrate/aws_iot.html)

## Contributors ✨

Thanks goes to these wonderful [people](https://github.com/1technophile/OpenMQTTGateway/graphs/contributors) who helped OpenMQTTGateway on Github and to the users contributions into the [community](https://community.openmqttgateway.com/).

## Support

For Questions or Support please don't open an issue, first go to the [docs](https://docs.openmqttgateway.com) and if you don't find your answer there, you can post your question in [the community forum](https://community.openmqttgateway.com)

## Help

If you like the project and/or used it please consider supporting it! It can be done in different ways:
* Helping other users in the [community](https://community.openmqttgateway.com)
* [Contribute](development) to the [code](https://github.com/1technophile/OpenMQTTGateway) or the [documentation](https://docs.openmqttgateway.com)
* Buy devices, boards or parts from the [compatible web site](https://compatible.openmqttgateway.com), the devices and parts linked use affiliated links.
* Donate or sponsor the project [developers](https://github.com/1technophile/OpenMQTTGateway/graphs/contributors)
* Make a video or a blog article about what you have done with [OpenMQTTGateway](https://docs.openmqttgateway.com) and share it to the [community](https://community.openmqttgateway.com)

## Media

* [Hackaday - ARDUINO LIBRARY BRINGS RTL_433 TO THE ESP32](https://hackaday.com/2023/01/13/arduino-library-brings-rtl_433-to-the-esp32)
* [CNX Software - 433 MHz is not dead! Using an ESP32 board with LoRa module to talk to 433 MHz sensors](https://www.cnx-software.com/2023/01/14/esp32-board-with-lora-433-mhz-sensors/)
* [RTL_433 PORTED TO ESP32 MICROCONTROLLERS WITH CC1101 OR SX127X TRANSCEIVER CHIPS](https://www.rtl-sdr.com/rtl_433-ported-to-esp32-microcontrollers-with-cc1101-or-sx127x-transceiver-chips/)
* [Using low-cost wireless sensors in the unlicensed bands](https://lwn.net/Articles/921497/)
* [SMART PLUG ESP32 OPENMQTTGATEWAY SERVING AS AN BLE MQTT GATEWAY AND A POWER METER](https://www.electronics-lab.com/smart-plug-esp32-openmqttgateway-serving-as-an-ble-mqtt-gateway-and-a-power-meter/)

### Theengs Plug
[![Theengs Plug video ElektroMaker](https://img.youtube.com/vi/nUwMt9p2U7o/0.jpg)](https://www.youtube.com/watch?v=nUwMt9p2U7o&t=427s)

### 433Mhz and BLE
[![433Mhz and BLE gateway video by Andreas Spiess](https://img.youtube.com/vi/_gdXR1uklaY/0.jpg)](https://www.youtube.com/watch?v=_gdXR1uklaY)

### BLE
[![BLE gateway video by Andreas Spiess](https://img.youtube.com/vi/noUROhtf0E0/0.jpg)](https://www.youtube.com/watch?v=noUROhtf0E0)

### 433Mhz
[![RTL_433 video by TECH MIND](https://img.youtube.com/vi/H-JXWbWjJYE/0.jpg)](https://www.youtube.com/watch?v=H-JXWbWjJYE)

### LORA
[![LORA video by Priceless Toolkit](https://img.youtube.com/vi/6DftaHxDawM/0.jpg)](https://www.youtube.com/watch?v=6DftaHxDawM)
