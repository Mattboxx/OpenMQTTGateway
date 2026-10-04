# Native reliability regression tests

Run from the repository root using a C++11 compiler (the firmware build itself
does not run these tests). PowerShell example after PlatformIO has downloaded
the BLE preset's dependencies:

```powershell
foreach ($test in 'rf_signal_memory_budget', 'multipart_boundary', 'controller_shutdown', 'bounded_json_array', 'checked_queue', 'bounded_log', 'fixed_discovery_cache', 'static_object_pool', 'wifi_buffer_budget', 'socket_memory_budget', 'runtime_heartbeat', 'queue_json', 'socket_write', 'firmware_transfer') {
  g++ -std=c++11 -Wall -Wextra -Werror -O2 `
    -I .pio/libdeps/esp32dev-multi_receiver-wol-gpio-ble/ArduinoJson/src `
    "tests/native/${test}_test.cpp" -o ".pio/${test}_test.exe"
  if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $test" }
  & ".pio/${test}_test.exe"
  if ($LASTEXITCODE -ne 0) { throw "Test failed: $test" }
}
```

- `rf_signal_memory_budget`: check contiguous signal-copy space, remaining
  network reserve, exact admission boundaries, concurrent-copy pressure and
  size overflow without allocating any signal buffer.
- `multipart_boundary`: bounded binary matching without a NUL terminator,
  maximum/invalid lengths, null pointers, embedded NUL mismatch and (Windows)
  a candidate immediately before an inaccessible memory page.
- `controller_shutdown`: verify scan-only OTA pause without controller teardown,
  acknowledged scan stop, SDK operation ordering,
  busy initialization and immediate exit on each disable/deinit/release failure.
- `bounded_json_array`: check module-list capacity, exact termination, valid
  empty-array fallback, escaping and surrounding memory guards.
- `checked_queue`: inject failed allocations and serialization, test capacity,
  FIFO ordering, wraparound, clear/reuse and ownership cleanup.
- `queue_json`: use the firmware's ArduinoJson version to check exact sizing,
  escaping and copying deserialization before releasing the queued payload.
- `bounded_log`: stress long lines, buffer boundaries, malformed data,
  identifier rollover and guard canaries.
- `fixed_discovery_cache`: prove that radio-controlled discovery identifiers
  cannot grow heap, evict unpublished entries or break across millis rollover.
- `static_object_pool`: check timer storage alignment, exhaustion, reuse and
  rejection of foreign/interior pointers before linker integration on ESP32.
- `wifi_buffer_budget`: check the minimum non-PSRAM RX/TX/DMA packet slots,
  disabled aggregation, compatible RX/Block Ack limits and preservation of
  static TX/security/SDK defaults.
- `runtime_heartbeat`: check timeout boundaries and millisecond rollover.
- `socket_write`: simulate partial writes, temporary pressure, fatal errors and
  bounded retries across the millisecond rollover.
- `socket_memory_budget`: leave ACK/RX headroom, reject fragmented/insufficient
  memory, avoid size overflow and resume after delayed ACK buffer reclamation.
- `firmware_transfer`: wait for delayed headers; bound idle/total OTA duration,
  retry transient/partial reads, preserve byte order, reject failed flash writes
  and drain buffered data after disconnect, including clock rollover.

These are deterministic host tests, not an ESP32 radio, FreeRTOS scheduler,
electrical-output or multi-day stability test. Keep crash dumps private and
archive the exact ELF for every deployed firmware revision.

The opt-in RF memory patch also has dependency-level checks:

```powershell
python tests/native/rtl433_patch_test.py
python tests/native/rtl433_copy_failure_test.py
g++ -std=c++11 -Wall -Wextra -Werror -O2 -DOMG_RTL433_COMPACT_METADATA `
  -I .pio/libdeps/esp32dev-multi_receiver-wol-gpio-ble/rtl_433_ESP/include `
  tests/native/rtl433_metadata_test.cpp -o .pio/rtl433_metadata_test.exe
& .pio/rtl433_metadata_test.exe
# Repeat without -DOMG_RTL433_COMPACT_METADATA to verify the default layout.
```

