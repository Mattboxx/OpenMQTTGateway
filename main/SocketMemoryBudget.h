#pragma once
#include <stddef.h>

// Sending must leave space for incoming Wi-Fi frames, including TCP ACKs.
// Otherwise an outbound response can consume the heap needed to drain itself.
// Check both total byte-addressable memory and a contiguous RX-sized block.
inline bool socketWriteHasHeadroom(size_t freeBytes, size_t largestBlock,
                                   size_t nextChunk) {
  constexpr size_t receiveReserve = 6144;
  constexpr size_t receiveBlock = 2308 + 256;
  return freeBytes >= receiveReserve && nextChunk <= freeBytes - receiveReserve &&
         largestBlock >= receiveBlock && nextChunk <= largestBlock - receiveBlock;
}
