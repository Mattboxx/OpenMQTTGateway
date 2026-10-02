#pragma once
#include <stddef.h>
#include <stdint.h>

enum class FirmwareTransferResult { Complete, IdleTimeout, Deadline, Disconnected, InvalidRead, WriteFailed };

// Only accepted flash bytes renew the idle deadline. Network retries cannot
// keep a failed OTA alive indefinitely. Caller owns flash validation/abort.
template <typename Available, typename Connected, typename Read, typename Write,
          typename Clock, typename Yield>
FirmwareTransferResult transferFirmware(size_t total, size_t& written,
                                       Available available, Connected connected,
                                       Read read, Write write, Clock now, Yield yield,
                                       uint32_t idleMs = 15000, uint32_t totalMs = 900000) {
  uint8_t block[512];
  const uint32_t started = now();
  uint32_t lastProgress = started;
  written = 0;
  while (written < total) {
    const uint32_t current = now();
    if (current - started >= totalMs) return FirmwareTransferResult::Deadline;
    if (current - lastProgress >= idleMs) return FirmwareTransferResult::IdleTimeout;
    const int buffered = available();
    if (buffered > 0) {
      size_t count = total - written;
      if (count > sizeof(block)) count = sizeof(block);
      if (count > static_cast<size_t>(buffered)) count = static_cast<size_t>(buffered);
      const int received = read(block, count);
      if (received > static_cast<int>(count) || received < -1)
        return FirmwareTransferResult::InvalidRead;
      if (received > 0) {
        if (write(block, static_cast<size_t>(received)) != static_cast<size_t>(received))
          return FirmwareTransferResult::WriteFailed;
        written += static_cast<size_t>(received);
        lastProgress = now();
      }
    } else if (!connected()) {
      return FirmwareTransferResult::Disconnected;
    }
    yield(5);
  }
  return FirmwareTransferResult::Complete;
}

// Waiting for TCP data is not validation. In particular, peek()==-1 before the
// first packet arrives must not be reported as an invalid firmware header.
template <typename Available, typename Peek, typename Connected, typename Clock, typename Yield>
int waitFirmwareHeader(Available available, Peek peek, Connected connected,
                       Clock now, Yield yield, uint32_t timeoutMs = 8000) {
  const uint32_t started = now();
  while (now() - started < timeoutMs) {
    if (available() > 0) {
      const int value = peek();
      if (value >= 0) return value;
    } else if (!connected()) {
      return -1;
    }
    yield(5);
  }
  return -1;
}
