# Native reliability regression tests

Run from the repository root using a C++11 compiler (the firmware build itself
does not run these tests). PowerShell example after PlatformIO has downloaded
the BLE preset's dependencies:

```powershell
foreach ($test in 'checked_queue', 'bounded_log', 'runtime_heartbeat', 'queue_json', 'socket_write') {
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
- `runtime_heartbeat`: check timeout boundaries and millisecond rollover.
- `socket_write`: simulate partial writes, temporary pressure, fatal errors and
  bounded retries across the millisecond rollover.

These are deterministic host tests, not an ESP32 radio, FreeRTOS scheduler,
electrical-output or multi-day stability test. Keep crash dumps private and
archive the exact ELF for every deployed firmware revision.
