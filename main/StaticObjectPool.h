#pragma once
#include <stddef.h>

// The caller serializes access. Each returned object has its natural alignment
// and can only be released by its exact address; foreign pointers pass through.
template <typename T, size_t Capacity>
class StaticObjectPool {
  static_assert(Capacity > 0, "Pool requires storage");
  T objects_[Capacity] = {};
  bool used_[Capacity] = {};
  size_t usedCount_ = 0;
  size_t peak_ = 0;
public:
  T* allocate() {
    for (size_t i = 0; i < Capacity; ++i) {
      if (used_[i]) continue;
      used_[i] = true;
      if (++usedCount_ > peak_) peak_ = usedCount_;
      return &objects_[i];
    }
    return nullptr;
  }
  bool release(void* pointer) {
    for (size_t i = 0; i < Capacity; ++i) {
      if (pointer != &objects_[i]) continue;
      if (used_[i]) { used_[i] = false; --usedCount_; }
      return true;
    }
    return false;
  }
  size_t used() const { return usedCount_; }
  size_t peak() const { return peak_; }
  static constexpr size_t capacity() { return Capacity; }
};
