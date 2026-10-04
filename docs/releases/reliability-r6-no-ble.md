## ESP32 + CC1101 — WOL + 2 IN + 2 OUT — NO BLE — R6

An optional OpenMQTTGateway 1.8.1 extension for a **classic ESP32 Dev Module,
4 MB flash and CC1101**, not a replacement for upstream's many other presets.
Firmware version: `v1.8.1-wol-2in-2out-no-ble-r6`.
This is a full-featured variant, **not a recovery-only firmware**. It differs
from the BLE edition only by omitting the BLE observer, leaving more free RAM.

### What this edition adds, and why

Use one garage/gate/shed gateway to receive RF sensors, monitor wired contacts,
control suitably driven external logic from Home Assistant and wake the MQTT
host after configured outage conditions, without BLE scanning.

- **WOL:** configurable destination MAC, delay, minimum failures, repeat interval
  and transport/broker/authentication trigger categories; reconnect resets timers.
- **Two GPIO inputs:** independent enable, safe pin dropdown, INPUT/PULLUP/
  PULLDOWN, active HIGH/LOW, debounce, name, retained state and HA device class.
- **Two GPIO outputs:** independent HA ON/OFF switches, push-pull/open-drain,
  active level, OFF/ON/restore startup and retained state. Disabled by default;
  electrical loads need appropriate external circuitry and validation.
- **Home Assistant:** discovery, retained states and cleanup of disabled/obsolete
  GPIO entities and duplicate discovery switches.
- **WebUI:** progressive GPIO configuration, wiring hints, diagnostics and local
  `.bin` upload after the first USB installation of this custom edition.

### Reliability improvements in R6

The common fixes match the BLE edition: a checked static 32-object lwIP timer
reserve for the decoded network allocation panic; release of 17,584 bytes of
unused OOK startup templates after independent decoder registration; compact
unused demodulator metadata; checked RF copies with network headroom; bounded
WebUI/OTA transfers and memory checks; Wi-Fi packet-buffer budgeting; ordered
OTA quiescence; and bounded binary multipart parsing in an isolated generated
WebServer. Full RF pulse arrays and protocol inventories remain intact.

WOL parsing no longer reads past short/empty MAC strings, and one-attempt mode
works at clock rollover. Checked MQTT queues, bounded console handling,
monotonic uptime, software/RTC recovery guards and gateway-liveness recovery are
retained. `/diag` and saved crash downloads help distinguish future failures;
keep reports private because they may contain credentials and decode only with
the exact deployed ELF. The original multi_receiver preset/shared SDK remain
unmodified by the opt-in dependency generation.

### Validation and limits

BLE, NO BLE and original multi_receiver compiled. Both custom variants were
installed on the same ESP32 via web OTA, returning to BLE with all GPIO/BLE
settings preserved. On NO BLE, all ten applicable page/style/GPIO responses
were complete; MQTT and RTL_433 were active and BLE absent. Initial usable
memory was approximately 86 KB, with zero allocation failures and Wi-Fi
disconnects recorded during the short run.

Fourteen native reliability programs, eight RF patch-generation checks, four
WebServer generation checks and actual RF allocation/ownership, BLE, WOL and
GPIO/discovery function tests passed. The complete eleven-file USB installer
passed pre-serial integrity, corrupted-image and duplicate-manifest checks.

**Public release does not mean guaranteed zero bugs.** Multi-week reliability,
every RF decoder, electrical output behavior and all interrupt interleavings
are not established by these short hardware/host tests. The owner requested
publication without scheduled monitoring; future issues will be reported during
normal use. Historical outages may have had more than one cause. See the
[investigation record](https://github.com/Mattboxx/OpenMQTTGateway/blob/feature/wol-multi-gpio-v1.8.1/docs/releases/reliability-r6-investigation.md).

### Installation: which file to download

**First installation from original OpenMQTTGateway: USB.** Download
`OpenMQTTGateway-v1.8.1-ESP32-CC1101-WOL-2IN-2OUT-NO-BLE-R6-USB-Windows.zip`,
extract **all files**, connect a USB data cable and double-click
`FLASH-USB-WINDOWS.bat`. Select the correct COM port if asked. All four images,
portable flashing tool, installer and instructions are included; Python and
PlatformIO are not required. If the standard launcher fails, follow
`LEGGIMI-PRIMA.md` before using `RIPROVA-USB-ROM.bat`. No full-flash erase is
requested; existing settings are normally retained. Not for ESP32-S2/S3/C3.

**Already using this custom edition:** download
`OpenMQTTGateway-v1.8.1-ESP32-CC1101-WOL-2IN-2OUT-NO-BLE-R6-WebUI.bin`, open
**Firmware Upgrade → Local firmware file** and install the `.bin`, not the ZIP.
Original 1.8.1 does not offer that local-file form. For URL update, use the
direct `.bin` download URL, not the GitHub release-page URL. Wait for completion
and restart; subsequent updates do not require USB.

`SHA256SUMS.txt` covers this release's two downloads. The
[BLE edition](https://github.com/Mattboxx/OpenMQTTGateway/releases/tag/v1.8.1-esp32-cc1101-wol-2in-2out-ble-r6)
adds selected fixed-MAC presence tracking to the same WOL and GPIO features.
