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

### Reliability revision 6 (October 2026)

Revision 6 is the current public release. BLE and NO BLE keep the same WOL,
two-input/two-output, WebUI and Home Assistant features; only BLE tracking
differs. All three presets compiled, including the unchanged upstream
multi_receiver preset. Both custom variants were installed and returned to BLE
through the existing local-file WebUI, without USB or changed sensor settings.
All applicable web pages/fragments were complete, MQTT was connected and
RTL_433 was active. The BLE controller resumed scanning afterward.

The RF setup-only template table is released after independent decoder
registration, recovering 17,584 bytes for the 157-entry OOK inventory without
removing protocols. Unused demodulator metadata was also compacted; actual pulse
arrays remain full size. RF copies check allocation and preserve 12,000 bytes
of usable network headroom. A static network-timer reserve addresses a decoded
lwIP allocation panic; WebUI writes and OTA have bounded memory/time behavior.
The pinned multipart parser now checks binary boundaries without reading past
the buffer, and the WOL MAC parser and one-attempt rollover cases are fixed.
Routine BLE messages are verbose-only, while actionable errors remain visible.

Fourteen native reliability programs, eight RF patch-generation checks, four
WebServer generation checks, and actual RF allocation/ownership, BLE, WOL and
GPIO/discovery function tests passed. Both complete eleven-file USB installers
passed the pre-serial integrity checks. Host tests mock hardware and do not
prove electrical behavior, every RF protocol or FreeRTOS interrupt scheduling.
The owner requested publication without a scheduled multi-day observation;
no scheduled monitoring remains. Initial BLE observations showed approximately
41 KB of usable memory, zero allocation failures and zero Wi-Fi disconnects.
Multi-week operation is not validated, historical outages may have had more
than one cause, and this is not a guarantee of zero bugs or impossible hangs.
See the [investigation record](../releases/reliability-r6-investigation.md) and
the per-variant GitHub release notes for exact test scope and installation.

### Reliability revision 5 (September 2026)

**Validation in progress — not a stable release.** All three target builds and
five native test programs pass. On intermediate revision 4, 51 mixed read-only HTTP requests
completed successfully, but a subsequent repeated-GPIO-page test was interrupted
when the BLE device stopped answering HTTP and ICMP. The router and repeater
remained reachable; automatic recovery had not been observed at the last check.
After power cycling, the saved crash matched the old dump byte for byte and no
new persistent incident was available. Revision 5 removes two dependencies from
the emergency guard: heap-lock acquisition and normal radio shutdown callbacks.
It uses cached memory samples and the pinned IDF's low-level emergency restart
when preserving an existing dump. This hardens recovery but does not establish
the cause of the offline event or prove recovery from every failure.
The later multi-day offline event was reported only after the device had been
restarted, so it has no matching live log. Source review did identify a strong
memory-pressure candidate: RTL_433 Home Assistant discovery kept every unique
device/field in separately allocated heap objects without any limit or cleanup.
Nearby transmitters and identifiers that change over time could therefore leave
permanent allocations and increasingly fragmented heap. This fits the earlier
confirmed `std::bad_alloc`, although the missing outage log means it cannot be
proven to be the sole cause.

Revision 5 was installed by local OTA and checked with light HTTP requests:
MQTT connected, BLE advertisements and matches advancing, GPIO page and BLE
configuration fragment complete. The emergency reset itself was not deliberately
triggered on the deployed device. The NO-BLE image was compiled but not installed
in this validation session. Revision 5 is published as two separate pre-releases;
the preceding public releases have not been replaced.

A saved crash decoded against its exact original ELF confirmed an uncaught
`std::bad_alloc` while `stateMeasures()` serialized a system MQTT message into
the outgoing queue. The queue now uses fixed slots and one checked payload
allocation: exhaustion, a full queue or failed serialization reject the message
and increment `msgblck` instead of throwing. Those early candidates preserved
24,000 bytes of heap on queue admission (the generic default remains 8 KiB).
The current R6 presets use 12,000 bytes after the RF memory improvements.
This is a precaution, not a guarantee that other allocations succeed.
The cause of the preceding memory pressure has not yet been established.
The saved panic occurred after the initial outage and does not prove that the
initial outage had the same cause.

Both custom variants include a two-level runtime progress guard. If the main
loop or an OTA write stops making progress for 120 seconds, an independent task
records the stalled phase and requests recovery without waiting for MQTT or the
log subsystem. A second RTC hardware watchdog is fed only by real main-loop or
OTA progress. It performs a full reset after 180 seconds even if FreeRTOS, a CPU
core or the radio/Wi-Fi driver is too blocked to schedule the software guard.
Active OTA writes refresh both guards.

The custom presets also ping the DHCP default gateway once per minute. Three
consecutive failures force a Wi-Fi reassociation even if the ESP32 driver still
reports `WL_CONNECTED`. This covers the otherwise invisible state where the
main loop remains healthy while both WebUI and MQTT have disappeared from the
LAN. It is independent of MQTT, so a deliberately powered-off broker can still
remain offline for WOL without causing firmware restart loops. `/diag` reports
the target, replies, timeouts and recovery count.

On 1 October 2026, a new saved panic from `r6-test2` was decoded against its
archived ELF. The complete checksum-verified report showed
`tcpip_thread -> sys_check_timeouts -> tcpip_tcp_timer ->
sys_timeout_abs`, ending in `MEMP_SYS_TIMEOUT is empty`. The pinned SDK uses
`MEMP_MEM_MALLOC=1`, so this means a timer allocation failed in the shared heap,
not simply that a configurable fixed timer count was reached. The precise
allocation that consumed the remaining memory was not recorded.

