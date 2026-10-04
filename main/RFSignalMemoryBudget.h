#ifndef OMG_RF_SIGNAL_MEMORY_BUDGET_H
#define OMG_RF_SIGNAL_MEMORY_BUDGET_H

#include <stddef.h>

namespace omg {
inline bool rfSignalCopyHasHeadroom(size_t usable, size_t largest,
                                    size_t signalBytes, size_t networkReserve) {
  // Subtract only after checking: no overflow when signalBytes is malformed.
  return signalBytes > 0 && signalBytes <= largest && signalBytes <= usable &&
         usable - signalBytes >= networkReserve;
}
} // namespace omg

#endif
