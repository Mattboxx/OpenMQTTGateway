#include "LwipTimerReserve.h"
#if defined(ESP32) && defined(OMG_LWIP_TIMER_RESERVE)
#include "StaticObjectPool.h"
#include <freertos/FreeRTOS.h>
#include <sdkconfig.h>
#include <lwip/memp.h>
#include <lwip/timeouts.h>

// The pinned IDF uses MEMP_MEM_MALLOC=1: SYS_TIMEOUT allocation is malloc(),
// and sys_timeout_abs asserts on failure. Give only these essential timer
// objects fixed storage. Other lwIP pools retain the original SDK allocator.
// Linker wrapping intercepts calls from the precompiled lwIP archive too;
// changing MEMP_NUM_SYS_TIMEOUT in application flags would not rebuild it.
namespace {
// Covers the pinned SDK's cyclic timers with ample room for DHCP/DNS events.
// Optional capacity override is meaningful here because this storage is built
// as application code, rather than a flag on the precompiled lwIP library.
#ifndef OMG_LWIP_TIMER_CAPACITY
#  define OMG_LWIP_TIMER_CAPACITY 32
#endif
StaticObjectPool<sys_timeo, OMG_LWIP_TIMER_CAPACITY> timerPool;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t fallbacks;
bool compatible() { return memp_pools[MEMP_SYS_TIMEOUT]->size == sizeof(sys_timeo); }
}

extern "C" void* __real_memp_malloc(memp_t type);
extern "C" void __real_memp_free(memp_t type, void* pointer);

extern "C" void* __wrap_memp_malloc(memp_t type) {
  if (type == MEMP_SYS_TIMEOUT && compatible()) {
    portENTER_CRITICAL(&timerMux);
    void* result = timerPool.allocate();
    if (!result) ++fallbacks;
    portEXIT_CRITICAL(&timerMux);
    if (result) return result;
  }
  return __real_memp_malloc(type);
}

extern "C" void __wrap_memp_free(memp_t type, void* pointer) {
  if (type == MEMP_SYS_TIMEOUT) {
    portENTER_CRITICAL(&timerMux);
    const bool owned = timerPool.release(pointer);
    portEXIT_CRITICAL(&timerMux);
    if (owned) return;
  }
  __real_memp_free(type, pointer);
}

LwipTimerReserveSnapshot lwipTimerReserveSnapshot() {
  LwipTimerReserveSnapshot result = {};
  result.compatible = compatible();
  result.capacity = timerPool.capacity();
  portENTER_CRITICAL(&timerMux);
  result.used = timerPool.used();
  result.peak = timerPool.peak();
  result.fallback = fallbacks;
  portEXIT_CRITICAL(&timerMux);
  return result;
}
#endif
