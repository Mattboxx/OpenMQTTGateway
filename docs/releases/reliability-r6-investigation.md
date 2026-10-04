# Reliability investigation — r6 candidates

This is an investigation record. The R6 release notes describe the public
release; candidate measurements below retain their original test scope.

## Evidence collected on 1 October 2026

The device at `192.168.1.57` responded through a Tailscale subnet route, while
`omg-multi-receiver.local` did not resolve. It ran `r6-test2`, with MQTT connected,
about seven hours of current uptime and reset reason 4 (panic). The saved core
dump had changed from 24,100 to 25,700 bytes. Diagnostics showed roughly 30 KB
free heap, 12,692 bytes minimum heap, two network recovery attempts, and BLE
advertisements/matches advancing. These are measurements after recovery, not
the heap state at the earlier crash.

The largest download contained the first 13,312 bytes of the dump. Its ELF
program headers, all register notes, and the first task's entire TCB/832-byte
stack were present. A separately labelled partial ELF with unavailable bytes
zero-filled allowed GDB to decode that complete crashed-task stack against the
archived installed firmware ELF:

```
tcpip_thread
  tcpip_timeouts_mbox_fetch
    sys_check_timeouts
      tcpip_tcp_timer
        sys_timeout
          sys_timeout_abs
            assert: timeout != NULL, pool MEMP_SYS_TIMEOUT is empty
```

Other tasks' missing stack bytes were not interpreted. After installing the
sliced-download handler in test3, all 25,700 bytes were retrieved in 1024-byte
chunks. `esp-coredump` validated the complete CRC and decoded all 17 tasks
against the archived test2 ELF. This independently confirmed the same panic.
The main loop was in the normal RTL_433 polling delay; the dump did not show
a main-loop stack overflow. Raw reports, exact firmware files and decoded
backtraces are kept private outside Git under `diagnostics/`.

The linked Arduino SDK's `lwipopts.h` sets `MEMP_MEM_MALLOC=1` and
`MEM_LIBC_MALLOC=1`. Its `memp_malloc` allocates these timer objects from heap.
The observed panic therefore establishes failed allocation of a critical timer;
it does not identify the producer of the memory pressure. Simply adding a
`MEMP_NUM_SYS_TIMEOUT` compiler definition would neither reserve memory nor
rebuild the precompiled SDK.

## Changes in r6-test3

- Reserve 32 network timer objects in static storage and wrap both allocation
  and freeing from the SDK archive. Check linked timer size and expose actual
  usage, peak and fallback count.
- Raise custom-preset queue admission headroom from 8192 to 24,000 bytes.
- Record failed allocation count, size, capability mask and API in `/diag`
  without allocating or logging from that callback.
- Correct a network-recovery clock sampling race and clear ping failures from
  an already recovered association outage.
- Add individually retryable crash-download slices for slow remote links.

Earlier r6 source changes bound the RTL_433 discovery cache, remove topic-name
allocation from the WebUI formatter, retain Wi-Fi coexistence priority, add
hardware progress recovery and preserve Wi-Fi/severe incidents separately.
The discovery cache was empty in the current measurements, so its old growth
defect is not proven to have caused this particular panic.

## Validation scope

Seven deterministic host regression tests passed, including timer storage
alignment, exhaustion, reuse and foreign-pointer rejection. Both BLE and no-BLE
test3/test4 presets compiled successfully. Inspection of the linked test3
instructions confirmed that both `sys_timeout_abs` allocation and
`sys_check_timeouts` freeing call the wrappers, including calls from the
precompiled SDK archive.

Test3 was installed using local OTA through the Tailscale route. At about five
minutes, the device had 6 of 32 timers in use (peak 6, no fallbacks), zero Wi-Fi
disconnections/recovery attempts, and an advancing BLE advertisement counter.
MQTT reconnected. Allocation failures continued after boot (2268 at about
310 seconds), so the underlying memory-pressure producer remained unresolved.
Free heap measured between requests does not describe instantaneous heap use.

Test4 adds eight allocation-caller PCs and the originating task name, captured
at most once per ten seconds with fixed storage and no printing or heap queries
inside the callback. These traces require the exact test4 ELF for decoding;
they remain outside normal BLE/MQTT logs. Failed preferred allocations may
be followed by a successful allocator fallback, so the counter alone is not
proof that every request ultimately failed.

## Test4 evidence and test5 changes

Test4 ran for about 4 hours 47 minutes without a new reset, but reported 90,575
failed allocations and one Wi-Fi disconnection. Its throttled trace consistently
identified the `wifi` task requesting 2308 bytes with capabilities `0x1800`:

```
wDev_IndicateFrame -> esf_buf_alloc -> esf_buf_alloc_dynamic
  -> wifi_malloc -> malloc -> heap_caps_malloc_default
  -> heap_caps_malloc -> heap_caps_alloc_failed
```

This establishes receive-buffer allocation pressure in that run, not a BLE
object leak or proof that all earlier outages had this exact trigger. Arduino
2.0.7 configures 32 dynamic receive and 32 dynamic transmit buffers. Its
`ESP.getFreeHeap()` uses `MALLOC_CAP_INTERNAL`, including 32-bit-only regions
that cannot satisfy ordinary byte-addressable packet allocation. Test5 exposes
`heap_default`, `min_heap_default` and `max_alloc_default` for the actual
`MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT` allocation class.

Test5 wraps the core's `esp_wifi_init` call, copies its SDK configuration and
bounds dynamic receive/transmit buffers to 6/4, and receive Block Ack window to
4. Existing smaller limits are respected, dynamic RX is never configured below
the static RX count, and static/security/function-pointer settings remain
unchanged. If the SDK rejects the configuration as invalid, the wrapper retries
the exact original defaults and reports the budget inactive. Both custom
presets opt in; the original upstream preset does not.

The custom presets also stream connected Home Assistant announcements from the
main task. Their whole startup discovery batch no longer accumulates in the
queue, where the higher memory-admission threshold was rejecting the tail.
Offline or failed publications still use the normal checked queue. The MQTT
connection callback does not recursively invoke the MQTT loop.

An eighth host test validates buffer limits, Block Ack compatibility, smaller
existing limits and preservation of unrelated SDK configuration. The limits
trade peak throughput for memory headroom, and require live verification.

## Test5 results and test6 changes

Test5 accepted the 6/4 limits and remained connected, but after roughly six
minutes still reported 1120 allocation failures on the same Wi-Fi receive path.
The ordinary allocator class had about 14.7 KB free between requests and a
minimum of only 1072 bytes, despite the broader metric reading about 29.5 KB.
Thus the initial zero counter before full radio startup was not a stability
result. Exact test5 ELF/images and before/after diagnostics were archived.

Test6 reduces the dynamic RX/TX budget to 4/2 (receive BA window 4). Queue
admission now measures the actual DEFAULT|INTERNAL heap and keeps 12 KB of
that class available rather than applying a 24 KB threshold to the broader
metric. Sustained-low-memory recovery uses the same class, at 6 KB, with its
existing three one-minute samples and startup grace. A permanently blocked
queue no longer suppresses that recovery indefinitely.

BLE presence and GPIO input/output state publications that are rejected by
the checked queue now remain pending and retry at most once per second, using
the latest state. GPIO retries do not repeat physical output writes or input
actuator triggers. Failed direct discovery restores its routing/retain fields
before queue fallback, because the normal publication function consumes them.
A brief live test cannot establish multi-day stability or absence of all bugs.
The existing published r5 pre-releases remain available during this investigation.

## Test6 results and test7 changes

