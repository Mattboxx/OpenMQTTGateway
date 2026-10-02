#include "../../main/FixedDiscoveryCache.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct Device {
  char uniqueId[16];
  char model[8];
  bool isDisc;
};

static Device device(const char* id, const char* model = "model") {
  Device result = {};
  strncpy(result.uniqueId, id, sizeof(result.uniqueId) - 1);
  strncpy(result.model, model, sizeof(result.model) - 1);
  return result;
}

int main() {
  FixedDiscoveryCache<Device, 3> cache;
  assert(cache.upsert(device("one"), 100) == decltype(cache)::Result::Inserted);
  assert(cache.upsert(device("two"), 200) == decltype(cache)::Result::Inserted);
  assert(cache.upsert(device("three"), 300) == decltype(cache)::Result::Inserted);
  assert(cache.size() == 3);

  // A burst cannot evict discovery records that have not yet been published.
  assert(cache.upsert(device("four"), 400) == decltype(cache)::Result::Full);
  assert(cache.find("four") == nullptr);

  assert(cache.markDiscovered("one"));
  assert(cache.markDiscovered("two"));
  assert(cache.upsert(device("two", "changed"), 500) == decltype(cache)::Result::Updated);
  assert(cache.find("two")->isDisc);
  assert(strcmp(cache.find("two")->model, "changed") == 0);

  // Only discovered entries are recyclable; the oldest one is selected.
  assert(cache.upsert(device("four"), 600) == decltype(cache)::Result::Evicted);
  assert(cache.find("one") == nullptr);
  assert(cache.find("four") != nullptr);
  assert(cache.find("three") != nullptr);

  // Unsigned subtraction keeps age ordering correct across millis() rollover.
  FixedDiscoveryCache<Device, 2> rollover;
  assert(rollover.upsert(device("old"), UINT32_MAX - 20) == decltype(rollover)::Result::Inserted);
  assert(rollover.markDiscovered("old"));
  assert(rollover.upsert(device("new"), 5) == decltype(rollover)::Result::Inserted);
  assert(rollover.markDiscovered("new"));
  assert(rollover.upsert(device("replacement"), 20) == decltype(rollover)::Result::Evicted);
  assert(rollover.find("old") == nullptr);
  assert(rollover.find("new") != nullptr);

  Device copy = {};
  size_t snapshots = 0;
  for (size_t i = 0; i < decltype(cache)::capacity(); ++i)
    if (cache.snapshot(i, copy)) ++snapshots;
  assert(snapshots == cache.size());

  puts("PASS: fixed discovery cache capacity, pending protection, eviction and rollover");
}
