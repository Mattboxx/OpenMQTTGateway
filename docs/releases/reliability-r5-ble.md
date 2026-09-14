## ESP32 + CC1101: WOL, 2 GPIO inputs, 2 GPIO outputs and BLE presence

**Pre-release / test version. Not a promise of zero bugs or uninterrupted operation.**

Based on OpenMQTTGateway 1.8.1. This is an optional preset for an ESP32 Dev Module
with CC1101, not a replacement for OpenMQTTGateway's other boards and presets.
Firmware reports `v1.8.1-wol-2in-2out-ble-r5`.

### What this edition is useful for

One gateway can receive RF sensors, monitor two wired contacts, control two
Home Assistant switches, detect selected fixed-MAC BLE beacons and wake the MQTT
host after a configured network/broker outage. Examples include a garage, gate,
shed or equipment cabinet.

- WOL target MAC, outage conditions, delay and repeat interval configurable in
  the WebUI; reconnection resets the recovery timers.
- Two independently enabled GPIO inputs: pin, INPUT/PULLUP/PULLDOWN, active
  HIGH/LOW, debounce, name and Home Assistant device class.
- Two independently enabled GPIO outputs: safe pin choices, push-pull/open-drain,
  active level, OFF/ON/restore startup and Home Assistant ON/OFF control.
  Outputs are disabled by default; external loads require suitable circuitry.
- Up to four selected fixed-MAC BLE devices: immediate presence on detection,
  per-device absence timeout and signal threshold, retained state and RSSI.
- Home Assistant discovery and removal of disabled GPIO/BLE entities.
- Local WebUI firmware upload after the first USB installation.

### Reliability changes in this update

- A decoded crash confirmed an uncaught `std::bad_alloc` while preparing a system
  MQTT message. The outgoing queue now has fixed slots, checked allocation,
  exact-sized serialization and an 8 KiB admission headroom threshold. Rejected
  messages are counted instead of causing that allocation exception.
- Fixed long-log-line buffer overflow, overlapping console writes and unsafe
  concurrent access, plus rollover-unsafe timers and shared uptime accounting.
- Fixed a BLE WebUI pause that could remain latched; pause BLE during Wi-Fi recovery.
- Bounded 512-byte HTTP writes retry temporary network-buffer pressure.
- A 120-second main-loop progress guard stores incident context. Its emergency
  path no longer queries the heap or waits for normal radio shutdown handlers.
- `/diag` exposes recovery diagnostics; `/crash.bin` downloads an existing crash
  report without erasing it. Reports may contain credentials: keep them private.
  Routine BLE messages remain at verbose level.

### Validation and known limitation

All three target builds passed: BLE, NO BLE and the original multi_receiver
preset. Five native regression tests passed. This BLE firmware was installed
via local OTA; light checks showed MQTT connected, BLE detections and complete
GPIO/BLE pages.

**An intermediate revision still went offline during repeated GPIO-page requests,
after 51 successful mixed HTTP requests.** The subsequent power cycle yielded
no new crash report. Revision 5 further hardens emergency recovery, but the cause
of that outage is not established, and multi-day stability is not validated.
The emergency reset was not deliberately triggered on the deployed device.

### Installation

**First installation from original OpenMQTTGateway: use USB.** Download the
`...BLE-USB-Windows.zip`, extract the entire ZIP, connect a USB data cable and
double-click `FLASH-USB-WINDOWS.bat`. Select the correct COM port if asked.
The ZIP includes the flashing utility, bootloader, partitions, OTA boot data,
firmware and instructions; PlatformIO and Python are not required. No full-flash
erase is requested, so existing settings are normally retained. This package
targets classic ESP32 / 4 MB flash, not ESP32-S2/S3/C3.

**Already running this custom edition:** download `...BLE-WebUI.bin` and open
**Firmware Upgrade > Local firmware file**. Do not upload the ZIP. The original
1.8.1 WebUI does not provide this local-file upload feature.

`SHA256SUMS.txt` covers both variants. Choose the files matching this release.
The [NO BLE pre-release](https://github.com/Mattboxx/OpenMQTTGateway/releases/tag/v1.8.1-esp32-cc1101-wol-2in-2out-no-ble-r5)
has the same WOL and 2 IN / 2 OUT features without the BLE observer.