Test6 booted with the 4/2 budget active and MQTT reconnected. Read-only requests
to `/up`, `/gi` and `/bt` still returned incomplete HTTP bodies (2560/2874,
1306/4698 and 1335/2700 bytes respectively). At about 201 seconds its allocation
counter reached 3690, again from the Wi-Fi RX path. The VPN route was verified
working before these tests; a separate failed upload while the user's VPN was
off transferred zero firmware bytes and is not evidence of a firmware failure.

Test7 checks DEFAULT|INTERNAL free memory and contiguous allocation headroom
before each WebUI send. It leaves a 6 KB receive reserve and a 2308-byte RX
block plus overhead, allowing ACK reception to reclaim transmit buffers.
Temporary pressure uses the existing bounded retry deadline rather than
allocating more response copies. Successful chunks also yield at least 10 ms
on the custom presets. This addresses a possible self-starvation path in which
the outgoing TCP response consumes memory needed to receive its own ACKs;
the exact source of every historical outage remains unproven.

A ninth host regression exercises reserve boundaries, fragmentation, oversized
requests, delayed-ACK reclamation, byte order and bounded waits. These tests
do not establish that the reserve survives every concurrent radio workload.

All three test7 builds passed: both custom variants and the unmodified upstream
`esp32dev-multi_receiver` preset. Exact ELF/application/bootloader/partition
files and complete Windows USB packages were archived outside Git before OTA.

Initially test7 could not be installed remotely. The working
Tailscale route returned diagnostics and 1200-byte pings, but five multipart
OTA attempts aborted part-way, including bounded-rate and HTTP/1.0 transfers.
The installed test6 restarted and remained installed each time. The first
1024 bytes of its retained core dump still matched the earlier test2 dump, so
these resets are not evidence of a newly recorded panic. The pinned WebServer
invokes `UPLOAD_FILE_ABORTED` on a negative upload read, and the current local
OTA callback immediately restarts on that event; no persistent upload-error
record is available to establish which network/read failure triggered it.
Requests were stopped to avoid further remote update/restart cycles.

## Local test7 verification and test8 changes

Once the user returned home, the Windows route still preferred Tailscale.
Binding the HTTP client to its Wi-Fi address selected the local connection
without changing VPN settings. This installed test7 successfully in about
31 seconds. A subsequent `/diag` confirmed the exact test7 version.

Fifteen read-only responses (main/system/upgrade/GPIO/BLE pages, CSS, all input,
output and BLE row fragments, and BLE candidates) returned their complete
HTTP bodies, with 12 allocation failures by uptime 70 seconds and no Wi-Fi
disconnections. MQTT was connected and configured BLE advertisements matched.
Repeating `/up`, `/gi` and `/bt` over the default Tailscale route also returned
complete bodies, but `/bt` took eight seconds. Allocation failures reached
280 by uptime 142 seconds and 391 by 335 seconds, with minimum usable heap
972 bytes. Therefore complete page delivery is not sufficient evidence of
resolved memory pressure; neither radio contention nor multi-day stability
has been ruled out.

Test8 reduces the custom presets' static RX DMA buffers from Arduino's four
to two (roughly 3.2 KB permanent saving), dynamic RX/TX budgets to 2/2 and BA
window to two. RX dynamic capacity covers static RX capacity. Both AMPDU
receive and transmit aggregation are disabled, following the pinned IDF's
guidance for small static RX budgets. This is a low-throughput gateway profile,
not a bulk-transfer optimization or a change to upstream presets. `/diag`
reports the accepted static RX and aggregation settings too. Security/callback
and static TX settings remain unchanged; invalid-argument initialization still
falls back to the exact SDK configuration. All nine host tests pass, including
the updated buffer-policy tests; live verification remains necessary.

