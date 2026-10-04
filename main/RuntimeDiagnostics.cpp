#include "RuntimeDiagnostics.h"
#include "RuntimeHeartbeat.h"
#include "NetworkLiveness.h"
#include "LwipTimerReserve.h"
#include "WifiMemoryBudget.h"
#ifdef ZgatewayBLETracker
#  include "config_BLETracker.h"
#endif
#if defined(ESP32) && defined(OMG_RUNTIME_DIAGNOSTICS)
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_core_dump.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <esp_debug_helpers.h>
#include <esp_private/system_internal.h>
#include <soc/rtc_wdt.h>
#include <stdlib.h>

namespace {
constexpr uint32_t recordMagic = 0x4F4D4431;
constexpr uint32_t timeoutMs = 120000;
constexpr uint32_t hardwareTimeoutMs = 180000;
struct Record {
  uint32_t magic;
  uint32_t uptimeMs;
  uint32_t stalledMs;
  uint32_t freeHeap;
  uint32_t minHeap;
  uint32_t disconnects;
  uint8_t wifiReason;
  uint8_t phase;
  uint8_t kind; // 1=stalled loop, 2=failed WiFi recovery window, 3=hardware watchdog
  uint8_t reserved;
  char version[48];
};
RTC_NOINIT_ATTR Record pendingRecord;
Record lastRecord = {};
Record lastWifiRecord = {};
constexpr uint32_t otaRecordMagic = 0x4F544131;
struct OTARecord {
  uint32_t magic;
  uint32_t uptimeMs;
  uint32_t expected;
  uint32_t accepted;
  int32_t error;
  int32_t detail;
  uint8_t transport;
  uint8_t stage;
  uint8_t lastStage;
  uint8_t reserved;
  char version[48];
};
RTC_NOINIT_ATTR OTARecord pendingOTARecord;
OTARecord lastOTARecord = {};
portMUX_TYPE progressMux = portMUX_INITIALIZER_UNLOCKED;
RuntimeHeartbeat heartbeat;
uint32_t disconnects;
uint8_t wifiReason;
uint32_t sampledFreeHeap;
uint32_t sampledMinHeap;
RuntimePhase phase = RuntimePhase::Loop;
bool guardStarted;
bool hardwareGuardStarted;
bool preserveCrash;
portMUX_TYPE allocationMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t allocationFailures;
uint32_t lastAllocationBytes;
uint32_t lastAllocationCaps;
char lastAllocationFunction[40];
constexpr size_t allocationTraceDepth = 8;
uint32_t allocationTrace[allocationTraceDepth];
uint32_t allocationTraceMs;
uint32_t allocationTraceBytes;
uint32_t allocationTraceCaps;
uint8_t allocationTraceCount;
bool allocationTraceClaimed;
char allocationTraceTask[17];

void allocationFailed(size_t bytes, uint32_t caps, const char* function) {
  // This callback can run under allocator/network locks. Record only fixed
  // fields; never allocate, query the heap, log or write flash here.
  portENTER_CRITICAL(&allocationMux);
  ++allocationFailures;
  lastAllocationBytes = bytes;
  lastAllocationCaps = caps;
  strlcpy(lastAllocationFunction, function ? function : "unknown", sizeof(lastAllocationFunction));
  const uint32_t now = millis();
  const bool capture = !allocationTraceClaimed || now - allocationTraceMs >= 10000UL;
  if (capture) {
    allocationTraceClaimed = true;
    allocationTraceMs = now;
  }
  portEXIT_CRITICAL(&allocationMux);
  if (!capture) return;
  // Fixed-size, throttled stack walking only: no printing, heap inspection or
  // flash writes inside the failure hook. Capture outside the critical section.
  esp_backtrace_frame_t frame = {};
  esp_backtrace_get_start(&frame.pc, &frame.sp, &frame.next_pc);
  uint32_t trace[allocationTraceDepth] = {};
  uint8_t count = 0;
  do {
    // Xtensa return PCs can include register-window bits; normalize for ELF
    // symbol decoding without storing stack contents or application secrets.
    trace[count++] = (frame.pc & 0x3FFFFFFFU) | 0x40000000U;
  } while (count < allocationTraceDepth && frame.next_pc && esp_backtrace_get_next_frame(&frame));
  char task[sizeof(allocationTraceTask)];
  strlcpy(task, pcTaskGetName(nullptr), sizeof(task));
  portENTER_CRITICAL(&allocationMux);
  memcpy(allocationTrace, trace, sizeof(trace));
  allocationTraceCount = count;
  allocationTraceBytes = bytes;
  allocationTraceCaps = caps;
  memcpy(allocationTraceTask, task, sizeof(task));
  portEXIT_CRITICAL(&allocationMux);
}

const char* phaseName(uint8_t value) {
  static const char* names[] = {"loop", "wifi", "web", "mqtt", "sensors", "queue", "ota", "restart"};
  return value < 8 ? names[value] : "unknown";
}

void saveRecord(const char* key, const Record& record) {
  Preferences preferences;
  if (preferences.begin("omgdiag", false)) {
    preferences.putBytes(key, &record, sizeof(record));
    preferences.end();
  }
}

Record snapshot(uint8_t kind) {
  Record result = {};
  result.magic = recordMagic;
  portENTER_CRITICAL(&progressMux);
  // Sample time under the same lock as lastProgress: a newer heartbeat must
  // never look like a full uint32 rollover and trigger an immediate reset.
  result.uptimeMs = millis();
  result.stalledMs = heartbeat.age(result.uptimeMs);
  result.disconnects = disconnects;
  result.wifiReason = wifiReason;
  result.phase = static_cast<uint8_t>(phase);
  // Never acquire the heap allocator lock from the emergency guard. A task
  // stalled inside that allocator must not also stall its recovery task.
  result.freeHeap = sampledFreeHeap;
  result.minHeap = sampledMinHeap;
  portEXIT_CRITICAL(&progressMux);
  result.kind = kind;
  strlcpy(result.version, OMG_VERSION, sizeof(result.version));
  return result;
}

void guardTask(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    const Record record = snapshot(1);
    if (record.stalledMs < timeoutMs) continue;
    // No Log, Serial.flush, MQTT or WiFi here: one of them may be the deadlock.
    pendingRecord = record;
    // Preserve an older crash, if present. Otherwise abort creates a core dump
    // containing both the guard and the blocked task's stacks.
    // esp_restart() first invokes registered shutdown handlers, including the
    // radio drivers that may be stuck. Use the pinned IDF's emergency reset
    // path here (also used by its panic handler), bypassing those callbacks.
    if (preserveCrash) esp_restart_noos();
    abort();
  }
}

