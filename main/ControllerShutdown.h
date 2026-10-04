#pragma once
#include <stdint.h>

enum class ControllerState : uint8_t { Idle, Initialized, Enabled };
enum class ControllerShutdownResult : uint8_t { Complete, Busy, ScanStopFailed, DisableFailed, DeinitFailed, ReleaseFailed };

// OTA needs scanning quiet, not a controller lifetime transition underneath
// the active Wi-Fi transport. Reboot after the attempt restores normal state.
template <typename Pause>
bool pauseControllerScan(bool scanning, Pause pause) {
  return !scanning || pause();
}

// A failed step must not execute the next SDK operation or claim freed memory.
template <typename Pause, typename Disable, typename Deinit, typename Release>
ControllerShutdownResult shutdownController(ControllerState state, bool scanning,
    bool commandsSettled, Pause pause, Disable disable, Deinit deinit, Release release) {
  if (state == ControllerState::Idle) return ControllerShutdownResult::Complete;
  if (state == ControllerState::Enabled) {
    if (!commandsSettled) return ControllerShutdownResult::Busy;
    if (scanning && !pause()) return ControllerShutdownResult::ScanStopFailed;
    if (disable() != 0) return ControllerShutdownResult::DisableFailed;
  }
  if (deinit() != 0) return ControllerShutdownResult::DeinitFailed;
  if (release() != 0) return ControllerShutdownResult::ReleaseFailed;
  return ControllerShutdownResult::Complete;
}