The custom presets now use linker wrapping to reserve 32 statically allocated
`sys_timeo` objects. This intercepts timer allocation and freeing from the
precompiled lwIP archive, while leaving other network pools on their SDK
allocator. The object size is checked against the linked SDK before use;
unexpected capacity exhaustion falls back to the original allocator and is
counted. `/diag` reports `timer_reserve`, capacity, current/peak usage and
fallbacks. It also records failed heap allocations without logging or allocating
from the failure callback. This targets the confirmed panic path but does not
prove that every historical outage shared that cause.
`alloc_trace_pc`, `alloc_trace_task`, `alloc_trace_ms`, `alloc_trace_bytes` and
`alloc_trace_caps` identify the latest throttled allocation-failure call path.
Decode those program addresses using the exact deployed firmware ELF; they
are captured at most once every ten seconds, without filling normal logs.
The raw failure counter includes unsuccessful preferred-allocation attempts
which can sometimes be followed by a successful fallback.

The test4 trace identified failed Wi-Fi receive-buffer allocations. Custom
presets therefore limit dynamic Wi-Fi RX/TX buffers to 2/2, static RX buffers
to 2, and the receive Block Ack window to 2. AMPDU receive/transmit aggregation
is disabled on these low-throughput presets: Espressif recommends disabling it
when using fewer than six static RX buffers. This saves approximately 3.2 KB
of permanently allocated DMA memory versus Arduino's four static RX buffers,
as well as bounding dynamic packet bursts. Static TX buffers, SDK callbacks,
security settings and initialization magic remain untouched. These limits favor memory
headroom over bulk transfer throughput. `/diag` reports whether the budget was
accepted and the configured limits. `heap_default`, `min_heap_default` and
`max_alloc_default` measure the allocation capabilities actually requested by
these packet buffers; Arduino's broader internal-memory metrics include RAM
which cannot satisfy ordinary byte-addressable allocations. See
[Espressif's buffer guidance](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-guides/wifi.html#wi-fi-buffer-usage).
Connected discovery announcements in these presets are sent one at a time
from the main task instead of accumulating the full startup batch in the queue.
Queue admission reserves 12 KB in that usable allocation class; sustained
low-memory recovery uses a 6 KB threshold. A permanently blocked queue cannot
defer recovery indefinitely. BLE/GPIO state messages rejected by the queue
remain pending and retry their latest state once per second, without repeating
physical GPIO actions.
WebUI sends also leave 6 KB of byte-addressable RX headroom and a contiguous
RX-sized block before enqueueing another outbound chunk. Slow-client retries
remain bounded; incoming TCP ACKs need memory to reclaim outgoing buffers.

RTL_433 discovery now uses 32 fixed records and a statically allocated mutex:
received radio data can no longer grow this part of the heap. Pending discovery
records are protected; after publication, the least-recently-seen record can be
reused. Oversized or excess identities are safely ignored and counted. RF state
reports expose `rtl433_discovery_cached`, `rtl433_discovery_capacity`,
`rtl433_discovery_dropped` and `rtl433_discovery_evicted`. The WebUI message
formatter also extracts topic names without its former per-message
`strdup`/`strtok` allocation.

On the next warm boot, a stalled-loop incident is copied from RTC memory to NVS.
The first failed Wi-Fi recovery window in an outage is recorded in a separate
NVS slot, so it can no longer overwrite the more important stalled-loop or
hardware-watchdog evidence. These small records are not a continuous log of
every packet. They include firmware version, uptime, phase, memory and the last
Wi-Fi failure reason.

Open **Information > Recovery diagnostics (JSON)**, or `/diag`, to read the
guard status and the last incident. **Download saved crash report**, or
`/crash.bin`, downloads an existing ESP32 core dump without erasing it. A 404
means no readable saved crash is available. These endpoints use the same
authentication setting as the WebUI. A dump can contain sensitive RAM data;
keep it private and pair it with the exact firmware ELF for decoding.
For a slow VPN, `/crash.bin?offset=0&size=1024` retrieves an individually
retryable slice (maximum 4096 bytes). Concatenate slices in increasing offset
order and verify the complete dump's checksum before treating it as complete.
An IP routed by Tailscale may work even when `.local` multicast name resolution
does not; partial remote HTTP transfers alone do not establish a radio fault.

The revision also fixes an out-of-bounds terminator on long serial log lines,
overlapping log-buffer writes, unchecked console mutex
failures, a BLE WebUI pause that could remain set, concurrent uptime accounting,
rollover-unsafe periodic timers and a shared-queue race during restart. BLE scans
pause during Wi-Fi recovery and resume afterwards. Routine BLE logs remain at
verbose level.
The BLE preset keeps Wi-Fi preferred in the coexistence scheduler, including
after WebUI requests; advertisements remain best-effort. Beacon timeouts were
observed, but the available evidence does not prove BLE caused them.

Web responses use bounded 512-byte socket writes with an eight-second deadline
per write call, retrying temporary network-buffer pressure and short writes.
This avoids the bundled core's unchecked short-write path. HTTP tests on the
intermediate revision reproduced truncated GPIO pages without a device reboot.

These fixes address verified source defects. The September 13 outage was not
captured before power cycling, so it cannot be assigned to one specific defect.
After that power cycle, the device recorded 14 Wi-Fi disconnection events with
a final four-way-handshake timeout; those events belong to the new boot.

### Test coverage

The reliability branch includes native regression tests for queue allocation failures, full
queues, serialization failure, FIFO ownership and 100,000 wrap iterations;
bounded console buffers including long lines and malformed input; and the
runtime heartbeat timeout including the millisecond counter rollover. The fixed
discovery-cache test covers capacity exhaustion, protection of pending records,
safe eviction and clock rollover. Socket tests cover partial writes, transient
errors, byte order and bounded timeouts.
No test can establish zero bugs or replace a multi-day deployment soak.

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