bool armHardwareGuard() {
  // The RTC watchdog runs outside FreeRTOS and both CPUs. It remains capable
  // of resetting the board when a radio/driver deadlock prevents the software
  // guard task from being scheduled. A full RTC reset deliberately resembles
  // a power cycle, which has been the only reliable manual recovery observed.
  rtc_wdt_protect_off();
  rtc_wdt_disable();
  bool ok = rtc_wdt_set_length_of_reset_signal(RTC_WDT_SYS_RESET_SIG, RTC_WDT_LENGTH_3_2us) == ESP_OK;
  ok = rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_RTC) == ESP_OK && ok;
  ok = rtc_wdt_set_stage(RTC_WDT_STAGE1, RTC_WDT_STAGE_ACTION_OFF) == ESP_OK && ok;
  ok = rtc_wdt_set_stage(RTC_WDT_STAGE2, RTC_WDT_STAGE_ACTION_OFF) == ESP_OK && ok;
  ok = rtc_wdt_set_stage(RTC_WDT_STAGE3, RTC_WDT_STAGE_ACTION_OFF) == ESP_OK && ok;
  ok = rtc_wdt_set_time(RTC_WDT_STAGE0, hardwareTimeoutMs) == ESP_OK && ok;
  if (ok) {
    rtc_wdt_enable();
    rtc_wdt_feed();
  } else {
    rtc_wdt_disable();
  }
  rtc_wdt_protect_on();
  return ok && rtc_wdt_is_on();
}
} // namespace

