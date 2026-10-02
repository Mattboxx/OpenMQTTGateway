#include "../../main/SocketMemoryBudget.h"
#include "../../main/BoundedSocketWrite.h"
#include <assert.h>
#include <stdio.h>
#include <string>

int main() {
  assert(socketWriteHasHeadroom(6656, 3076, 512));
  assert(!socketWriteHasHeadroom(6655, 3076, 512));
  assert(!socketWriteHasHeadroom(6656, 3075, 512));
  assert(!socketWriteHasHeadroom(0, 0, 512));
  assert(!socketWriteHasHeadroom(5000, 20000, 1));
  assert(!socketWriteHasHeadroom(20000, 2308, 1));
  assert(!socketWriteHasHeadroom(6144, 2564, size_t(-1)));

  const std::string payload(5000, 'x');
  std::string output;
  size_t freeBytes = 8000, inFlight = 0;
  uint32_t now = 0xfffffff0U;
  unsigned waits = 0;
  const size_t sent = writeBoundedResponse(payload.data(), payload.size(),
    [&](const char* bytes, size_t count) -> int {
      if (!socketWriteHasHeadroom(freeBytes, freeBytes, count)) {
        ++waits;
        return 0;
      }
      freeBytes -= count;
      inFlight += count;
      assert(freeBytes >= 6144);
      output.append(bytes, count);
      return static_cast<int>(count);
    }, [&]() { return now; }, [&](unsigned ms) {
      now += ms;
      // Delayed ACKs free the transmit buffers only after temporary pressure.
      if (ms >= 10) { freeBytes += inFlight; inFlight = 0; }
    });
  assert(sent == payload.size() && output == payload && waits > 0);
  now = 0;
  assert(writeBoundedResponse(payload.data(), payload.size(),
    [](const char*, size_t) { return 0; }, [&]() { return now; },
    [&](unsigned ms) { now += ms; }, 120) == 0);
  assert(now == 120);
  puts("PASS: RX headroom, fragmentation, size overflow, delayed ACKs and bounded memory backpressure");
}
