#pragma once
#include <stddef.h>

// Sending must leave space for incoming Wi-Fi frames, including TCP ACKs.
// Otherwise an outbound response can consume the heap needed to drain itself.
// Reserve aggregate space for RX/ACK bookkeeping and a contiguous individual
// RX allocation. Frames are allocated separately, not as one two-frame block.
// The 12-KiB/two-frame contiguous experiment rejected even small live test17
// responses after restoring proper Wi-Fi packet-slot capacity. These limits
// are a bounded admission guard, not proof that every concurrent allocator fits.
constexpr size_t socketReceiveReserve = 8192;
constexpr size_t socketReceiveBlock = 2308 + 256;
inline bool socketWriteHasHeadroom(size_t freeBytes, size_t largestBlock,
                                   size_t nextChunk) {
  return freeBytes >= socketReceiveReserve && nextChunk <= freeBytes - socketReceiveReserve &&
         largestBlock >= socketReceiveBlock && nextChunk <= largestBlock - socketReceiveBlock;
}