void runtimeProgress(RuntimePhase value) {
  // Sample from the main loop only. If a heap query itself blocks, the guard
  // can still inspect the last heartbeat and use the previous memory sample.
  static uint32_t lastMemorySample;
  if (value == RuntimePhase::Loop &&
      (!lastMemorySample || millis() - lastMemorySample >= 5000UL)) {
    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t minHeap = ESP.getMinFreeHeap();
    portENTER_CRITICAL(&progressMux);
    sampledFreeHeap = freeHeap;
    sampledMinHeap = minHeap;
    portEXIT_CRITICAL(&progressMux);
    lastMemorySample = millis();
  }
  portENTER_CRITICAL(&progressMux);
  phase = value;
  heartbeat.progress(millis());
  portEXIT_CRITICAL(&progressMux);
  if (hardwareGuardStarted) rtc_wdt_feed();
}

void runtimePhase(RuntimePhase value) {
  portENTER_CRITICAL(&progressMux);
  phase = value;
  portEXIT_CRITICAL(&progressMux);
}

void runtimeWiFiEvent(uint8_t reason) {
  portENTER_CRITICAL(&progressMux);
  ++disconnects;
  wifiReason = reason;
  portEXIT_CRITICAL(&progressMux);
}

void runtimeRememberWiFiFailure() {
  // Called once per outage, never from the WiFi callback or per advertisement.
  lastWifiRecord = snapshot(2);
  saveRecord("wifi", lastWifiRecord);
}

void runtimeOTAStart(OTATransport transport, uint32_t expected) {
  OTARecord record = {};
  record.magic = otaRecordMagic;
  record.uptimeMs = millis();
  record.expected = expected;
  record.transport = static_cast<uint8_t>(transport);
  record.stage = static_cast<uint8_t>(OTAStage::Starting);
  strlcpy(record.version, OMG_VERSION, sizeof(record.version));
  portENTER_CRITICAL(&progressMux);
  pendingOTARecord = record;
  portEXIT_CRITICAL(&progressMux);
}

void runtimeOTAStep(OTAStage stage) {
  portENTER_CRITICAL(&progressMux);
  if (pendingOTARecord.magic == otaRecordMagic) {
    pendingOTARecord.lastStage = pendingOTARecord.stage;
    pendingOTARecord.stage = static_cast<uint8_t>(stage);
    pendingOTARecord.uptimeMs = millis();
  }
  portEXIT_CRITICAL(&progressMux);
}

void runtimeOTAAccepted(uint32_t total, uint32_t expected) {
  portENTER_CRITICAL(&progressMux);
  if (pendingOTARecord.magic == otaRecordMagic) {
    pendingOTARecord.accepted = total;
    if (expected) pendingOTARecord.expected = expected;
    pendingOTARecord.uptimeMs = millis();
  }
  portEXIT_CRITICAL(&progressMux);
}

void runtimeOTAFinish(bool success, int error, int detail) {
  portENTER_CRITICAL(&progressMux);
  if (pendingOTARecord.magic == otaRecordMagic) {
    if (pendingOTARecord.stage != static_cast<uint8_t>(OTAStage::Complete) &&
        pendingOTARecord.stage != static_cast<uint8_t>(OTAStage::Failed))
      pendingOTARecord.lastStage = pendingOTARecord.stage;
    pendingOTARecord.stage = static_cast<uint8_t>(success ? OTAStage::Complete : OTAStage::Failed);
    pendingOTARecord.error = error;
    if (detail) pendingOTARecord.detail = detail;
    pendingOTARecord.uptimeMs = millis();
  }
  portEXIT_CRITICAL(&progressMux);
}