Sources: [pinned IDF Wi-Fi buffer guidance](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-guides/wifi.html#wi-fi-buffer-usage),
[pinned IDF configuration bounds](https://github.com/espressif/esp-idf/blob/v4.4/components/esp_wifi/Kconfig).

## OTA investigation and test9

Test8's three local multipart attempts also aborted before completion, including
a capped-rate request and a paced raw HTTP client. Test8 was not installed;
the device returned to test7 after each abort. Thus the VPN is not established
as the sole cause of the interrupted updates.

The workstation has both Arduino-ESP32 2.0.7 (the versioned package actually
used by this project) and 2.0.17 (the unversioned package). Initial inspection
of the unversioned WebServer parser was incorrect for this build. The pinned
2.0.7 parser already retries temporary negative reads, unlike the inspected
later parser. It does, however, consume multipart uploads inside `_parseRequest`
before `handleClient` sets the longer stream timeout. Its inherited Stream
timeout defaults to only 1000 ms. Radio/retransmission pauses can therefore
abort a large upload even though the intended response timeout is longer.

Test9 accepts a pending client using the same protected fields as the pinned
base server, and sets its seconds-based timeout to ten seconds **before**
parsing. The original retry implementation and bounded idle termination remain
in use; no framework package or vendored parser replacement is needed.
Only the custom presets opt into this setting. All nine host tests passed
for the memory changes; this server integration needs an actual OTA test.
No claim is made that every observed abort was caused by this timeout.

The URL upgrade path also releases the lightweight BLE controller before
allocating HTTP/TLS and flash-update buffers, matching local-file OTA. Plain
HTTP now leaves reboot/config-save bookkeeping to the same application path
as HTTPS. A short URL is length-checked before checking its `.bin` suffix,
avoiding the old pointer-underflow read. WebUI update-request logging no longer
serializes the JSON object containing the OTA password.

The user requested web-only installation. A temporary LAN HTTP helper can
serve only the chosen application at `/firmware.bin`, with Content-Length and
MD5 headers and a paced body. The existing URL-upgrade path then pulls the
image, bypassing the older multipart parser. Stop the helper after verifying
the installed version; do not publish private diagnostics or ELF/core dumps.

Related upstream report: [large-file OTA and parsing timeouts](https://github.com/espressif/arduino-esp32/issues/9990).

## Web-only deployment evidence, 1–2 October

All three presets (upstream multi receiver, custom BLE and custom no-BLE)
compiled successfully with the final test9 sources. The nine current native
tests passed. Exact images/ELFs and complete nine-file Windows USB packages
were archived privately; compilation is not live validation.

The device reached the temporary single-file HTTP endpoint on the local PC.
The initial URL transfer closed after 68,096 bytes and the old application
returned. This rules out an inbound firewall block for that request, not all
network or OTA failures. Preparing OTA through the older local-file handler
with a rejected non-image upload released the BLE controller without changing
saved sensor settings or writing a bootable image. `/diag` then measured
52,748 usable bytes and `ble_started=false`, versus roughly 15 KB normally.
Nevertheless two subsequent URL downloads stopped at 394,240 and 416,768
server-side bytes respectively. Server bytes are not verified flash progress.

After recovery, the persistent diagnostic explicitly reported `loop_stalled`
in phase `ota`, with no progress for 120,396 ms (and 120,935 ms on the later
attempt). The software guard recovered the old test7 application. This is
stronger evidence of an OTA stall than a mere lost web response; it does not
identify the blocked instruction. The server was stopped after the tests.
No test9 installation was established from these attempts.

On 2 October the PC's LAN address changed from `.61` to `.121`. A fresh local
query found test7 online for 30,260,403 ms, 3,133 failed Wi-Fi allocations,
minimum usable heap 512 bytes and two Wi-Fi disconnections (last reason 200).
The neighbor entry now showed the ESP's own MAC rather than the extender's
proxy MAC observed previously. This changes the available network path, but
does not by itself prove the extender caused the failures. A new local-file
deployment was attempted on this directly observed path.

That multipart request reset after 1,310,518 wire bytes and 67 seconds; no
successful response was received. A slow URL transfer initially failed after
2,048 server-side bytes. Inspection of this repository's `zzHTTPUpdate.cpp`
(not the SDK's separate HTTPUpdate class) found a fixed 100-ms delay followed
by `peek()!=0xE9`: absence of the first packet was treated as bad magic. Enabling
TCP_NODELAY on the temporary server avoids small-body Nagle/ACK delays on this
older client. With 1,024-byte blocks every 125 ms and BLE released (53,240 usable
bytes measured before download), the server transmitted all 1,831,680 bytes.
This is NOT yet proof of flash completion or a test9 boot; initial post-transfer
HTTP observations timed out. The server was then stopped.

Test10 replaces the fixed-delay header assumption with a bounded wait, preserving
the byte for Update's own validation. Its URL transfer uses 512-byte stack reads,
yields between accepted writes, renews its 15-second idle deadline only on actual
flash acceptance, and has a 15-minute total deadline. It drains buffered final
bytes after disconnect, retries transient negative reads, aborts partial flash
updates and retains MD5/image validation before boot-slot activation. It also
reports allocation/start failure even when the SDK's Update error remains zero,
resets stale HTTP errors per request and aborts an invalid MD5 setup. Ten fresh
native regression tests passed. Live integration still needs validation.

The later `/diag` confirmed test9 booted, with RX/TX/static RX/BA budgets 2,
both AMPDU modes disabled, 19,448 usable bytes at uptime 52 seconds and no
allocation failures/disconnections then. Fifteen read-only page/fragment/CSS
responses completed and `/in` confirmed MQTT connected. After that test there
was one failed 2,308-byte Wi-Fi allocation and minimum usable heap 2,700 bytes,
without disconnection/reboot. This is an improvement, not elimination of memory
pressure or evidence of multi-day stability. GPIO outputs were not actuated.

The subsequent test9-to-test10 multipart attempt reset after only 65,536 wire
bytes (19 seconds). Slow URL attempts without and with the preparatory BLE
release also failed, at 78,848 and 124,928 server-side bytes respectively. A
fresh diagnostic recorded another 120-second OTA-phase stall in test9. Test10
therefore remains unverified on the device. Both final test10 variants and the
upstream preset compile; their exact images/ELFs and nine-file USB packages are
archived, and all ten fresh host tests pass.

Routing diagnostics confirmed that constraining the source to the PC's LAN IP
selects interface 17 (Wi-Fi), while the unconstrained route selects interface 27
(Tailscale). Merely observing a local IP was not used as route proof.

Review of pinned rtl_433_ESP found `disableReceiver()` detaches the RF GPIO
interrupt. Neither local-file nor URL OTA currently calls it; the loop's
ProcessLock does not detach that ISR. A controlled test used the existing web
console command `commands/MQTTtoRF/config {"active":0}` without `save`, then
verified RF active=0 and MQTT connected in `/in`. No GPIO outputs were switched
and saved RF settings are unchanged; reboot should reload the original receiver.
This tests RF interrupt interference, not proof that it causes every OTA stall.

The RF-paused multipart test also reset at 65,536 wire bytes (12.6 seconds).
After reboot `/in` confirmed original RF active=3 and MQTT connected again.
Disabling RF alone therefore did not resolve this upload failure; it must not
be presented as the established cause. The candidate is not a final release.

A comparison through the in-app browser reached the actual test9 upgrade page,
but the supported file chooser returned without selecting a file on either
Windows-path spelling. DOM inspection confirmed an empty file list. No install
button was pressed with an empty selection. Manual browser selection is the
remaining comparison; browser upload success has not been claimed.

## 3 October: USB deployment of test10

With explicit authorization to flash over USB, COM4 identified an ESP32-D0WD-V3
revision 3.1 with the expected device MAC. Stub-based full-flash reads failed
(one stalled at 16 KB; the 115,200-baud retry reported corrupt data). This is
not evidence that the saved firmware itself is corrupt. ROM/no-stub reads at
115,200 baud succeeded for the 20 KB NVS partition, the existing partition
table and the 128 KB SPIFFS partition. Those private backups are not a complete
flash backup and must not be published because they may contain credentials.

The existing and candidate partition layouts matched. ROM/no-stub writing at
115,200 baud successfully programmed and verified the packaged bootloader,
partition table, OTA selection data and BLE test10 application. NVS, SPIFFS,
the other application slot and the saved crash partition were not erased.
Application SHA256 before programming:
`6B513F3BF69FA59E3985FA1B2CB8B32E1D5BC9D15A74AAA6971E84E2632C6FCF`.

After reset, `/diag` positively identified test10. The home, information, GPIO,
BLE and firmware-upgrade pages returned HTTP 200 with complete closing HTML;
GPIO took about 4 seconds, the others 0.2–1.2 seconds. `/in` confirmed MQTT
connected and the original RF receiver active=3. No GPIO output was actuated.
A 75-second bounded private serial capture contained no panic/error markers.
All ten current host-test executables also passed.

This is successful installation and an initial smoke test, NOT proof of
stability. At uptime 65.7 seconds, diagnostics already counted 23 failed
2,308-byte Wi-Fi allocations, minimum usable heap 2,536 bytes, no Wi-Fi
disconnections, two gateway replies and zero ping timeouts. Memory pressure
therefore remains despite complete web responses. Before this installation,
test9's retained Wi-Fi failure record showed 21 disconnections at approximately
22 hours of uptime, with last reason 200; test9 was not multi-day stable either.
Do not publish test10 as a validated final release on this evidence.

## 4 October: network deployment and further OTA investigation

The USB cable was then disconnected. Test10 was reachable after a power-on
boot; repeating the information/GPIO/BLE/upgrade page checks at uptime 130
seconds added zero allocation failures. The earlier failed allocations must
not be attributed conclusively to one page or to sustained idle operation.

Review identified 25 WebUI sites passing `measureJson(modules)+1` as the
capacity of a 100-byte C array. A module list longer than that array could
overwrite stack memory. Test11 uses an array-size-deduced serializer with a
valid empty-array fallback rather than an overflowing or truncated string.
Its host test checks normal modules, long lists, guard canaries, escaping and
exact terminator boundaries. This preset's seven-module list fits the old
array; the general bug is not established as this device's outage cause.
Diagnostics now expose the actual running OTA partition and address.

All three test11 presets compiled and all eleven host tests passed. A paced
32-KB/s local-file upload from test10 reset at 65,536 wire bytes (23.5 seconds)
and returned to test10. A 1,024-byte/125-ms URL transfer closed after 105,472
server-side bytes; the device recovered with requested restart reason 6 and
without a new 120-second OTA-stall incident. This confirms bounded recovery
on that attempt, not successful URL installation or its precise error.

The existing ArduinoOTA service authenticated with the saved gateway
credential, read privately from the previously backed-up configuration. The
credential was not logged, changed or placed on a command line. Both the TCP
listener and UDP invitation were constrained to the PC's LAN address.
This transfer completed and `/diag` later positively confirmed test11 running
from `app1`, address `0x1f0000`, without USB. Initial constrained HTTP queries
timed out; later default-route and constrained ICMP probes succeeded. Merely
observing the earlier timeouts was not treated as proof of a dead device.
Subsequent information/GPIO/BLE/upgrade pages completed in 0.22–0.45 seconds.

Test12 adds a fixed RTC OTA-attempt record, migrated to NVS once on the next
non-power-on boot. It records transport, stage before failure, accepted and
expected bytes, error and transfer-detail codes without credentials or URLs.
It does not write NVS on each chunk. All three presets compiled and all eleven
tests passed; repeated ArduinoOTA bootstrap attempts failed, including paced
and temporarily RF-paused tests. Test12 was NOT confirmed installed. RF pause
used a non-saved command; reboot restored RF active=3 and MQTT connected.

Test13 also fixes controller shutdown sequencing: do not merely clear the
software scan flag, and do not release controller memory after failed SDK
disable/deinit calls. Its bounded HCI pause waits for acknowledgement; an
unsettled initialization or failed SDK step rejects OTA and schedules recovery.
The deterministic host test injects failure at every stage and verifies that
later SDK operations are not called. This follows the documented requirement
to stop scanning before controller deinitialization:
https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32/api-reference/bluetooth/controller_vhci.html
It is a concrete API-lifecycle correction, not proof of the sole outage cause.
All three test13 presets compiled and all twelve freshly built host tests
passed. Its exact applications/ELFs and USB components are archived privately.

Both automated browser file-selection methods returned without selecting the
file; read-only DOM inspection confirmed `files=[]`, and no empty upload was
submitted. The user then selected test13 manually and started the web upload.
The browser reported connection reset. `/diag` positively confirmed that the
gateway recovered on test11/app1, not test13. A browser error alone was not used
to infer either successful installation or permanent loss of the gateway.

Further test13 bootstrap attempts over the existing ArduinoOTA service failed.
One paced 256-byte sender reported 5,120 bytes sent and 4,864 numeric response
bytes acknowledged before a ten-second receive timeout. That acknowledged
count assumes a complete response per receive; TCP does not guarantee that
framing. Another 512-byte/TCP_NODELAY attempt sent 124,928 bytes and timed out;
Windows TCP_INFO_v0 for that helper's own socket showed 10,040 retransmitted
bytes, three timeout episodes and 1,436 bytes still in flight, with a nonzero
send window. Its numeric response sum was clearly invalid because multiple
unframed responses could coalesce. This is evidence of transfer trouble, not
proof of a particular ESP32 driver bug, router fault or antivirus interference.
The TCP counters use the documented read-only socket interface:
https://learn.microsoft.com/en-us/windows/win32/api/mstcpip/ns-mstcpip-tcp_info_v0

A URL bootstrap with 256-byte blocks, 40-ms spacing, a one-second initial
body delay and temporarily paused RF stopped at 65,792 server-side bytes.
The gateway recovered on test11 with restart reason 6, MQTT connected, and RF
active=3 restored from its unchanged saved configuration. The temporary server
was stopped. Server-side sent bytes are not device-accepted flash bytes.

A separate private stream probe drains ArduinoOTA responses concurrently
rather than assuming TCP receive boundaries delimit acknowledgements. Its
initial one-second socket timeout was too short for backpressure: it exited
after sending 70,144 bytes, while socket counters showed only 14,548 bytes out.
A retry with a longer timeout was rejected before authentication because the
preflight could not confirm a paused scanner. Once the existing firmware had
completed its reboot/startup, the longer-timeout stream probe ran and was reset
after 9.6 seconds: 70,656 sender-side bytes, 18,856 TCP bytes out, 8,504
retransmitted bytes, 5,232 bytes in flight. No installation was claimed.
The exact test11 ELF also decoded a new post-recovery allocation trace through
`wifi_malloc -> esf_buf_alloc_dynamic -> esf_buf_alloc -> wDev_IndicateFrame`.
Three failed 2,308-byte allocations were recorded at boot uptime 14.98 seconds,
with minimum usable heap 2,668 bytes. Those are Wi-Fi frame-buffer allocations;
their timing does not establish that they caused the later OTA interruption.
Private probes and configuration/diagnostic captures remain outside the repo.
All twelve host tests were rebuilt and passed again after these attempts.
Web OTA is therefore still unvalidated; test13 must not be published as final.

After the final stream interruption, HTTP and ICMP initially failed. A later
read positively identified test11 and `/in` confirmed MQTT connected and RF
active=3. Its requested restart reason was 10, the existing 60-second module
initialization watchdog, not a confirmed newly captured panic. This shows that
recovery included an initialization timeout; it does not identify the module.

Source review for test14 removes blocking logging, heap queries and serial
flush from that startup watchdog's emergency path and uses the pinned SDK's
no-shutdown-handler reset in diagnostic-enabled presets. RTC reason 10 is still
retained. The normal non-diagnostic preset retains its existing SDK reset path.
Both remaining main.ino module-array serializers now use the bounded helper;
the startup call had the same potentially oversized capacity as the WebUI
calls, while the status call could truncate its output. The current preset
fits the old buffers, so neither is established as its specific outage cause.
ArduinoOTA now allows a bounded 15-second idle wait on ESP32 rather than its
one-second default. Authentication rejection no longer reboots a healthy
gateway or changes ProcessLock/previous OTA records, because it precedes the
SDK's onStart callback. These source corrections require build verification
and live integration testing; they do not remotely repair installed test11.

After correcting the ArduinoJson include placement to preserve the project's
64-bit integer setting, all three test14 presets compiled successfully (base
95.6 s, BLE 96.0 s, no-BLE 111.0 s). All twelve rebuilt host tests passed.
BLE application SHA256 is
`C6C7C32EFDE1A3C93872666BB4080B8D640D00CCF6ECCF08A1355D2931B7E088`;
no-BLE application SHA256 is
`9D04005E22FEEFF61C9333A8C48AF0598E46DFDFE1DC28630B8711A46B9F4ECB`.
The exact application/ELF/bootloader/partition artifacts are archived privately.

The user agreed to compare uploading from a smartphone on the same LAN. A
temporary firmware-only endpoint served the BLE test14 application; a PC
download later matched the archived SHA256, and the server also logged a full
1,833,520-byte transfer to a different LAN client. A completed download is not
proof that a phone's subsequent upload or the ESP32 installation succeeded.
That manual outcome and post-boot version verification remain pending.
The helper's single-threaded server had delayed requests behind silent browser
preconnections until its 45-second read timeout. Its source now uses a
threaded HTTP server with a ten-second per-connection timeout. This helper-side
correction does not by itself explain the earlier ESP32 transfer failures.

The user subsequently reported that the smartphone upload also left test11.
The post-attempt `/diag` confirmed test11/app1, and `/in` confirmed MQTT
connected and RF active=3. Diagnostics counted one failed 2,308-byte Wi-Fi
allocation at uptime 79.489 seconds in that boot; no new saved severe incident
or crash size was observed. The temporary endpoint was stopped. This comparison
makes a PC-browser-only explanation less likely but does not prove a sole
firmware cause or exclude the shared Wi-Fi path. Test14 remains uninstalled.
No further remote retry is justified without a new diagnostic hypothesis or an
external change. A wired installation is the remaining dependable bootstrap
option, followed by an actual web-to-web update test before making OTA claims.

The USB installer default is now the ROM/no-stub path at 115200 baud, matching
the successful wired bootstrap on this board rather than the earlier failing
RAM-stub attempts. It still writes only the four explicit package images and
does not erase saved configuration. The portable tool supports this option;
these packaging checks are not a new hardware installation or an OTA pass.

On the user's subsequent USB connection, the ROM loader identified the same
ESP32-D0WD-V3 revision 3.1 and gateway MAC. A fresh private 512-KiB read at
0x8000 preserved the partition table, NVS/OTA data and only a prefix of app0
(not a complete firmware backup). The installed partition table matched the
candidate exactly. A separate combined SPIFFS/core read was interrupted by
serial packet corruption; a fresh-reset retry of SPIFFS alone completed the
full 128 KiB. Partial captures are not described as complete backups.

Test14 was then written through ROM/no-stub at 115200 baud to the four explicit
package addresses. All four image hashes were verified by esptool, and the
device was reset. A bounded normal-reset serial capture positively identified
the running test14, module initialization completing in 2.957 seconds and no
panic/error markers during the 75-second capture. All twelve host tests were
rebuilt and passed again. These checks do not prove multi-day stability.

The old LAN URL did not become reachable. Serial diagnostics established a
different condition: the same configured extender SSID/BSSID supplied an IPv4
address and gateway on 192.168.0.x, while the PC and configured MQTT broker were
on 192.168.1.x. The gateway's Wi-Fi status was connected, but repeated MQTT
transport attempts failed. The user also reported problems with that extender.
The firmware does not compile a 192.168.0.100 static address into this preset.
This is evidence of changed LAN addressing/connectivity, not proof of exactly
which extender configuration or DHCP service caused it. No router, PC network
profile or saved gateway settings were changed to work around it. Web-to-web
OTA and sustained connected operation remain unverified pending LAN recovery.

After LAN recovery, `/diag` positively identified test14/app0 at uptime
10,906,459 ms without a connected USB port. The later preflight reported eleven
Wi-Fi disconnects, zero recorded allocation failures and minimum usable heap
5,104 bytes. This is not proof of multi-day stability or of a firmware cause for
the disconnects. A normal `/up` GET then stalled after 2,560 of 2,875 bytes.
Post-request diagnostics counted 472 allocation failures; the exact test14 ELF
decoded a failed 2,308-byte default/internal allocation through
`wifi_malloc -> esf_buf_alloc_dynamic -> esf_buf_alloc -> wDev_IndicateFrame`.

A standard multipart POST of the exact archived test14 application reset the
connection after 21.515 seconds. Post-boot diagnostics still showed test14/app0,
with a saved `web_file` attempt, stage 6 (failed), last stage 2 (receiving),
accepted_bytes=1,436 and error=-5 (parser/client abort). Sender-side upload
bytes were 130,870 and are not device-accepted bytes. No new severe incident
was inferred from the unchanged old test9 watchdog record.

A distinct URL test used the threaded firmware-only helper, 512-byte blocks
and 20-ms spacing. The helper logged a gateway GET and stopped at 66,048 sent
bytes. Post-boot diagnostics again showed test14/app0, saved transport `url`,
stage 6, last stage 3, accepted_bytes=512 of 1,833,520, error=-110 and detail=1
(idle timeout). The temporary helper was stopped. Both live OTA paths remain
failed; successful delivery of the small update-request HTML is not OTA success.
The pinned Update implementation buffers 4 KiB before ordinary block erases,
so these accepted-byte counts do not implicate a long first-block flash erase.

The next test15 hypothesis keeps the BLE controller initialized during OTA and
only acknowledges scan stop, consistently across local file, URL and network
OTA. This avoids a controller/PHY lifetime transition under an active Wi-Fi
transport, but the existing evidence does not prove teardown is the sole cause.
The exact pinned controller implementation can be inspected at:
https://raw.githubusercontent.com/espressif/esp-idf/v4.4.4/components/bt/controller/esp32/bt.c
The outgoing-response guard also leaves 12 KiB plus a two-RX-frame contiguous
block instead of 6 KiB plus one frame, addressing the observed receive-buffer
allocation pressure without increasing radio-buffer limits. Relevant allocation
ownership is documented at:
https://docs.espressif.com/projects/esp-idf/en/v4.4.4/esp32/api-guides/wifi.html#wi-fi-buffer-configure
These source changes are candidates, not a live OTA pass. Test14 artifacts remain
unchanged.

All three test15 presets compiled successfully (base 69.494 s, BLE 68.578 s,
no-BLE 69.824 s), and all twelve rebuilt native tests passed. BLE application
SHA256 is `448A37C1FC40BD3D3A5C83480EBF265E5CBF1AF1587C6B1BCEE07E9BBAE16229`;
no-BLE SHA256 is
`F7AEA0683F3D534CA13EB017D6950C35D7DEDBBAF5187D10E2B754EE2DBB31A3`.
Both private Windows USB packages contain the four required flash images,
portable flashing tool and guided instructions. These checks do not establish
installation, a working wireless update or multi-day stability.

With the user's USB connection, the ROM loader again identified the expected
gateway MAC. Fresh complete private reads preserved 32 KiB of partition/NVS/OTA
metadata and 128 KiB of SPIFFS; the partition table matches test15 exactly.
The first ROM/no-stub USB write at 115200 baud verified the bootloader,
partition and OTA metadata images, but the application write failed near 60%
with an invalid serial message (sequence 1094, result 0105). This is not a
successful installation. A no-reset reconnection failed, so an automatic
bootloader reset and application-only retry at 57600 baud were started. The
outcome and hardware/web validation remain pending.

The 57600-baud ROM retry also failed with result 0105 at sequence 711. A
stub-based compressed application-only write at 115200 baud then completed in
100.8 seconds and verified the application hash. Normal-reset UART capture
identified running test15/app0; module initialization completed and RF/MQTT
connected. The bounded ten-minute capture contained zero crash/error markers.
These serial-link failures are separate from the observed Wi-Fi OTA failures.

Test15's `/up` returned complete 2,875-byte HTML in 0.185 seconds with zero
allocation failures in the adjacent diagnostics. Normal multipart OTA still
reset the connection at 19.013 seconds (sender 130,870 bytes, device accepted
zero, saved failure -5). A paced 12-KiB/s comparison also failed at 20.039 seconds
(sender 73,728 bytes). The first post-boot diagnostic showed minimum usable
heap 1,528 bytes and failed 2,308-byte allocations in task `wifi`. Scan-only
pause therefore did not solve upload memory exhaustion.

A separate URL transfer pinned the Windows helper's response sockets to the
LAN interface using IP_UNICAST_IF, without changing Tailscale or system routes.
Binding a source address alone had not ensured this on the multihomed PC.
The gateway accepted 4,608 of 1,833,520 bytes but failed with -110/detail 3;
the helper stopped at 70,144 sent bytes and was shut down. Post-boot diagnostics
showed minimum usable heap 3,676 bytes and five failed 2,308-byte allocations
in the Wi-Fi task. This remains a failed OTA, not a successful installation.

Candidate test16 quiesces RF reception, stops MQTT transport, blocks new queue
admissions under the queue mutex and frees existing queued messages before
OTA. It returns to the acknowledged ordered BLE SDK shutdown to reclaim
controller RAM, rather than the failed scan-only experiment. All three OTA
paths share this preparation and restart after success or a preparation/transfer
failure; saved GPIO/RF/BLE/MQTT configuration is not modified. New diagnostics
report the prepared usable heap, largest block and discarded-message count.
Queue clear/reuse is covered by the rebuilt host regression tests. Hardware
and OTA validation of this new candidate remain pending.

Test16 compiled successfully for the base, BLE and no-BLE presets in 66.103,
64.533 and 63.514 seconds, respectively. All twelve native tests passed; the
updated queue test additionally confirms clearing all allocations and safe
reuse. BLE application size is 1,833,888 bytes, SHA256
`EC8D22EEE4C698FC03EA0E8117E6F6551C724C4B875274FD0D2B911500D249BA`, MD5
`049a106b2437d0f69da658b040aba4b7`; no-BLE SHA256 is
`4BE759E2D6CDFF70410E12FE6B29951CB14D261E7D148259FBD49E9B0548AF71`.
The exact image, ELF and matching bootloader/partition artifacts are archived
privately before the stub-based application-only wired installation attempt.

The BLE test16 stub write completed and its hash was verified in 101.0 seconds.
UART and `/diag` positively identified test16/app0, with RF and MQTT connected.
OTA preparation reported 44,948 usable bytes (largest 10,228); normal multipart
still reset at 19.034 seconds, accepted only 1,436 bytes and retained app0.
A second multipart comparison explicitly pinned its TCP socket to Windows LAN
interface 17 (verified with getsockopt), rather than merely binding an IP.
It also reset at 19.641 seconds; preparation reported 50,076 usable bytes.
Tailscale routing alone therefore does not explain these failed transfers.

A temporary cold-boot no-BLE test16 comparison was installed and hash-verified
through USB in 92.8 seconds, preserving saved configuration. `/diag` identified
no-BLE test16/app0 with 82,748 usable bytes and zero allocation failures. The
LAN-pinned multipart comparison still failed at 19.000 seconds, despite 74,904
usable bytes immediately before Update.begin. A separate LAN-pinned URL helper
comparison prepared 79,292 usable bytes but again accepted only 4,608 bytes,
ending with -110/detail 1; the helper was stopped. This falsifies an explanation
based solely on BLE teardown or insufficient total heap. Allocation statistics
read after a reboot describe that new boot and must not be attributed to the
previous transfer without a retained trace or contemporaneous UART evidence.

Candidate test17 restores four static RX and eight dynamic RX/TX Wi-Fi buffers,
matching the minimum non-PSRAM rank documented for the pinned SDK rather than
the earlier two-buffer limits. Buffer slots and usable heap are independent
constraints: the no-BLE comparison had ample memory but still stalled. This
is a new testable hypothesis, not a proven sole cause or a live OTA pass.
Source: https://docs.espressif.com/projects/esp-idf/en/v4.4.4/esp32/api-guides/wifi.html#minimum-rank
The twelve rebuilt native tests pass with the revised packet-slot limits.

USB packaging now defaults to the compressed RAM-stub method, after three
consecutive verified application writes in this session, and offers a separate
ROM retry launcher. Both methods preserve the configuration partitions. Script
syntax was checked; neither method is represented as immune to physical serial
link faults or as proof of full-package hardware installation.

Test17 compiled successfully for base/BLE/no-BLE in 93.793/92.757/92.020
seconds. BLE application SHA256 is
`9B30DC5CFD0BC64AEF067D9D01F2B4C6E6652096E5B7352B1C626E65B2AF993E`, size
1,833,824 bytes and MD5 `ec012c77abc5016480a421d7678d47de`. No-BLE SHA256 is
`D9C8513C49F8091B5F8D106802315FE8A60BFD8EF7C6BF3575C56E5564D5E068`.
The BLE application-only USB write completed and verified in 100.9 seconds;
UART and `/diag` identified running BLE test17/app0 with RX=8, TX=8, static RX=4.

For the first time in this investigation, an actual LAN-pinned multipart
transfer completed: HTTP 200, 1,833,824 image bytes sent, complete success HTML,
17.657 seconds. UART separately confirmed every image byte accepted, validation
with the exact archived MD5 and the deferred restart. A subsequent standard
curl multipart upload also completed in 20.131 seconds, with complete HTTP 200
success HTML and the same byte count and MD5 confirmed on the ESP32. USB was
used only to observe UART during both wireless transfers, not to deliver data.
The restored packet-slot capacity correlates with eliminating the reproducible
early-transfer stall; this does not prove the sole cause of every historical
disconnect or multi-day stability.

The post-update UART confirmed test17 restarted and RF/MQTT reconnected.
However, `/diag` and `/in` could produce incomplete responses: the 12-KiB and
two-contiguous-frame outgoing guard logged zero-byte writes while a complete
response buffer was still owned. UART identified the incomplete-body errors;
a small HTTP 200 header alone is not a valid diagnostics response. Candidate
test18 reduces the aggregate guard to 8 KiB and requires room for a separate
individual 2,308-byte RX allocation rather than two frames in one allocation.
The rebuilt guard test covers this admission case and bounded retry behavior.
Compilation, wireless installation and complete page validation remain pending.

Test18 subsequently compiled for base/BLE/no-BLE in 90.645/92.633/92.210 seconds.
The BLE application is 1,833,824 bytes, SHA256
`954AAFF84F1BB043875C31DCF1BA9A712763AF227BA28426DEDC54BA0AED4125`;
no-BLE SHA256 is
`14CEBB58838D06BCE75A93C1F657D336352962CA34539AC9123FDE6E818B7F43`.
A standard multipart upload from test17 to test18 completed in 17.208 seconds
with complete HTTP 200 success HTML. Contemporaneous UART confirmed every
application byte, exact MD5 `d25ffddd823aee791c0b82de5c94d842`, validation and
the test18 boot. `/diag` independently identified test18/app1 and retained OTA
stage 5 with 1,833,824 bytes, error 0. The recorded initiating version is test17,
not the newly installed firmware. After USB was disconnected, reset reason 1
identified a power-on; it is not evidence of a firmware panic.

Root, update, BLE, console, GPIO and information pages returned complete HTML.
All two input rows, two output rows, four BLE rows and candidate fragments
returned HTTP 200 with complete response lengths, without changing settings or
actuating GPIO. The slowest first GPIO row took 11.937 seconds; other rows took
0.056 to 2.695 seconds. Continued diagnostics still showed substantial Wi-Fi
allocation pressure: 2,446 failed allocations by approximately five minutes,
minimum ordinary usable heap 1,280 bytes. Passing OTA alone therefore does not
justify final publication or a claim of multi-day stability.

Candidate test19 applies a version-checked PRE-build patch to the pinned
rtl_433_ESP v0.3.3 dependency in the two derivative presets only. The ESP port
copies a full pulse_data_t into dm_state but its active callback reads only
signalRssi and signalDuration. The patch retains those values, removes only
that redundant storage, and leaves real pulse/gap arrays, OOK/FSK demodulators
and protocol support unchanged. It also checks failed pulse storage/copy
allocations before use and requests byte-addressable internal RAM for data
containing floating-point fields. Unknown dependency changes cause the build
to fail rather than being overwritten. Compilation and live memory/RF/OTA
validation of test19 remain pending.

Test19 compiled successfully for base/BLE/no-BLE in 107.996/140.012/149.368
seconds. The unmodified base preset's three corresponding dependency files
still match the original source hashes. Four dependency patch tests, both
compact/default actual-header tests, the actual copy-branch allocation failure
test and the twelve rebuilt native regression tests passed. The header test
measured 9,680-byte real signals, an 8-byte metadata copy and 9,672 bytes saved
on the Windows host; the real input arrays were not reduced. The fault test
executes the extracted patched branch with mocks, including failed allocation,
slot release, warning throttling/rollover and subsequent successful copying;
it does not simulate RF interrupts or the ESP32 scheduler.

Archived BLE application size is 1,834,048 bytes, SHA256
`49846E4F056AAA228434096E4D92A5BC1CE6564046667FAB604E3802D2880745`.
No-BLE SHA256 is
`D5337E5BEDFE39387F697BE4C9B92E25815A2DE729BF8C712B70E61E5AE10138`.
Each image, exact ELF, bootloader and partition table was archived privately
before installation. No further USB connection or wired write was used.

Standard multipart upload of test19 from test18 completed in 19.667 seconds,
HTTP 200, complete 1,665-byte success HTML identifying 1,834,048 installed image
bytes. After the deferred restart, `/diag` independently identified test19/app0
and retained web_file OTA stage 5, accepted 1,834,048 bytes, error 0. Usable heap
was 24,512 bytes, largest block 23,540, minimum 5,572 and allocation failures 4
at 18 seconds. At approximately 228 seconds, after testing pages, usable heap
was 23,608, largest 22,516, minimum 3,264 and failures 16, compared with thousands
of failed Wi-Fi allocations in the preceding test18 observations. The remaining
failures were still 2,308-byte Wi-Fi allocations; they are not claimed solved.

All six HTML pages, CSS, both input/output row pairs, four BLE rows and BLE
candidates returned complete HTTP 200 responses in 0.057 to 0.225 seconds.
The information response confirmed MQTT connected, RF mode 3 and all intended
modules present. BLE advertisements increased with no dropped reports or Wi-Fi
disconnects. These are observations of startup and web use, not a successful
decoded RF-device test or long-term presence/stability proof.

A second wireless validation used URL OTA of the same archived test19 image.
The temporary LAN-only helper pinned response sockets to the LAN interface,
served 1,024-byte blocks with a 10-ms inter-block pause and one-second initial
body delay, and logged all 1,834,048 bytes sent. The subsequent `/diag` positively
identified test19/app1, URL OTA stage 5, exact expected/accepted byte counts,
error/detail 0 and an ordinary requested software reset. At 28 seconds it showed
24,704 usable bytes, largest 23,540, minimum 5,480, zero failed allocations and
BLE active. The helper was then stopped, with its terminal process confirmed
terminated. Two complete update paths now pass without USB; this does not
guarantee transfer through a faulty access point or during power loss.

Both private candidate USB ZIPs contain eleven files: four matching flash
images, portable esptool 4.11.0, standard/ROM launchers, the guided installer,
instructions, variant description and SHA256 manifest. The reproducible
packager checks input image formats/version, refuses existing outputs and
verifies every archived file and ZIP CRC. The actual installer's pre-serial
integrity block passed for both intact packages and rejected a corrupted image
and duplicate checksum in isolated fixtures. This validates packaging and
preflight only: the new full packages have not been flashed physically. No
GitHub publication was made; multi-day stability and remaining feature-level
validation are still required before calling the release final.

Before the next installation, test19 was still online at approximately eighteen
minutes, with 24,152 usable bytes, minimum 2,944, 411 failed Wi-Fi allocations,
1,189 BLE advertisements and zero Wi-Fi disconnects. This supports reduced
memory pressure, not a claim that packet allocation failures are eliminated.

Candidate test20 addresses a separate confirmed source defect in the pinned
Arduino-ESP32 2.0.7 WebServer: binary `endBuf` had no NUL terminator but was
searched with `strstr`, allowing an out-of-bounds read on a mismatch. The same
code allocated a variable-length stack array from an unchecked boundary length.
The fix compares exactly the received bytes, validates the 1..70-byte MIME
boundary length and uses a fixed 70-byte buffer. References:
[pinned upstream parser](https://github.com/espressif/arduino-esp32/blob/2.0.7/libraries/WebServer/src/Parsing.cpp),
[RFC 2046 section 5.1.1](https://www.rfc-editor.org/rfc/rfc2046.html#section-5.1.1).
This defect is not claimed to explain all past disconnections or the already
resolved two-buffer OTA stall.

The PRE-build hook generates a checked, isolated `OMGWebServer` from the pinned
SDK and includes the shared tested boundary helper. It does not patch the
installed SDK. Unknown parser versions, changed generated files or unrecognized
output folders cause failure before overwrite. Four generation tests passed;
the binary matcher test also passed with its non-terminated candidate directly
before an inaccessible Windows memory page. All three builds passed in
123.710/102.615/101.093 seconds (base/BLE/no-BLE). Logs prove the custom presets
compiled and linked OMGWebServer, while the base compiled the original WebServer.

Test20 BLE image is 1,834,272 bytes, SHA256
`F760413EDA022A9A00757ED362DA1A8D4922004C5E9752DA2D25FEA25B8DBD6C`.
No-BLE image is 1,689,952 bytes, SHA256
`13D989A524491F684A1F84B72C6C61F513A3D669714859F86125E816FEB95933`.
Images and exact ELF/bootloader/partition artifacts were archived privately.
Standard multipart OTA from test19 completed in 19.304 seconds, HTTP 200 with
complete success HTML. `/diag` identified test20/app0, stage 5, 1,834,272 accepted
bytes and error 0. No USB was used.

The newly installed bounded parser was then exercised directly: an invalid
71-byte boundary was rejected by closing the connection before starting an
upload. A subsequent diagnostic showed uninterrupted uptime, unchanged app0 and
unchanged last OTA record. A valid maximum-length 70-byte boundary then uploaded
the exact archived test20 application in 17.781 seconds, HTTP 200 and complete
success HTML. The subsequent `/diag` identified test20/app1, retained stage 5,
1,834,272 accepted bytes and error 0. At approximately 114 seconds it showed
23,880 usable bytes, minimum 4,476, one failed allocation, BLE active and zero
Wi-Fi disconnects. This validates a complete transfer through the patched
parser, not merely a successful installation of a firmware containing it.

A separate host test executed the actual BLE detection, timeout and JSON
publication functions with clock/mutex/queue mocks. Immediate ON, timeout renewal,
weak-signal rejection, JSON-null unavailable RSSI, pending OFF retry, startup
retained-state grace and millisecond rollover passed. No HA entities/settings
were changed and no GPIO output was actuated. Real beacon arrival/departure,
multi-day stability and residual memory pressure remain separate live gates.

Candidate test21 adds admission control to the RF decoder's approximately
9.7-KB signal copies. Before attempting a copy, it checks contiguous usable
internal/default RAM and leaves the configured MQTT queue network reserve
(`QUEUE_MIN_FREE_HEAP`, otherwise 12,000 bytes). Insufficient memory releases
the completed signal slot, increments the existing ignored-signal counter and
emits a throttled diagnostic; it never copies through a NULL pointer. Decoder
input sizes and supported RF protocols are unchanged. This is a heap snapshot,
not an atomic reservation against allocations by other tasks, and is not a
claim that all Wi-Fi allocation failures or historical hangs are solved.

Six dependency-patch tests passed, including exact migration from the previous
generated patch and refusal of unknown source/helper edits before any write.
The test executing the actual patched RF copy branch passed allocator failure,
headroom and fragmentation refusal, slot release, warning rollover and later
successful copying. Fourteen native C++ regression programs and the actual BLE
presence-function test also passed. All three firmware builds succeeded
(base 97.564 s, BLE 89.295 s, no-BLE 88.423 s).

Privately archived test21 BLE image is 1,834,368 bytes, SHA256
`E63DC7C4F834ACA4E6A0686C4E766F6A79470B317A68F34C43F710CCA90AEADA`.
No-BLE image is 1,690,064 bytes, SHA256
`D0F89AE2486A5D4EBC456E34A3B1C9FC004BC75D3FF1E83450CE8A0C584A45EE`.
The exact ELF, bootloader and partition table were preserved for both images
before installation. Immediately before this update, test20 remained online
at approximately 38 minutes: 24,348 usable bytes, minimum 2,096, 416 failed
allocations, 2,763 BLE advertisements, five dropped reports and zero Wi-Fi
disconnects. These short-run observations do not replace multi-day validation.

Test21 was installed wirelessly in 18.763 seconds (HTTP 200, complete success
HTML). After restart `/diag` positively identified test21/app0, web_file stage
5, 1,834,368 accepted bytes and error 0. All sixteen WebUI page/style/channel
responses were complete HTTP 200 responses in 0.056..0.210 seconds. At about
14.5 minutes it remained online with 24,248 usable bytes, minimum 2,916, 68
failed Wi-Fi allocations, 1,220 BLE advertisements, no dropped reports and zero
Wi-Fi disconnects. The allocation counter stopped increasing between the
9-minute and 14.5-minute observations, but it was not zero. Selected-beacon
arrival/departure and multi-day behavior are not established by this run.

Candidate test22 fixes two independently reproduced WOL edge cases. An empty
or odd-length MAC string could be read past its terminating NUL because both
hex digits were read before validating the first. The parser now rejects an
invalid first digit before reading the next byte. Separately, a send attempt
at `millis()==0` after rollover was mistaken for "never attempted", allowing
an extra packet in one-attempt mode. A separate attempt flag is now set before
sending and cleared on reconnection/configuration reset, regardless of success.
Neither defect is claimed to explain the historical Wi-Fi hangs.

The actual WOL-function regression failed before these fixes: first on the
extra send after clock rollover, then on a read beyond an empty string placed
at the last readable byte. It passes after both fixes, including every MAC
prefix, exact packet bytes, delay/failure thresholds, trigger categories,
repeat behavior and reconnect reset. No real UDP packet was sent by the test.
The actual GPIO/discovery-cleanup host test passed pins/modes, debounce,
inversion, retry/rollover, conflicts, commands, startup/restore and cleanup of
disabled/legacy GPIO and BLE entities. Tests mock pins and broker delivery;
they do not actuate the device, change HA or prove electrical behavior.

All three test22 builds succeeded (base 101.797 s, BLE 102.103 s, no-BLE
100.190 s). Archived BLE image: 1,834,400 bytes, SHA256
`C0E90A8D3AC6DF3C1CEA6206AA6D20158FFC0B29C0C059BBBE2958588332BC8A`.
No-BLE image: 1,690,080 bytes, SHA256
`ADB1D0736ABD617FADE2D5FF0BE12CE1E3A0BA9FDF0A5C984AC6EF9EA31996D1`.
Exact ELF, bootloader and partition tables were preserved before deployment.

Standard multipart test22 OTA completed in 18.231 seconds, HTTP 200 and complete
1,665-byte success HTML. Post-restart diagnostics positively identified
test22/app1, web_file stage 5, accepted 1,834,400 bytes, error 0. All sixteen
page/style/channel responses were complete HTTP 200 responses in 0.059..0.218
seconds. Form fields for both inputs, both outputs and all four BLE slots were
compared with test21 and remained unchanged. The information snapshot confirmed
MQTT connected and RTL_433 active. Both eleven-file USB packages passed the
actual installer's pre-serial integrity tests, including corruption/duplicate
manifest rejection. The shared SDK/dependency generation tests and fourteen
native reliability programs passed again.

At approximately 7.4 minutes test22 had 23,240 usable bytes, minimum 3,800,
296 failed Wi-Fi allocations, 718 BLE advertisements, no dropped reports and
zero Wi-Fi disconnects. The remaining allocation failures are explicitly not
claimed eliminated. A short successful run is not evidence of multi-day
stability, live selected-beacon arrival/departure or ISR concurrency safety;
no final-release publication has been made on that basis.

Candidate test23 removes an unused startup RF template allocation after the
pinned library has registered independent decoder copies. The target compiler
confirms `sizeof(r_device)==112`: the full 157-entry OOK table occupies 17,584
bytes (80-entry FSK table: 8,960 bytes). Both protocol inventories and actual
decoder input arrays remain intact. The release helper refuses any decoder
record or nested data pointer that aliases this table, missing registered
entries and address-range overflow; it never frees registered decoders or their
contexts. An allocation-failed factory also stops before a NULL dereference.
The opt-in patch affects only the two derivative presets, not the base library.

Eight patch-generation tests passed, including unchanged full protocol
inventories and migration from the previous exact generated version. A host
test executes the pinned registration, creation and Fine Offset factory
functions, verifies independent lifetime after table release, and injects
factory allocation failure and alias cases. This is an ownership test, not
live RF decoding or an ISR concurrency test. All three builds passed (base
101.188 s, BLE 107.321 s, no-BLE 100.112 s).

Archived test23 BLE image: 1,834,832 bytes, SHA256
`FF506976FAC00136471BB8A25A4813937F7A244D2055B529D0510FD79815432A`.
No-BLE image: 1,690,560 bytes, SHA256
`4A31B51079F105BD47093AA5B1C6FBAFFDB86BD0DD96DC873BBCCF897EEFED56`.
Exact ELF, bootloader and partition artifacts were archived before deployment.
The standard multipart OTA from test22 completed in 18.492 seconds, HTTP 200
with complete success HTML. Subsequent diagnostics positively identified
test23/app0, web_file stage 5, accepted 1,834,832 bytes and error 0. No USB was
used. All sixteen WebUI responses were complete HTTP 200 responses in
0.047..0.224 seconds. Both input/output and all four BLE slot form fields were
unchanged from test22; MQTT was connected and RTL_433 remained active.

At approximately seven minutes the device had 41,840 usable bytes, minimum
13,916, largest block 40,948, zero failed allocations, 387 BLE advertisements,
no dropped reports and zero Wi-Fi disconnects. These initial observations
show increased memory headroom, not proof of multi-day reliability. Both
eleven-file USB packages passed the actual installer's pre-serial integrity
checks, including corrupted-image and duplicate-manifest rejection. The
existing hourly read-only stability follow-up now targets test23 for a
72-hour observation window, with private evidence and no automatic actuation,
configuration changes or firmware updates.

Both test23 variants were subsequently exercised on the same board using only
the existing local-file WebUI endpoint. BLE to no-BLE completed in 17.065
seconds, HTTP 200 with complete success HTML. Diagnostics identified
no-BLE/app1, stage 5, accepted 1,690,560 bytes and error 0. At 25 seconds it had
86,552 usable bytes, minimum 66,448 and zero allocation failures or Wi-Fi
disconnects. All ten applicable page/style/GPIO responses were complete HTTP
200 responses in 0.075..0.219 seconds; MQTT and RTL_433 were active and the BLE
module was absent. Both input and output settings matched the BLE variant.

The subsequent no-BLE to BLE transfer completed in 18.743 seconds, HTTP 200
with complete success HTML. Diagnostics identified BLE/app0, stage 5, accepted
1,834,832 bytes and error 0. All eight GPIO/BLE configuration forms matched the
pre-round-trip values. At approximately 115 seconds usable memory was 41,992,
minimum 21,064, allocations failed zero, BLE advertisements 151, dropped zero
and Wi-Fi disconnects zero. The fourteen native reliability programs and all
dependency, WOL, GPIO and BLE-function regressions passed again. The round-trip
tests establish wireless installation and settings preservation for both
variants, not multi-day operation of either variant. The BLE firmware was left
installed for observation; no physical output commands were sent.

## Public R6 release (4 October 2026)

The owner explicitly requested publication without waiting for scheduled
multi-day observation. The hourly follow-up was deleted, not merely paused;
long-term behavior will be reported during normal use. No claim of zero bugs
or impossible hangs is made. Further improvements are separate from this
published reference.

Release source changes from test23 affect the two version labels only; the
packager labels public builds correctly and README/release notes describe
features, USB-first installation and evidence limitations. Final BLE and
no-BLE builds passed in 98.388 and 100.330 seconds. The exact ELF and all
bootloader/partition/application artifacts are archived privately. Public
USB ZIPs contain all eleven required files, including four images, portable
flashing utility and both launchers. Both passed the actual installer's
pre-serial intact/corrupt/duplicate-manifest tests.

BLE application: 1,834,816 bytes, SHA256
`3E9A804248E2DBF077B181AD21B9CA89DF5AB30B11FA9D91F0E98F23E083C780`.
NO BLE application: 1,690,544 bytes, SHA256
`BB9B49F034275810C245ACDC7B465417E1B9F0391395A700709385834475D5CF`.
Per-variant public manifests cover the WebUI application and complete USB ZIP.
