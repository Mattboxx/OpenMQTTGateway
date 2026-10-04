#include "../../main/RFSignalMemoryBudget.h"
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
  const size_t signal = 9680, reserve = 12000;
  assert(omg::rfSignalCopyHasHeadroom(signal + reserve, signal, signal, reserve));
  assert(!omg::rfSignalCopyHasHeadroom(signal + reserve - 1, signal, signal, reserve));
  assert(!omg::rfSignalCopyHasHeadroom(signal + reserve, signal - 1, signal, reserve));
  assert(!omg::rfSignalCopyHasHeadroom(signal - 1, signal, signal, 0));
  assert(!omg::rfSignalCopyHasHeadroom(30000, 20000, 0, reserve));
  const size_t maximum = std::numeric_limits<size_t>::max();
  assert(!omg::rfSignalCopyHasHeadroom(maximum, maximum, maximum, 1));
  assert(omg::rfSignalCopyHasHeadroom(maximum, maximum, maximum - reserve, reserve));
  assert(omg::rfSignalCopyHasHeadroom(24000, 24000, signal, reserve));
  assert(!omg::rfSignalCopyHasHeadroom(24000 - signal, 24000 - signal, signal, reserve));
  std::puts("PASS: RF copy admission, network reserve, fragmented heap, exact boundary and overflow");
}