void runtimeDiagnosticsBegin() {
  heap_caps_register_failed_alloc_callback(allocationFailed);
  const esp_reset_reason_t bootResetReason = esp_reset_reason();
  Preferences preferences;
  if (preferences.begin("omgdiag", false)) {
    if (preferences.getBytesLength("last") == sizeof(lastRecord))
      preferences.getBytes("last", &lastRecord, sizeof(lastRecord));
    if (preferences.getBytesLength("wifi") == sizeof(lastWifiRecord))
      preferences.getBytes("wifi", &lastWifiRecord, sizeof(lastWifiRecord));
    if (preferences.getBytesLength("ota") == sizeof(lastOTARecord))
      preferences.getBytes("ota", &lastOTARecord, sizeof(lastOTARecord));
    if (bootResetReason != ESP_RST_POWERON && pendingOTARecord.magic == otaRecordMagic) {
      lastOTARecord = pendingOTARecord;
      if (lastOTARecord.stage != static_cast<uint8_t>(OTAStage::Complete) &&
          lastOTARecord.stage != static_cast<uint8_t>(OTAStage::Failed)) {
        lastOTARecord.lastStage = lastOTARecord.stage;
        lastOTARecord.stage = static_cast<uint8_t>(OTAStage::Interrupted);
      }
      preferences.putBytes("ota", &lastOTARecord, sizeof(lastOTARecord));
    }
    // Revision 5 used the severe-incident slot for Wi-Fi recovery records.
    // Migrate it once so future network outages cannot erase evidence from a
    // stalled loop or hardware watchdog reset.
    if (lastRecord.magic == recordMagic && lastRecord.kind == 2) {
      if (lastWifiRecord.magic != recordMagic) {
        lastWifiRecord = lastRecord;
        preferences.putBytes("wifi", &lastWifiRecord, sizeof(lastWifiRecord));
      }
      preferences.remove("last");
      lastRecord = {};
    }
    preferences.end();
  }
  lastRecord.version[sizeof(lastRecord.version) - 1] = '\0';
  lastWifiRecord.version[sizeof(lastWifiRecord.version) - 1] = '\0';
  lastOTARecord.version[sizeof(lastOTARecord.version) - 1] = '\0';
  pendingOTARecord.magic = 0;
  if (bootResetReason != ESP_RST_POWERON && pendingRecord.magic == recordMagic) {
    lastRecord = pendingRecord;
    saveRecord("last", lastRecord);
  }
  pendingRecord.magic = 0;
  if (bootResetReason == ESP_RST_WDT) {
    Record hardwareRecord = {};
    hardwareRecord.magic = recordMagic;
    hardwareRecord.kind = 3;
    hardwareRecord.phase = 255;
    hardwareRecord.freeHeap = ESP.getFreeHeap();
    hardwareRecord.minHeap = ESP.getMinFreeHeap();
    hardwareRecord.reserved = static_cast<uint8_t>(bootResetReason);
    strlcpy(hardwareRecord.version, OMG_VERSION, sizeof(hardwareRecord.version));
    lastRecord = hardwareRecord;
    saveRecord("last", lastRecord);
  }
  size_t address = 0, size = 0;
  preserveCrash = esp_core_dump_image_get(&address, &size) == ESP_OK && size;
  runtimeProgress(RuntimePhase::Loop);
  guardStarted = xTaskCreatePinnedToCore(guardTask, "omgRuntime", 3072, nullptr, 2, nullptr, 0) == pdPASS;
  hardwareGuardStarted = armHardwareGuard();
  // Each completed flash write proves progress even when OTA occupies loop().
  Update.onProgress([](size_t, size_t) { runtimeProgress(RuntimePhase::OTA); });
}

