#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../../main/BoundedFirmwareTransfer.h"

int main() {
  uint32_t clock = 0;
  const auto now = [&]() { return clock; };
  const auto yield = [&](unsigned ms) { clock += ms; };
  assert(waitFirmwareHeader([&]() { return clock >= 500 ? 1 : 0; },
                            []() { return 0xe9; }, []() { return true; }, now, yield) == 0xe9);
  assert(clock == 500); // Delayed first byte, not the old 100-ms rejection.
  clock = 0;
  assert(waitFirmwareHeader([]() { return 1; }, []() { return 0x00; },
                            []() { return true; }, now, yield) == 0);
  clock = 0;
  assert(waitFirmwareHeader([]() { return 0; }, []() { return -1; },
                            []() { return true; }, now, yield, 50) == -1);
  assert(clock == 50);
  clock = UINT32_MAX - 20;
  assert(waitFirmwareHeader([]() { return 0; }, []() { return -1; },
                            []() { return true; }, now, yield, 50) == -1);
  assert(clock == 29);
  clock = 0;
  assert(waitFirmwareHeader([]() { return 0; }, []() { return -1; },
                            []() { return false; }, now, yield) == -1);
  assert(clock == 0);

  std::vector<uint8_t> source(1700), output;
  for (size_t i = 0; i < source.size(); ++i) source[i] = static_cast<uint8_t>(i);
  size_t offset = 0, written = 123;
  int transientReads = 2;
  const auto read = [&](uint8_t* block, size_t count) -> int {
    assert(count <= 512);
    if (transientReads-- > 0) return -1;
    if (count > 97) count = 97; // Partial reads, including 0 and 255 payload bytes.
    memcpy(block, source.data() + offset, count);
    offset += count;
    return static_cast<int>(count);
  };
  const auto write = [&](uint8_t* block, size_t count) {
    output.insert(output.end(), block, block + count);
    return count;
  };
  clock = UINT32_MAX - 20;
  assert(transferFirmware(source.size(), written, []() { return 700; },
                          []() { return true; }, read, write, now, yield) == FirmwareTransferResult::Complete);
  assert(written == source.size() && output == source);

  const auto noRead = [](uint8_t*, size_t) { return -1; };
  const auto noWrite = [](uint8_t*, size_t count) { return count; };
  clock = 0;
  assert(transferFirmware(100, written, []() { return 1; }, []() { return true; },
                          noRead, noWrite, now, yield, 40) == FirmwareTransferResult::IdleTimeout);
  assert(clock == 40 && written == 0);
  clock = 0;
  assert(transferFirmware(100, written, []() { return 0; }, []() { return false; },
                          noRead, noWrite, now, yield) == FirmwareTransferResult::Disconnected);
  const auto partialRead = [](uint8_t* block, size_t) { block[0] = 0xe9; return 1; };
  clock = 0;
  assert(transferFirmware(100, written, []() { return 1; }, []() { return true; },
                          partialRead, noWrite, now, yield, 40, 20) == FirmwareTransferResult::Deadline);
  assert(clock == 20 && written == 4); // Real progress cannot bypass total bound.
  clock = 0;
  assert(transferFirmware(100, written, []() { return 1; }, []() { return true; },
                          partialRead, [](uint8_t*, size_t) { return size_t(0); },
                          now, yield) == FirmwareTransferResult::WriteFailed);
  assert(written == 0);
  clock = 0;
  assert(transferFirmware(100, written, []() { return 1; }, []() { return true; },
                          [](uint8_t*, size_t count) { return static_cast<int>(count + 1); },
                          noWrite, now, yield) == FirmwareTransferResult::InvalidRead);
  // Closed socket with buffered final bytes: drain before considering disconnect.
  clock = 0;
  assert(transferFirmware(1, written, []() { return 1; }, []() { return false; },
                          partialRead, noWrite, now, yield) == FirmwareTransferResult::Complete);
  puts("PASS: OTA header delay, partial/transient reads, byte order, flash failure, idle/total bounds, disconnect and rollover");
}
