"""Execute actual BLE presence/publish functions with clock/queue mocks.

No radio, network, GPIO or Home Assistant mutations. This tests state/timing
logic and ArduinoJson output, not real advertising reception or scheduling.
"""

import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "main/ZgatewayBLETracker.ino").read_text(encoding="utf-8")


def function(name):
    start = source.index("static void " + name + "(")
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


enqueue = function("enqueueBLETrackerState")
arduino_origin = 'String origin = String("/BTtracker/") + String(slot + 1);'
assert enqueue.count(arduino_origin) == 1
enqueue = enqueue.replace(arduino_origin,
                          'std::string origin = "/BTtracker/" + std::to_string(slot + 1);')
functions = function("processBLEAdvertisement") + "\n" + enqueue + "\n" + function("publishBLETrackerChanges")

harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
#include <cstdio>
#include <ArduinoJson.h>
using String = std::string;
#include "config_BLETracker.h"
#ifdef _WIN32
#define strcasecmp _stricmp
#endif
#define F(value) value
#define CR "\n"
const int pdTRUE = 1, pdFALSE = 0, QueueSemaphoreTimeOutTask = 50;
uint32_t fakeNow = 1000;
bool allowMutex = true, failQueue = false;
int bleTrackerMutex = 1;
uint32_t millis() { return fakeNow; }
uint32_t pdMS_TO_TICKS(uint32_t value) { return value; }
int xSemaphoreTake(int, uint32_t) { return allowMutex ? pdTRUE : pdFALSE; }
void xSemaphoreGive(int) {}
void rememberBLETrackerCandidate(const char *, const char *, int, uint32_t) {}
struct { uint32_t getFreeHeap() { return 30000; } } ESP;
struct { template <typename... Args> void verbose(const char *, Args...) {} } Log;
BLETrackerConfig_s BLETrackerConfig[BLE_TRACKER_MAX] = {};
uint32_t bleTrackerAdvertisements = 0, bleTrackerMatchedAdvertisements = 0;
bool bleTrackerPendingPublish[BLE_TRACKER_MAX] = {};
uint8_t bleTrackerPendingReason[BLE_TRACKER_MAX] = {};
uint32_t bleTrackerPublishRetryMs[BLE_TRACKER_MAX] = {};
bool bleTrackerInitialStatePending[BLE_TRACKER_MAX] = {};
uint32_t bleTrackerInitialStateSince[BLE_TRACKER_MAX] = {};
const uint8_t BLE_TRACKER_REASON_DETECTED = 1, BLE_TRACKER_REASON_REFRESH = 2;
struct State { bool present; bool rssiNull; int rssi; };
std::vector<State> sent;
unsigned attempts = 0;
bool enqueueJsonObject(JsonObject state, int) {
  ++attempts;
  assert(state["retain"].as<bool>());
  assert(state["origin"].as<std::string>() == "/BTtracker/1");
  if (failQueue) return false;
  sent.push_back({state["presence"].as<bool>(), state["rssi"].isNull(), state["rssi"].as<int>()});
  return true;
}
__FUNCTIONS__
void reset() {
  std::memset(BLETrackerConfig, 0, sizeof(BLETrackerConfig));
  std::memset(bleTrackerPendingPublish, 0, sizeof(bleTrackerPendingPublish));
  std::memset(bleTrackerPublishRetryMs, 0, sizeof(bleTrackerPublishRetryMs));
  std::memset(bleTrackerInitialStatePending, 0, sizeof(bleTrackerInitialStatePending));
  std::memset(bleTrackerInitialStateSince, 0, sizeof(bleTrackerInitialStateSince));
  allowMutex = true; failQueue = false; attempts = 0; sent.clear();
  BLETrackerConfig[0].enabled = true;
  BLETrackerConfig[0].timeoutSeconds = 300;
  BLETrackerConfig[0].minRssi = -90;
  BLETrackerConfig[0].lastRssi = -127;
  std::strcpy(BLETrackerConfig[0].mac, "00:11:22:33:44:55");
  std::strcpy(BLETrackerConfig[0].name, "Test beacon");
}
void detected(int rssi = -60) {
  processBLEAdvertisement("00:11:22:33:44:55", "Test beacon", rssi);
}
int main() {
  reset(); fakeNow = 1000;
  processBLEAdvertisement("00:11:22:33:44:66", "Other", -50);
  assert(!BLETrackerConfig[0].present);
  detected(-100); assert(!BLETrackerConfig[0].present);
  detected(); assert(BLETrackerConfig[0].present); // No timeout wait on arrival.
  publishBLETrackerChanges();
  assert(sent.size() == 1 && sent.back().present && sent.back().rssi == -60);
  fakeNow = 250000; detected(); publishBLETrackerChanges();
  assert(BLETrackerConfig[0].lastSeen == 250000);
  assert(sent.size() == 2 && sent.back().present);
  fakeNow = 300001; publishBLETrackerChanges();
  assert(BLETrackerConfig[0].present); // The new detection renewed the window.
  fakeNow = 549999; detected(-100); publishBLETrackerChanges();
  assert(BLETrackerConfig[0].present && BLETrackerConfig[0].lastSeen == 250000);
  fakeNow = 550000; publishBLETrackerChanges();
  assert(!BLETrackerConfig[0].present && !sent.back().present && sent.back().rssiNull);
  fakeNow = 550001; detected(); publishBLETrackerChanges();
  assert(sent.back().present && BLETrackerConfig[0].present);

  // A failed OFF publication stays pending and is retried after one second.
  fakeNow = 850001; failQueue = true; publishBLETrackerChanges();
  assert(!BLETrackerConfig[0].present && bleTrackerPendingPublish[0]);
  const auto beforeRetry = attempts;
  fakeNow += 999; publishBLETrackerChanges(); assert(attempts == beforeRetry);
  ++fakeNow; failQueue = false; publishBLETrackerChanges();
  assert(!sent.back().present && !bleTrackerPendingPublish[0] && bleTrackerPublishRetryMs[0] == 0);

  reset(); fakeNow = std::numeric_limits<uint32_t>::max() - 100;
  detected(); publishBLETrackerChanges();
  fakeNow += 299999; publishBLETrackerChanges(); assert(BLETrackerConfig[0].present);
  ++fakeNow; publishBLETrackerChanges(); assert(!BLETrackerConfig[0].present);

  // Retained startup state is not overwritten before a full initial timeout.
  reset(); fakeNow = 20000;
  bleTrackerInitialStatePending[0] = true; bleTrackerInitialStateSince[0] = fakeNow;
  fakeNow += 299999; publishBLETrackerChanges(); assert(sent.empty());
  ++fakeNow; publishBLETrackerChanges(); assert(!sent.back().present && sent.back().rssiNull);
  reset(); fakeNow = 30000; bleTrackerInitialStatePending[0] = true;
  bleTrackerInitialStateSince[0] = fakeNow;
  detected(); publishBLETrackerChanges();
  assert(sent.back().present && !bleTrackerInitialStatePending[0]);

  reset(); BLETrackerConfig[0].enabled = false; detected();
  assert(!BLETrackerConfig[0].present);
  reset(); allowMutex = false; detected();
  assert(!BLETrackerConfig[0].present);
  std::puts("PASS: actual BLE immediate presence, renewed timeout, RSSI filtering/null, retry, initial retention and rollover");
}
'''.replace("__FUNCTIONS__", functions)

with tempfile.TemporaryDirectory(prefix="omg-ble-presence-") as directory:
    cpp = Path(directory) / "presence.cpp"
    binary = Path(directory) / ("presence.exe" if os.name == "nt" else "presence")
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra", "-Werror", "-O2",
                    "-I", str(ROOT / "main"), "-I", str(ROOT / ".pio/libdeps/esp32dev-multi_receiver-wol-gpio-ble/ArduinoJson/src"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
