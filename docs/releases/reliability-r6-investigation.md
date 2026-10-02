# Reliability investigation — r6 candidates

This is an investigation record, not a stable-release announcement.

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
