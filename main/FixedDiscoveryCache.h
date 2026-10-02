#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Fixed-capacity cache for discovery records. It deliberately performs no
// dynamic allocation: radio identifiers controlled by received packets must
// never be able to grow the ESP32 heap without a bound.
template <typename Record, size_t Capacity>
class FixedDiscoveryCache {
 public:
  enum class Result : uint8_t { Inserted, Updated, Evicted, Full, Invalid };

  Result upsert(const Record& value, uint32_t now) {
    if (!value.uniqueId[0]) return Result::Invalid;

    Slot* slot = findSlot(value.uniqueId);
    if (slot) {
      const bool wasDiscovered = slot->record.isDisc;
      slot->record = value;
      slot->record.isDisc = wasDiscovered || value.isDisc;
      slot->lastSeen = now;
      return Result::Updated;
    }

    for (size_t i = 0; i < Capacity; ++i) {
      if (!slots_[i].used) {
        store(slots_[i], value, now);
        ++size_;
        return Result::Inserted;
      }
    }

    // Never evict a record whose discovery message is still pending. Once a
    // record is published, reuse the least recently observed slot.
    Slot* oldest = nullptr;
    uint32_t oldestAge = 0;
    for (size_t i = 0; i < Capacity; ++i) {
      Slot& candidate = slots_[i];
      if (!candidate.record.isDisc) continue;
      const uint32_t age = now - candidate.lastSeen;
      if (!oldest || age > oldestAge) {
        oldest = &candidate;
        oldestAge = age;
      }
    }
    if (!oldest) return Result::Full;
    store(*oldest, value, now);
    return Result::Evicted;
  }

  Record* find(const char* uniqueId) {
    Slot* slot = findSlot(uniqueId);
    return slot ? &slot->record : nullptr;
  }

  bool snapshot(size_t index, Record& output) const {
    if (index >= Capacity || !slots_[index].used) return false;
    output = slots_[index].record;
    return true;
  }

  bool markDiscovered(const char* uniqueId) {
    Slot* slot = findSlot(uniqueId);
    if (!slot) return false;
    slot->record.isDisc = true;
    return true;
  }

  size_t size() const { return size_; }
  static constexpr size_t capacity() { return Capacity; }

 private:
  struct Slot {
    Record record;
    uint32_t lastSeen;
    bool used;
  };

  Slot* findSlot(const char* uniqueId) {
    if (!uniqueId || !uniqueId[0]) return nullptr;
    for (size_t i = 0; i < Capacity; ++i) {
      if (slots_[i].used && strcmp(slots_[i].record.uniqueId, uniqueId) == 0)
        return &slots_[i];
    }
    return nullptr;
  }

  static void store(Slot& slot, const Record& value, uint32_t now) {
    slot.record = value;
    slot.lastSeen = now;
    slot.used = true;
  }

  Slot slots_[Capacity] = {};
  size_t size_ = 0;
};
