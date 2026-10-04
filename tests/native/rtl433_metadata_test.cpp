#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>
#include "r_private.h"

int main() {
  pulse_data_t pulses = {};
  dm_state state = {};
  static_assert(sizeof(pulses.pulse) / sizeof(pulses.pulse[0]) == PD_MAX_PULSES,
                "Real decoder pulse capacity must remain unchanged");
  static_assert(sizeof(pulses.gap) == sizeof(pulses.pulse), "Gap capacity");
#ifdef OMG_RTL433_COMPACT_METADATA
  static_assert(sizeof(state.pulse_data) <= 2 * sizeof(unsigned long),
                "Only RSSI and duration belong in the compact copy");
  for (int rssi : {-127, -82, 0, 20}) {
    for (unsigned long duration : {0UL, 1000UL, std::numeric_limits<unsigned long>::max()}) {
      pulses.signalRssi = rssi;
      pulses.signalDuration = duration;
      pulses.pulse[PD_MAX_PULSES - 1] = 42;
      pulses.gap[PD_MAX_PULSES - 1] = 84;
      omg_rtl433_set_metadata(&state, &pulses);
      assert(state.pulse_data.signalRssi == rssi);
      assert(state.pulse_data.signalDuration == duration);
      assert(pulses.pulse[PD_MAX_PULSES - 1] == 42);
      assert(pulses.gap[PD_MAX_PULSES - 1] == 84);
    }
  }
#else
  static_assert(sizeof(state.pulse_data) == sizeof(pulse_data_t), "Default layout");
  state.pulse_data = pulses;
  assert(state.pulse_data.num_pulses == pulses.num_pulses);
#endif
  std::printf("RF metadata: real input=%zu, stored copy=%zu, saved=%zu bytes\n",
              sizeof(pulses), sizeof(state.pulse_data), sizeof(pulses) - sizeof(state.pulse_data));
}
