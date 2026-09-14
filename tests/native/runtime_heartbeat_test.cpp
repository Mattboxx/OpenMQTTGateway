#include "../../main/RuntimeHeartbeat.h"
#include <assert.h>
#include <stdio.h>

int main() {
  RuntimeHeartbeat heartbeat;
  const uint32_t timeout = 120000;
  heartbeat.progress(1000);
  assert(!heartbeat.expired(120999, timeout));
  assert(heartbeat.expired(121000, timeout));
  heartbeat.progress(120999); // Continuing upload or a completed loop.
  assert(!heartbeat.expired(121000, timeout));
  heartbeat.progress(UINT32_MAX - 1000);
  assert(heartbeat.age(999) == 2000);
  assert(!heartbeat.expired(118998, timeout));
  assert(heartbeat.expired(118999, timeout));
  heartbeat.progress(0);
  assert(heartbeat.age(0) == 0);
  puts("PASS: watchdog boundary, progress renewal, millisecond rollover");
}
