#pragma once
#include <stddef.h>
#include <stdint.h>

// Writer returns bytes accepted, 0 for temporary backpressure, -1 for a fatal
// socket error. It must be non-blocking. Yield lets the network task drain.
template <typename Writer, typename Clock, typename Yield>
size_t writeBoundedResponse(const char* data, size_t length, Writer write,
                            Clock clock, Yield yield, uint32_t timeoutMs = 8000) {
  const uint32_t started = clock();
  size_t offset = 0;
  while (offset < length && uint32_t(clock() - started) < timeoutMs) {
    const size_t chunk = length - offset > 512 ? 512 : length - offset;
    const int accepted = write(data + offset, chunk);
    if (accepted < 0 || static_cast<size_t>(accepted) > chunk) break;
    offset += static_cast<size_t>(accepted);
    yield(accepted ? 1 : 10);
  }
  return offset;
}
