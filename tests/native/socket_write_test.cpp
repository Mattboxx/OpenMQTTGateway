#include "../../main/BoundedSocketWrite.h"
#include <assert.h>
#include <stdio.h>
#include <string>

int main() {
  const std::string payload(5000, 'x');
  std::string output;
  uint32_t now = 0xfffffff0U;
  unsigned calls = 0;
  auto clock = [&]() { return now; };
  auto wait = [&](unsigned ms) { now += ms; };
  const size_t sent = writeBoundedResponse(payload.data(), payload.size(),
    [&](const char* data, size_t bytes) -> int {
      assert(bytes <= 512);
      if (++calls % 3 == 0) return 0;
      const size_t accepted = bytes > 73 ? 73 : bytes;
      output.append(data, accepted);
      return static_cast<int>(accepted);
    }, clock, wait);
  assert(sent == payload.size() && output == payload);
  now = 0;
  assert(writeBoundedResponse(payload.data(), payload.size(),
    [](const char*, size_t) { return 0; }, clock, wait, 120) == 0);
  assert(now == 120);
  assert(writeBoundedResponse(payload.data(), payload.size(),
    [](const char*, size_t) { return -1; }, clock, wait) == 0);
  assert(now == 120);
  puts("PASS: partial writes, transient pressure, byte order, timeout, fatal error and clock rollover");
}
