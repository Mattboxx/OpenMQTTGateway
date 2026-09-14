#include "RuntimeDiagnostics.h"
#include "RuntimeHeartbeat.h"
#if defined(ESP32) && defined(OMG_RUNTIME_DIAGNOSTICS)
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_core_dump.h>
#include <esp_system.h>
#include <esp_private/system_internal.h>
#include <stdlib.h>

namespace {
constexpr uint32_t recordMagic = 0x4F4D4431;
constexpr uint32_t timeoutMs = 120000;
struct Record {
  uint32_t magic;
  uint32_t uptimeMs;
  uint32_t stalledMs;
  uint32_t freeHeap;
  uint32_t minHeap;
  uint32_t disconnects;
  uint8_t wifiReason;
  uint8_t phase;
  uint8_t kind; // 1=stalled loop, 2=failed WiFi recovery window
  uint8_t reserved;
  char version[48];
};
RTC_NOINIT_ATTR Record pendingRecord;
Record lastRecord = {};
portMUX_TYPE progressMux = portMUX_INITIALIZER_UNLOCKED;
RuntimeHeartbeat heartbeat;
uint32_t disconnects;
uint8_t wifiReason;
uint32_t sampledFreeHeap;
uint32_t sampledMinHeap;
RuntimePhase phase = RuntimePhase::Loop;
bool guardStarted;
bool preserveCrash;

const char* phaseName(uint8_t value) {
  static const char* names[] = {"loop", "wifi", "web", "mqtt", "sensors", "queue", "ota", "restart"};
  return value < 8 ? names[value] : "unknown";
}

void saveRecord(const Record& record) {
  Preferences preferences;
  if (preferences.begin("omgdiag", false)) {
    preferences.putBytes("last", &record, sizeof(record));
    preferences.end();
  }
  lastRecord = record;
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
  saveRecord(snapshot(2));
}

void runtimeDiagnosticsBegin() {
  Preferences preferences;
  if (preferences.begin("omgdiag", true)) {
    if (preferences.getBytesLength("last") == sizeof(lastRecord))
      preferences.getBytes("last", &lastRecord, sizeof(lastRecord));
    preferences.end();
  }
  lastRecord.version[sizeof(lastRecord.version) - 1] = '\0';
  if (esp_reset_reason() != ESP_RST_POWERON && pendingRecord.magic == recordMagic)
    saveRecord(pendingRecord);
  pendingRecord.magic = 0;
  size_t address = 0, size = 0;
  preserveCrash = esp_core_dump_image_get(&address, &size) == ESP_OK && size;
  runtimeProgress(RuntimePhase::Loop);
  guardStarted = xTaskCreatePinnedToCore(guardTask, "omgRuntime", 3072, nullptr, 2, nullptr, 0) == pdPASS;
  // Each completed flash write proves progress even when OTA occupies loop().
  Update.onProgress([](size_t, size_t) { runtimeProgress(RuntimePhase::OTA); });
}

String runtimeDiagnosticsJSON() {
  StaticJsonDocument<1024> json;
  const Record current = snapshot(0);
  json["version"] = OMG_VERSION;
  json["runtime_guard"] = guardStarted;
  json["timeout_ms"] = timeoutMs;
  json["emergency_reset"] = "no_shutdown_handlers";
  json["phase"] = phaseName(current.phase);
  json["progress_age_ms"] = current.stalledMs;
  json["wifi_disconnects"] = current.disconnects;
  json["wifi_last_reason"] = current.wifiReason;
  size_t address = 0, size = 0;
  const bool available = esp_core_dump_image_get(&address, &size) == ESP_OK && size;
  json["crash_available"] = available;
  json["crash_bytes"] = available ? size : 0;
  if (lastRecord.magic == recordMagic) {
    JsonObject last = json.createNestedObject("last_incident");
    last["kind"] = lastRecord.kind == 1 ? "loop_stalled" : "wifi_recovery_failed";
    last["version"] = lastRecord.version;
    last["phase"] = phaseName(lastRecord.phase);
    last["uptime_ms"] = lastRecord.uptimeMs;
    last["stalled_ms"] = lastRecord.stalledMs;
    last["heap"] = lastRecord.freeHeap;
    last["min_heap"] = lastRecord.minHeap;
    last["wifi_disconnects"] = lastRecord.disconnects;
    last["wifi_last_reason"] = lastRecord.wifiReason;
  } else {
    json["last_incident"] = nullptr;
  }
  String output;
  serializeJson(json, output);
  return output;
}
#endif
