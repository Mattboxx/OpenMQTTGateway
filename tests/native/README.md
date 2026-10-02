# Native reliability regression tests

Run from the repository root using a C++11 compiler (the firmware build itself
does not run these tests). PowerShell example after PlatformIO has downloaded
the BLE preset's dependencies:

```powershell
foreach ($test in 'checked_queue', 'bounded_log', 'fixed_discovery_cache', 'static_object_pool', 'wifi_buffer_budget', 'socket_memory_budget', 'runtime_heartbeat', 'queue_json', 'socket_write', 'firmware_transfer') {
  g++ -std=c++11 -Wall -Wextra -Werror -O2 `
    -I .pio/libdeps/esp32dev-multi_receiver-wol-gpio-ble/ArduinoJson/src `
    "tests/native/${test}_test.cpp" -o ".pio/${test}_test.exe"
  if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $test" }
  & ".pio/${test}_test.exe"
  if ($LASTEXITCODE -ne 0) { throw "Test failed: $test" }
}
```

- `checked_queue`: inject failed allocations and serialization, test capacity,
  FIFO ordering, wraparound and ownership cleanup.
- `queue_json`: use the firmware's ArduinoJson version to check exact sizing,
  escaping and copying deserialization before releasing the queued payload.
- `bounded_log`: stress long lines, buffer boundaries, malformed data,
  identifier rollover and guard canaries.
- `fixed_discovery_cache`: prove that radio-controlled discovery identifiers
  cannot grow heap, evict unpublished entries or break across millis rollover.
- `static_object_pool`: check timer storage alignment, exhaustion, reuse and
  rejection of foreign/interior pointers before linker integration on ESP32.
- `wifi_buffer_budget`: check RX/TX/DMA caps, disabled aggregation, compatible
  RX/Block Ack limits and preservation of static TX/security/SDK defaults.
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