Run after building the custom BLE preset so its dependency has been patched.
`rtl433_patch_test.py` verifies exact pinned source hashes, repeatability,
reversibility, migration from the previous exact generated patch, refusing
modified source/helper files before any write, and placement of the
allocation-failure guards before dereferencing the decoder copy. The C++ test
uses the actual dependency header and metadata helper, checks RSSI/duration
boundaries and preserves the full decoder input arrays. These checks do not
simulate RF interrupts, validate live RF decoding or inject hardware heap
failures. The patch test's guard checks are structural. Separately,
`rtl433_copy_failure_test.py` compiles and executes the actual patched copy
branch with mocked allocation failure and heap pressure, checking admission
before allocator invocation, fragmentation rejection, no NULL write, slot release,
byte-addressable caps, throttled warnings across clock rollover and subsequent
successful copying. Headroom is a snapshot, not a reservation against other
tasks. FreeRTOS scheduling and ISR concurrency are not simulated.

USB packages built by `scripts/package-usb.py` include all four flash images,
the portable Windows tool, both launchers, installer, instructions, variant
label and a SHA256 manifest. The packager verifies the ZIP against those hashes
and refuses existing output paths. To check an extracted package without any
serial access, run `tests/native/usb_package_integrity_test.ps1 -Package PATH`.
It executes the actual installer's integrity block and checks rejection of a
corrupt application and duplicate checksum using a private temporary fixture.
This is not a physical USB flash test or proof of download-source authenticity.

`python tests/native/webserver_patch_test.py --framework PATH_TO_PINNED_ARDUINO_SDK`
checks isolated WebServer generation, repeatability, rejection of changed source
or local edits and preservation of the shared SDK. Custom presets select the
generated `OMGWebServer` before built-in SDK libraries; base presets do not run
the hook. Build logs must show this library compiled, not just a generated copy.

`python tests/native/ble_presence_test.py` compiles and executes the actual BLE
advertisement, timeout and JSON publication functions against the actual config
header and pinned ArduinoJson. Mocks supply the clock, mutex, queue and logging;
the one Arduino `String` origin expression is adapted to `std::string`, without
changing state decisions. It checks immediate arrival, renewed five-minute
absence windows, RSSI filtering, unknown RSSI as JSON null, rejected OFF-state
publication retry, initial retained-state grace, disabled/nonmatching slots and
clock rollover. It does not prove real HCI reception or Home Assistant behavior.

`python tests/native/wol_behavior_test.py` executes the actual MAC parser,
packet construction and outage/timer functions, using the pinned PicoMQTT
return-code enum and mocked clock/MQTT/Wi-Fi/UDP. It checks all 102 packet bytes,
trigger categories, initial-delay/failure thresholds, repeat intervals,
one-attempt mode, retry reset after reconnect, failed sends and clock rollover.
On Windows, every prefix of a valid MAC ends at the last readable byte before
an inaccessible page. No broadcast packet is sent. Use a compiler with a
32-bit `unsigned long` ABI, as on the Windows host and target ESP32.

`python tests/native/gpio_behavior_test.py` executes the actual GPIO module and
discovery-cleanup function against the pinned ArduinoJson/config headers.
Mocks replace pins, Arduino `String`, preferences, time, logging and MQTT;
there is no physical actuation or HA mutation. It checks reserved/input-only
pins, pulls, active-level inversion, debounce/retry across rollover, duplicate
and input/output conflicts, both output modes, commands, startup/restore and
retained/discovery cleanup. Both entities of a disabled BLE tracker and old
discovery switches/input slots are removed; enabled slots are preserved.
This verifies cleanup intent, not live broker delivery or actual HA rendering.

`python tests/native/rf_template_lifetime_test.py` executes the pinned RF
registration, `create_device` and Fine Offset factory functions with mocked
allocation/list/output handlers and actual device/list headers. Only C-style
malloc assignments acquire casts for C++ compilation. It registers 157 mock
templates, frees the startup-only table, and checks the independent decoder
records, names, fields and factory context afterward. Record/nested-pointer
aliases, missing entries and address overflow refuse release; factory NULL
allocation stops before a dereference. This verifies lifetime/ownership, not
live decoding of all 157 protocols or FreeRTOS/ISR concurrency.