String runtimeDiagnosticsJSON() {
  StaticJsonDocument<2304> json;
  const Record current = snapshot(0);
  json["version"] = OMG_VERSION;
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running) {
    json["running_partition"] = running->label;
    json["running_address"] = running->address;
  }
  json["uptime_ms"] = current.uptimeMs;
  json["reset_reason"] = static_cast<unsigned>(esp_reset_reason());
  json["runtime_guard"] = guardStarted;
  json["timeout_ms"] = timeoutMs;
  json["hardware_guard"] = hardwareGuardStarted;
  json["hardware_timeout_ms"] = hardwareTimeoutMs;
  json["emergency_reset"] = "no_shutdown_handlers";
  json["phase"] = phaseName(current.phase);
  json["progress_age_ms"] = current.stalledMs;
  json["heap"] = current.freeHeap;
  json["min_heap"] = current.minHeap;
  json["max_alloc"] = ESP.getMaxAllocHeap();
  // Arduino's INTERNAL-only metrics include 32-bit-only RAM, which cannot
  // satisfy malloc()/Wi-Fi packet buffers. Expose their actual capability set.
  constexpr uint32_t packetCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT;
  json["heap_default"] = heap_caps_get_free_size(packetCaps);
  json["min_heap_default"] = heap_caps_get_minimum_free_size(packetCaps);
  json["max_alloc_default"] = heap_caps_get_largest_free_block(packetCaps);
  uint32_t failures, failedBytes, failedCaps;
  char failedFunction[40];
  uint32_t trace[allocationTraceDepth], traceMs, traceBytes, traceCaps;
  uint8_t traceCount;
  char traceTask[sizeof(allocationTraceTask)];
  portENTER_CRITICAL(&allocationMux);
  failures = allocationFailures;
  failedBytes = lastAllocationBytes;
  failedCaps = lastAllocationCaps;
  memcpy(failedFunction, lastAllocationFunction, sizeof(failedFunction));
  memcpy(trace, allocationTrace, sizeof(trace));
  traceMs = allocationTraceMs;
  traceBytes = allocationTraceBytes;
  traceCaps = allocationTraceCaps;
  traceCount = allocationTraceCount;
  memcpy(traceTask, allocationTraceTask, sizeof(traceTask));
  portEXIT_CRITICAL(&allocationMux);
  json["alloc_failures"] = failures;
  json["alloc_last_bytes"] = failedBytes;
  json["alloc_last_caps"] = failedCaps;
  json["alloc_last_function"] = failedFunction;
  json["alloc_trace_ms"] = traceMs;
  json["alloc_trace_bytes"] = traceBytes;
  json["alloc_trace_caps"] = traceCaps;
  json["alloc_trace_task"] = traceTask;
  JsonArray traceJSON = json.createNestedArray("alloc_trace_pc");
  for (uint8_t i = 0; i < traceCount; ++i) traceJSON.add(trace[i]);
#ifdef OMG_LWIP_TIMER_RESERVE
  const LwipTimerReserveSnapshot timers = lwipTimerReserveSnapshot();
  json["timer_reserve"] = timers.compatible;
  json["timer_capacity"] = timers.capacity;
  json["timer_used"] = timers.used;
  json["timer_peak"] = timers.peak;
  json["timer_fallbacks"] = timers.fallback;
#endif
  json["wifi_disconnects"] = current.disconnects;
  json["wifi_last_reason"] = current.wifiReason;
#ifdef OMG_WIFI_MEMORY_BUDGET
  const WifiMemoryBudgetSnapshot budget = wifiMemoryBudgetSnapshot();
  json["wifi_buffer_budget"] = budget.initialized;
  json["wifi_buffer_rx"] = budget.rx;
  json["wifi_buffer_tx"] = budget.tx;
  json["wifi_buffer_ba"] = budget.ba;
  json["wifi_buffer_static_rx"] = budget.staticRx;
  json["wifi_ampdu_rx"] = budget.ampduRx;
  json["wifi_ampdu_tx"] = budget.ampduTx;
