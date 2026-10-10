## ESP32 + CC1101 — WOL + 2 IN + 2 OUT + BLE — R6

An optional OpenMQTTGateway 1.8.1 extension for a **classic ESP32 Dev Module,
4 MB flash and CC1101**. It does not replace upstream's other boards or gateway
presets. Firmware version: `v1.8.1-wol-2in-2out-ble-r6`.

**Stable release — status updated on 10 October 2026.** The owner reports that
the deployed BLE firmware has remained stable so far in normal use following
the 4 October release. This is a field-use confirmation on that installation,
not a new build or a guarantee for every board/network configuration.

### What this edition adds, and why

A garage, gate, shed or equipment cabinet can use one gateway to receive RF
sensors, monitor wired contacts, control external logic from Home Assistant,
detect selected BLE beacons and wake the machine hosting MQTT after a genuine
configured outage.

- **Wake-on-LAN:** target MAC, outage delay, failure threshold, repeat interval
  and transport/broker/authentication trigger categories configurable in the
  WebUI. Reconnection resets the timers; ordinary disconnects need not send WOL.
- **Two GPIO inputs:** independent enable, safe pin dropdown, INPUT/PULLUP/
  PULLDOWN, active HIGH/LOW, debounce, name, retained state and HA device class.
- **Two GPIO outputs:** independently enabled HA ON/OFF switches, push-pull or
  open-drain, active HIGH/LOW, OFF/ON/restore startup and retained state.
  Outputs are disabled by default; relays/LEDs/buzzers require suitable external
  circuitry and electrical validation, not direct arbitrary loads on ESP32 pins.
- **Four selected fixed-MAC BLE slots:** immediate ON when detected, renewed
  per-device absence timeout, RSSI threshold, retained presence and RSSI.
  Unknown/absent RSSI is JSON null, not a misleading -127 measurement.
- **HA discovery cleanup:** disabled GPIO/BLE entities and obsolete duplicate
  discovery switches are removed. Routine BLE logs are verbose-only.
- **WebUI:** progressive GPIO/BLE configuration, contextual wiring hints,
  nearby BLE suggestions, diagnostics and local `.bin` upload after the first
  installation of this custom edition.

### Reliability improvements in R6

- Static storage for 32 lwIP timers addresses a checksum-verified decoded
  `MEMP_SYS_TIMEOUT is empty` panic in the pinned SDK's heap-backed timer path.
- RF startup templates are released only after independent decoder registration,
  recovering 17,584 bytes for the full 157-entry OOK inventory. Unused demodulator
  metadata is compacted; actual pulse arrays and protocol inventories are intact.
- RF pulse allocations are checked and leave usable network headroom. Allocation
  failure drops a signal safely rather than copying through NULL.
- Bounded HTTP writes and OTA transfers, usable/contiguous-memory checks,
  Wi-Fi packet-buffer budgeting and ordered RF/BLE/MQTT OTA quiescence.
- Fixed binary multipart boundary matching and unchecked stack-buffer sizing
  in the pinned WebServer; isolated checked generation leaves the shared SDK
  and original multi_receiver preset unchanged.
- Fixed WOL short/empty MAC parsing and one-attempt behavior at clock rollover.
- Retains checked MQTT queues, console bounds/concurrency protections, monotonic
  uptime, software/RTC recovery guards and gateway-liveness recovery.
- `/diag` records allocation, Wi-Fi, network, OTA and BLE context. Saved crashes
  can be downloaded without erasure. Keep raw reports private: they can contain
  credentials. Archive the exact ELF before decoding a report.

### Validation and limits

Both feature variants and the original multi_receiver preset compiled.
Both variants were installed on the same ESP32 via local-file web OTA and
returned to BLE, preserving all eight GPIO/BLE configuration forms. Complete
web pages, MQTT connection, RTL_433 operation and resumed BLE advertisements
were verified. Initial BLE usable heap was approximately 41 KB, with no failed
allocations or Wi-Fi disconnects recorded during the short validation run.

Fourteen native reliability programs, eight RF patch-generation tests, four
WebServer generation tests, actual RF allocation/template-lifetime tests and
actual BLE, WOL and GPIO/discovery function tests passed. Both eleven-file USB
packages passed the actual installer's pre-serial integrity checks, including
corrupted-image and duplicate-manifest rejection.

**Marked stable following the owner's field-use confirmation on 10 October.**
This does not guarantee zero bugs or impossible hangs. Multi-week stability is
not yet established; the owner chose normal use and future fault reporting
instead of scheduled observation. No scheduled checks
remain. Host tests mock radio, pins, scheduler and broker; they do not validate
every RF protocol, electrical loads or all interrupt interleavings. Historical
outages may have had more than one cause. The [investigation record](https://github.com/Mattboxx/OpenMQTTGateway/blob/feature/wol-multi-gpio-v1.8.1/docs/releases/reliability-r6-investigation.md)
distinguishes proven defects from hypotheses.

### Installation: which file to download

**First installation from original OpenMQTTGateway: USB.** Download
`OpenMQTTGateway-v1.8.1-ESP32-CC1101-WOL-2IN-2OUT-BLE-R6-USB-Windows.zip`, extract
**all files**, connect a USB data cable and double-click `FLASH-USB-WINDOWS.bat`.
Choose the correct COM port when asked. The ZIP includes the portable tool,
bootloader, partition table, OTA bootstrap, application and instructions;
Python and PlatformIO are not required. Use `RIPROVA-USB-ROM.bat` only if the
standard launcher fails, following `LEGGIMI-PRIMA.md`. No full-flash erase is
requested, so existing settings are normally retained. Not for ESP32-S2/S3/C3.

**Already using this custom edition:** download
`OpenMQTTGateway-v1.8.1-ESP32-CC1101-WOL-2IN-2OUT-BLE-R6-WebUI.bin`, open
**Firmware Upgrade → Local firmware file**, select that `.bin` and install.
Do not upload the ZIP. Original OpenMQTTGateway 1.8.1 does not have this local
upload form. URL update also accepts the direct `.bin` download URL; do not use
the GitHub release-page URL. Wait for installation/restart before navigating
away. USB is not required for subsequent custom-edition web updates.

`SHA256SUMS.txt` covers this release's two downloads. The
[NO BLE release](https://github.com/Mattboxx/OpenMQTTGateway/releases/tag/v1.8.1-esp32-cc1101-wol-2in-2out-no-ble-r6)
has the same WOL, WebUI and two-input/two-output features, without BLE scanning.
