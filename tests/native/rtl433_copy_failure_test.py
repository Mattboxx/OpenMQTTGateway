"""Compile/execute the actual patched RF copy branch with a failing allocator.

Radio/FreeRTOS operations are mocked; this checks NULL handling, ownership,
byte-addressable caps, warning throttling/rollover, and subsequent recovery.
"""

import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIBRARY = ROOT / ".pio/libdeps/esp32dev-multi_receiver-wol-gpio-ble/rtl_433_ESP"
source = (LIBRARY / "src/rtl_433_ESP.cpp").read_text(encoding="utf-8")
begin = source.index("      const uint32_t pulseMemoryCaps =")
end = source.index("\n", source.index("      memcpy(rtl_pulses", begin))
branch = source[begin:end]
assert "if (!rtl_pulses)" in branch, "Build the patched preset first"

harness = r'''
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <cstdint>
#include "pulse_data.h"
#include "RFSignalMemoryBudget.h"
static const int MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_DEFAULT = 4, LOG_ERR = 3;
static pulse_data_t trains[2] = {};
static pulse_data_t *_pulseTrains = trains, *captured = nullptr;
static bool failAllocation = true;
static unsigned long fakeNow = 100;
static int ignoredSignals = 0, warnings = 0, delays = 0;
static size_t fakeUsable = 40000, fakeLargest = 40000;
static unsigned allocations = 0;
size_t heap_caps_get_free_size(uint32_t caps) {
  assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT)); return fakeUsable;
}
size_t heap_caps_get_largest_free_block(uint32_t caps) {
  assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT)); return fakeLargest;
}
unsigned long millis() { return fakeNow; }
void vTaskDelay(int ticks) { assert(ticks == 1); ++delays; }
void logprintfLn(int level, const char *, ...) { assert(level == LOG_ERR); ++warnings; }
void *heap_caps_calloc(size_t count, size_t size, int caps) {
  ++allocations;
  assert(count == 1 && size == sizeof(pulse_data_t));
  assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  return failAllocation ? nullptr : std::calloc(count, size);
}
void actualCopyBranch() {
  int _receiveTrain = 0;
__BRANCH__
  captured = rtl_pulses;
}
int main() {
  trains[0].num_pulses = 10;
  actualCopyBranch();
  assert(captured == nullptr && trains[0].num_pulses == 0);
  assert(ignoredSignals == 1 && delays == 1 && warnings == 1);
  fakeNow = 200;
  actualCopyBranch();
  assert(ignoredSignals == 2 && warnings == 1);
  fakeNow = 5100;
  actualCopyBranch();
  assert(warnings == 2);
  fakeNow = std::numeric_limits<unsigned long>::max() - 10;
  actualCopyBranch();
  assert(warnings == 3);
  fakeNow = 20;
  actualCopyBranch();
  assert(warnings == 3);  // Millisecond wrap does not flood warnings.
  failAllocation = false;
  trains[0].num_pulses = PD_MAX_PULSES;
  trains[0].pulse[PD_MAX_PULSES - 1] = 42;
  trains[0].gap[PD_MAX_PULSES - 1] = 84;
  actualCopyBranch();
  assert(captured && std::memcmp(captured, &trains[0], sizeof(trains[0])) == 0);
  assert(ignoredSignals == 5 && delays == 5);
  std::free(captured);
  captured = nullptr;
  const unsigned beforeAdmission = allocations;
  fakeUsable = sizeof(pulse_data_t) + 11999;
  trains[0].num_pulses = 10;
  actualCopyBranch();
  assert(allocations == beforeAdmission && captured == nullptr && trains[0].num_pulses == 0);
  fakeUsable = 40000; fakeLargest = sizeof(pulse_data_t) - 1;
  actualCopyBranch();
  assert(allocations == beforeAdmission && captured == nullptr);
  fakeLargest = 40000;
  trains[0].num_pulses = 10;
  actualCopyBranch();
  assert(allocations == beforeAdmission + 1 && captured && captured->num_pulses == 10);
  std::free(captured);
}
'''.replace("__BRANCH__", branch)

with tempfile.TemporaryDirectory(prefix="omg-rtl433-fault-") as directory:
    fixture = Path(directory) / "copy_test.cpp"
    binary = Path(directory) / ("copy_test.exe" if os.name == "nt" else "copy_test")
    fixture.write_text(harness, encoding="utf-8")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra",
                    "-Werror", "-O2", "-I", str(LIBRARY / "include"),
                    str(fixture), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: actual RF copy branch, admission/fragmentation, NULL allocation, slot release, warning rollover and recovery")