#endif
#ifdef WIFI_GATEWAY_LIVENESS
  const NetworkLivenessSnapshot network = networkLivenessSnapshot();
  json["network_guard"] = network.started;
  json["network_target"] = IPAddress(network.target).toString();
  json["network_replies"] = network.replies;
  json["network_timeouts"] = network.timeouts;
  json["network_consecutive_failures"] = network.consecutiveFailures;
  json["network_recoveries"] = network.recoveries;
  json["network_last_result_age_ms"] = network.lastResultAgeMs;
#endif
#ifdef ZgatewayBLETracker
  const BLETrackerRuntimeSnapshot_s ble = getBLETrackerRuntimeSnapshot();
  json["ble_started"] = ble.started;
  json["ble_scanning"] = ble.scanning;
  json["ble_blocked"] = ble.blocked;
  json["ble_paused_web"] = ble.pausedWeb;
  json["ble_paused_wifi"] = ble.pausedWiFi;
  json["ble_advertisements"] = ble.advertisements;
  json["ble_matched"] = ble.matched;
  json["ble_dropped"] = ble.dropped;
  json["ble_pending"] = ble.pending;
  json["ble_scan_interval_ms"] = BLE_TRACKER_SCAN_INTERVAL_MS;
  json["ble_scan_window_ms"] = BLE_TRACKER_SCAN_WINDOW_MS;
#endif
  size_t address = 0, size = 0;
  const bool available = esp_core_dump_image_get(&address, &size) == ESP_OK && size;
  json["crash_available"] = available;
  json["crash_bytes"] = available ? size : 0;
  if (lastRecord.magic == recordMagic) {
    JsonObject last = json.createNestedObject("last_incident");
    last["kind"] = lastRecord.kind == 1 ? "loop_stalled" :
                   lastRecord.kind == 2 ? "wifi_recovery_failed" :
                                          "hardware_watchdog_reset";
    last["version"] = lastRecord.version;
    last["phase"] = phaseName(lastRecord.phase);
    last["uptime_ms"] = lastRecord.uptimeMs;
    last["stalled_ms"] = lastRecord.stalledMs;
    last["heap"] = lastRecord.freeHeap;
    last["min_heap"] = lastRecord.minHeap;
    last["wifi_disconnects"] = lastRecord.disconnects;
    last["wifi_last_reason"] = lastRecord.wifiReason;
    if (lastRecord.kind == 3) last["reset_reason"] = lastRecord.reserved;
  } else {
    json["last_incident"] = nullptr;
  }
  if (lastWifiRecord.magic == recordMagic) {
    JsonObject wifi = json.createNestedObject("last_wifi_failure");
    wifi["version"] = lastWifiRecord.version;
    wifi["uptime_ms"] = lastWifiRecord.uptimeMs;
    wifi["heap"] = lastWifiRecord.freeHeap;
    wifi["min_heap"] = lastWifiRecord.minHeap;
    wifi["wifi_disconnects"] = lastWifiRecord.disconnects;
    wifi["wifi_last_reason"] = lastWifiRecord.wifiReason;
  } else {
    json["last_wifi_failure"] = nullptr;
  }
  String output;
  if (lastOTARecord.magic == otaRecordMagic) {
    JsonObject ota = json.createNestedObject("last_ota");
    ota["version"] = lastOTARecord.version;
    ota["transport"] = lastOTARecord.transport == 1 ? "web_file" : lastOTARecord.transport == 2 ? "url" : "network";
    ota["stage"] = lastOTARecord.stage;
    ota["last_stage"] = lastOTARecord.lastStage;
    ota["accepted_bytes"] = lastOTARecord.accepted;
    ota["expected_bytes"] = lastOTARecord.expected;
    ota["error"] = lastOTARecord.error;
    ota["detail"] = lastOTARecord.detail;
    ota["uptime_ms"] = lastOTARecord.uptimeMs;
  } else {
    json["last_ota"] = nullptr;
  }
  serializeJson(json, output);
  return output;
}
#endif
