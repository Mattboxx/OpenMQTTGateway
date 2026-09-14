## ESP32 + CC1101: WOL, 2 GPIO inputs and 2 GPIO outputs — without BLE

**Pre-release / test version. Not a promise of zero bugs or uninterrupted operation.**

Based on OpenMQTTGateway 1.8.1. This optional ESP32 Dev Module + CC1101 preset
leaves the BLE observer out, freeing memory. It is a full-featured alternative,
not a recovery-only firmware. Firmware reports `v1.8.1-wol-2in-2out-no-ble-r5`.

### What this edition is useful for

Receive RF sensors, monitor garage/door contacts, control external logic or
properly driven loads through Home Assistant, and wake the MQTT host after
configurable outage conditions — without BLE scanning.

- RF/CC1101 multi-receiver modules.
- Configurable WOL target MAC, outage conditions, delay and repeat interval;
  reconnection resets the timers.
- Two WebUI-configurable GPIO inputs: safe pin choices, INPUT/PULLUP/PULLDOWN,
  active HIGH/LOW, debounce, names and Home Assistant device classes.
- Two WebUI-configurable GPIO outputs exposed as Home Assistant switches:
  push-pull/open-drain, active level and OFF/ON/restore startup. Outputs are
  disabled by default; external loads require suitable circuitry.
- Retained states, Home Assistant discovery and cleanup of disabled GPIO slots.
- Local firmware upload after the first USB installation.

### Reliability changes in this update

- Fixed-slot MQTT queue with checked, exact-sized payload allocation and an
  8 KiB admission headroom threshold. This addresses the uncaught allocation
  failure confirmed in a saved crash from the BLE variant; rejected messages
  are counted rather than causing that exception.
- Fixed console buffer overflow, overlapping writes and concurrency problems.
- Safer periodic timers, monotonic ESP32 uptime and shared-queue access.
- Bounded 512-byte HTTP writes with retries for temporary network-buffer pressure.
- Independent 120-second progress guard, persistent incident context and an
  emergency reset path that avoids heap queries and normal radio shutdown handlers.
- `/diag` for diagnostics and `/crash.bin` to download a saved report without
  erasing it. Keep crash reports private: they may contain credentials.

### Validation and known limitation

BLE, NO BLE and the original multi_receiver preset compiled successfully.
Five native regression tests passed. **This NO BLE revision has not been
installed on hardware during the current validation session.**

An intermediate BLE revision went offline during repeated GPIO-page requests.
Its cause remains unexplained. The common recovery path was hardened further,
but neither this build nor the BLE build is being presented as a proven stable
release. Multi-day validation and an actual emergency-reset test remain outstanding.

### Installation

**First installation from original OpenMQTTGateway: use USB.** Download the
`...NO-BLE-USB-Windows.zip`, extract the entire ZIP, connect a USB data cable and
double-click `FLASH-USB-WINDOWS.bat`. Select the correct COM port if asked.
The package contains the flashing utility, bootloader, partitions, OTA boot data,
firmware and instructions; no Python or PlatformIO installation is required.
It does not request a full-flash erase, so existing settings are normally retained.
This package targets classic ESP32 / 4 MB flash, not ESP32-S2/S3/C3.

**Already running this custom edition:** download `...NO-BLE-WebUI.bin` and use
**Firmware Upgrade > Local firmware file**. Upload the `.bin`, not the ZIP.
Original OpenMQTTGateway 1.8.1 does not provide this local-file WebUI upload.

`SHA256SUMS.txt` covers both variants. Choose the files matching this release.
The [BLE pre-release](https://github.com/Mattboxx/OpenMQTTGateway/releases/tag/v1.8.1-esp32-cc1101-wol-2in-2out-ble-r5)
has the same WOL and GPIO features plus selected fixed-MAC BLE presence detection.
