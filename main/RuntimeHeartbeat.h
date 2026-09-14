#pragma once
#include <stdint.h>

// Access only while holding the runtime progress mutex. Sample 'now' under the
// same mutex, so a concurrent update cannot make the timestamp appear future.
struct RuntimeHeartbeat {
  uint32_t last = 0;
  void progress(uint32_t now) { last = now; }
  uint32_t age(uint32_t now) const { return now - last; }
  bool expired(uint32_t now, uint32_t timeout) const { return age(now) >= timeout; }
};
