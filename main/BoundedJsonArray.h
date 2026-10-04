#pragma once
#include <ArduinoJson.h>
#include <stddef.h>

// Never pass a measured JSON length as the capacity of a smaller C array.
// A truncated module array is invalid JavaScript; use a valid empty fallback.
template <typename Source, size_t Capacity>
bool serializeJsonArrayBounded(const Source& source, char (&target)[Capacity]) {
  static_assert(Capacity >= 3, "JSON array fallback needs three bytes");
  const size_t expected = measureJson(source);
  if (expected < Capacity && serializeJson(source, target, Capacity) == expected) {
    target[expected] = '\0';
    return true;
  }
  target[0] = '[';
  target[1] = ']';
  target[2] = '\0';
  return false;
}
