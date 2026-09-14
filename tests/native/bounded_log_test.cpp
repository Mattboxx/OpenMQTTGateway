#include "../../main/BoundedLogBuffer.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <random>

static void validate(const char* data, size_t capacity) {
  const char* end = static_cast<const char*>(memchr(data, 0, capacity));
  assert(end);
  const char* position = data;
  while (position < end) {
    assert(static_cast<uint8_t>(position[0]) != 0);
    assert(position + 2 < end);
    assert(position[1] >= '0' && position[1] <= '9');
    const char* delimiter = static_cast<const char*>(memchr(position + 2, '\1', end - position - 2));
    assert(delimiter);
    position = delimiter + 1;
  }
  assert(position == end);
}

int main() {
  struct { char line[1024]; uint64_t after; } assembly = {};
  assembly.after = 0x123456789abcdef0ULL;
  size_t index = 0, total = 0;
  const std::string longLine(20000, 'x');
  for (unsigned i = 0; i < longLine.size(); ++i) {
    consumeLogBytes(assembly.line, sizeof(assembly.line), index,
                    reinterpret_cast<const uint8_t*>(longLine.data() + i), 1,
                    [&](const char* chunk) { assert(strlen(chunk) <= 1023); total += strlen(chunk); });
    assert(assembly.after == 0x123456789abcdef0ULL);
  }
  consumeLogBytes(assembly.line, sizeof(assembly.line), index,
                  reinterpret_cast<const uint8_t*>("\n"), 1,
                  [&](const char* chunk) { total += strlen(chunk); });
  assert(total == longLine.size());
  assert(index == 0);
  struct { uint64_t before; char data[128]; uint64_t after; } area = {};
  area.before = area.after = 0x123456789abcdef0ULL;
  uint32_t cursor = 1;
  std::mt19937 random(7);
  for (unsigned n = 0; n < 100000; ++n) {
    const size_t length = random() % 1000;
    std::string text(length, 'a');
    for (char& value : text) if (random() % 13 == 0) value = '\1';
    appendBoundedLog(area.data, sizeof(area.data), cursor, random() % 16,
                     text.c_str(), "second", "third");
    validate(area.data, sizeof(area.data));
    assert(area.before == 0x123456789abcdef0ULL);
    assert(area.after == 0x123456789abcdef0ULL);
    assert(cursor >= 1 && cursor <= 255);
  }
  // Damaged input must recover without unbounded reads or integer underflow.
  memset(area.data, 'x', sizeof(area.data));
  appendBoundedLog(area.data, sizeof(area.data), cursor, 1, "recovered");
  validate(area.data, sizeof(area.data));
  char tiny[5] = {};
  cursor = 255;
  appendBoundedLog(tiny, sizeof(tiny), cursor, 1, "long text", nullptr, nullptr);
  validate(tiny, sizeof(tiny));
  assert(cursor == 1);
  // More than 255 entries with room left: identifiers must not collide.
  char large[6096] = {};
  for (unsigned n = 0; n < 1000; ++n)
    appendBoundedLog(large, sizeof(large), cursor, 1, "x");
  validate(large, sizeof(large));
  bool seen[256] = {};
  for (const char* p = large; *p; p = strchr(p + 2, '\1') + 1) {
    const uint8_t id = static_cast<uint8_t>(*p);
    assert(!seen[id]);
    seen[id] = true;
  }
  puts("PASS: bounded log stress, truncation, malformed buffer, index rollover, canaries");
}
