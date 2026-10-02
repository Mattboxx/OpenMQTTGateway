#pragma once
#include <stdint.h>

struct LwipTimerReserveSnapshot {
  bool compatible;
  uint32_t capacity;
  uint32_t used;
  uint32_t peak;
  uint32_t fallback;
};
#if defined(ESP32) && defined(OMG_LWIP_TIMER_RESERVE)
LwipTimerReserveSnapshot lwipTimerReserveSnapshot();
#else
inline LwipTimerReserveSnapshot lwipTimerReserveSnapshot() { return {}; }
#endif
