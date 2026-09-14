#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

template <typename Emit>
inline void consumeLogBytes(char* line, size_t capacity, size_t& index,
                            const uint8_t* data, size_t size, Emit emit) {
  if (!line || capacity < 2 || !data) return;
  if (index >= capacity - 1) index = 0;
  for (size_t i = 0; i < size; ++i) {
    if (data[i] != '\n') line[index++] = static_cast<char>(data[i]);
    if (data[i] == '\n' || index == capacity - 1) {
      line[index] = '\0';
      emit(line);
      index = 0;
    }
  }
}

// Caller holds the console mutex. Never use the destination as a printf source.
inline void appendBoundedLog(char* buffer, size_t capacity, uint32_t& nextIndex,
                             unsigned level, const char* a, const char* b = "", const char* c = "") {
  if (!buffer || capacity < 5) return;
  const char* parts[] = {a ? a : "", b ? b : "", c ? c : ""};
  size_t sizes[3];
  size_t remaining = capacity - 4;
  size_t payload = 0;
  for (unsigned i = 0; i < 3; ++i) {
    sizes[i] = 0;
    while (sizes[i] < remaining && parts[i][sizes[i]]) ++sizes[i];
    remaining -= sizes[i];
    payload += sizes[i];
  }
  nextIndex &= 255;
  if (!nextIndex) nextIndex = 1;
  size_t used = 0;
  while (used < capacity && buffer[used]) ++used;
  if (used == capacity) used = 0; // Reject a damaged, unterminated buffer.
  while (used && (static_cast<uint8_t>(buffer[0]) == nextIndex || used + payload + 4 > capacity)) {
    const char* delimiter = static_cast<const char*>(memchr(buffer + 1, '\1', used - 1));
    if (!delimiter) { used = 0; break; }
    const size_t removed = delimiter - buffer + 1;
    memmove(buffer, buffer + removed, used - removed);
    used -= removed;
  }
  buffer[used++] = static_cast<char>(nextIndex);
  buffer[used++] = '0' + (level <= 9 ? level : 9);
  for (unsigned part = 0; part < 3; ++part) {
    for (size_t i = 0; i < sizes[part]; ++i) {
      const char value = parts[part][i];
      buffer[used++] = value == '\1' ? ' ' : value;
    }
  }
  buffer[used++] = '\1';
  buffer[used] = '\0';
  nextIndex = nextIndex == 255 ? 1 : nextIndex + 1;
}
