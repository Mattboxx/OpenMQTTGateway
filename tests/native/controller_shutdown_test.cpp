#include "../../main/ControllerShutdown.h"
#include <assert.h>
#include <stdio.h>
#include <string>

int main() {
  for (int failed = 0; failed <= 4; ++failed) {
    std::string sequence;
    auto result = shutdownController(ControllerState::Enabled, true, true,
      [&]() { sequence += 'p'; return failed != 1; },
      [&]() { sequence += 'd'; return failed == 2 ? -1 : 0; },
      [&]() { sequence += 'i'; return failed == 3 ? -1 : 0; },
      [&]() { sequence += 'r'; return failed == 4 ? -1 : 0; });
    assert(sequence == (failed == 1 ? "p" : failed == 2 ? "pd" : failed == 3 ? "pdi" : "pdir"));
    assert(result == (failed == 0 ? ControllerShutdownResult::Complete :
                     failed == 1 ? ControllerShutdownResult::ScanStopFailed :
                     failed == 2 ? ControllerShutdownResult::DisableFailed :
                     failed == 3 ? ControllerShutdownResult::DeinitFailed : ControllerShutdownResult::ReleaseFailed));
  }
  std::string calls;
  auto pause = [&]() { calls += 'p'; return true; };
  auto disable = [&]() { calls += 'd'; return 0; };
  auto deinit = [&]() { calls += 'i'; return 0; };
  auto release = [&]() { calls += 'r'; return 0; };
  assert(pauseControllerScan(false, pause));
  assert(calls.empty());
  assert(pauseControllerScan(true, pause));
  assert(calls == "p");
  calls.clear();
  assert(!pauseControllerScan(true, [&]() { calls += 'p'; return false; }));
  assert(calls == "p");
  calls.clear();
  assert(shutdownController(ControllerState::Idle, false, false, pause, disable, deinit, release) == ControllerShutdownResult::Complete);
  assert(calls.empty());
  assert(shutdownController(ControllerState::Enabled, false, false, pause, disable, deinit, release) == ControllerShutdownResult::Busy);
  assert(calls.empty());
  assert(shutdownController(ControllerState::Initialized, false, true, pause, disable, deinit, release) == ControllerShutdownResult::Complete);
  assert(calls == "ir");
  calls.clear();
  assert(shutdownController(ControllerState::Enabled, false, true, pause, disable, deinit, release) == ControllerShutdownResult::Complete);
  assert(calls == "dir");
  puts("PASS: scan-only OTA pause, failure propagation, ordered shutdown and stop on every API failure");
}
