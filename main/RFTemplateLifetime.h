#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

namespace omg {
inline bool rfTemplateAddressInside(uintptr_t address, uintptr_t begin, uintptr_t end) {
  return address >= begin && address < end;
}

// Registration must own independent decoder records before the startup-only
// template table is released. Refuse an alias (including nested data pointers)
// rather than turning an unexpected factory implementation into a dangling
// decoder. Does not free decoder records, contexts or the protocol list.
template <typename Device>
size_t releaseRFTemplateTable(Device*& templates, size_t count,
                              void* const* registered, size_t registeredCount) {
  if (!templates || !count || (registeredCount && !registered)) return 0;
  const uintptr_t begin = reinterpret_cast<uintptr_t>(templates);
  if (count > (UINTPTR_MAX - begin) / sizeof(Device)) return 0;
  const size_t bytes = count * sizeof(Device);
  const uintptr_t end = begin + bytes;
  for (size_t i = 0; i < registeredCount; ++i) {
    const Device* device = static_cast<const Device*>(registered[i]);
    if (!device || rfTemplateAddressInside(reinterpret_cast<uintptr_t>(device), begin, end)) return 0;
    if (rfTemplateAddressInside(reinterpret_cast<uintptr_t>(device->name), begin, end) ||
        rfTemplateAddressInside(reinterpret_cast<uintptr_t>(device->fields), begin, end) ||
        rfTemplateAddressInside(reinterpret_cast<uintptr_t>(device->decode_ctx), begin, end) ||
        rfTemplateAddressInside(reinterpret_cast<uintptr_t>(device->output_ctx), begin, end)) return 0;
  }
  free(templates);
  templates = nullptr;
  return bytes;
}
} // namespace omg
