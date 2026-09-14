#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct MessageAllocator {
  static void* allocate(size_t bytes) { return malloc(bytes); }
  static void release(void* memory) { free(memory); }
};

// Caller serializes access. Fixed slots avoid std::deque/string's throwing
// allocations; payload allocation failure is an ordinary, testable result.
template <size_t Capacity, typename Allocator = MessageAllocator>
class CheckedMessageQueue {
  static_assert(Capacity > 0, "Queue needs at least one slot");
  struct Slot { char* data; size_t length; } slots[Capacity] = {};
  size_t head = 0, count = 0;
public:
  CheckedMessageQueue() = default;
  CheckedMessageQueue(const CheckedMessageQueue&) = delete;
  CheckedMessageQueue& operator=(const CheckedMessageQueue&) = delete;
  ~CheckedMessageQueue() { while (!empty()) pop(); }
  size_t size() const { return count; }
  bool empty() const { return count == 0; }
  const char* front() const { return empty() ? "" : slots[head].data; }
  size_t frontSize() const { return empty() ? 0 : slots[head].length; }
  template <typename Fill>
  bool tryPush(size_t length, Fill fill) {
    if (count == Capacity || length == SIZE_MAX) return false;
    char* data = static_cast<char*>(Allocator::allocate(length + 1));
    if (!data) return false;
    if (!fill(data, length + 1)) { Allocator::release(data); return false; }
    data[length] = '\0';
    const size_t tail = (head + count) % Capacity;
    slots[tail] = {data, length};
    ++count;
    return true;
  }
  void pop() {
    if (empty()) return;
    Allocator::release(slots[head].data);
    slots[head] = {nullptr, 0};
    head = (head + 1) % Capacity;
    --count;
  }
};
