#include "../../main/StaticObjectPool.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

struct alignas(16) Timer { uint32_t time; void* next; };
int main() {
  StaticObjectPool<Timer, 3> pool;
  Timer* first = pool.allocate();
  Timer* second = pool.allocate();
  Timer* third = pool.allocate();
  assert(first && second && third && first != second && second != third);
  assert(reinterpret_cast<uintptr_t>(first) % alignof(Timer) == 0);
  assert(pool.used() == 3 && pool.peak() == 3 && !pool.allocate());
  Timer foreign = {};
  assert(!pool.release(&foreign) && !pool.release(nullptr));
  assert(!pool.release(reinterpret_cast<char*>(first) + 1));
  assert(pool.release(second) && pool.used() == 2);
  assert(pool.release(second) && pool.used() == 2);
  assert(pool.allocate() == second);
  assert(pool.release(first) && pool.release(second) && pool.release(third));
  for (unsigned i = 0; i < 100000; ++i) {
    Timer* value = pool.allocate();
    assert(value && pool.release(value));
  }
  assert(pool.used() == 0 && pool.peak() == 3);
  puts("PASS: static timer storage alignment, exhaustion, reuse and foreign pointers");
}
