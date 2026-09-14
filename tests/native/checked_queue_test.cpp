#include "../../main/CheckedMessageQueue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct FaultAllocator {
  static bool fail;
  static size_t live;
  static void* allocate(size_t bytes) {
    if (fail) return nullptr;
    void* data = malloc(bytes);
    if (data) ++live;
    return data;
  }
  static void release(void* data) { if (data) --live; free(data); }
};
bool FaultAllocator::fail = false;
size_t FaultAllocator::live = 0;

int main() {
  {
    CheckedMessageQueue<3, FaultAllocator> queue;
    auto push = [&](const char* text) {
      return queue.tryPush(strlen(text), [&](char* target, size_t size) {
        memcpy(target, text, size); return true;
      });
    };
    assert(push("first"));
    FaultAllocator::fail = true;
    assert(!push("out of memory"));
    assert(queue.size() == 1 && strcmp(queue.front(), "first") == 0);
    FaultAllocator::fail = false;
    assert(!queue.tryPush(100, [](char*, size_t) { return false; }));
    assert(FaultAllocator::live == 1);
    assert(push("second"));
    assert(push("third"));
    assert(!push("full"));
    queue.pop();
    assert(push("fourth"));
    assert(strcmp(queue.front(), "second") == 0);
    queue.pop(); queue.pop();
    assert(strcmp(queue.front(), "fourth") == 0);
    queue.pop(); queue.pop();
    for (unsigned i = 0; i < 100000; ++i) {
      assert(push("wrap"));
      assert(queue.frontSize() == 4);
      queue.pop();
    }
    assert(push("released by destructor"));
  }
  assert(FaultAllocator::live == 0);
  puts("PASS: allocation failure, full queue, failed serialization, FIFO, wrap and ownership");
}
