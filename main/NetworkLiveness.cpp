#include "NetworkLiveness.h"

#if defined(ESP32) && defined(WIFI_GATEWAY_LIVENESS)
#include "RuntimeDiagnostics.h"
#include <ArduinoLog.h>
#include <WiFi.h>
#include <ping/ping_sock.h>

#ifndef WIFI_GATEWAY_LIVENESS_INTERVAL_MS
#  define WIFI_GATEWAY_LIVENESS_INTERVAL_MS 60000UL
#endif
#ifndef WIFI_GATEWAY_LIVENESS_TIMEOUT_MS
#  define WIFI_GATEWAY_LIVENESS_TIMEOUT_MS 1500UL
#endif
#ifndef WIFI_GATEWAY_LIVENESS_FAILURES
#  define WIFI_GATEWAY_LIVENESS_FAILURES 3
#endif
#ifndef WIFI_GATEWAY_LIVENESS_MIN_HEAP
#  define WIFI_GATEWAY_LIVENESS_MIN_HEAP 28000U
#endif

namespace {
portMUX_TYPE livenessMux = portMUX_INITIALIZER_UNLOCKED;
esp_ping_handle_t pingHandle;
IPAddress pingTarget;
uint32_t replies;
uint32_t timeouts;
uint32_t recoveries;
uint32_t lastResultMs;
uint32_t nextStartMs;
uint32_t lastRecoveryMs;
uint8_t consecutiveFailures;
bool started;
bool wifiWasAvailable;

void pingSuccess(esp_ping_handle_t handle, void*) {
  portENTER_CRITICAL(&livenessMux);
  if (handle == pingHandle) {
    ++replies;
    consecutiveFailures = 0;
    lastResultMs = millis();
  }
  portEXIT_CRITICAL(&livenessMux);
}

void pingTimeout(esp_ping_handle_t handle, void*) {
  portENTER_CRITICAL(&livenessMux);
  if (handle == pingHandle) {
    ++timeouts;
    if (consecutiveFailures < UINT8_MAX) ++consecutiveFailures;
    lastResultMs = millis();
  }
  portEXIT_CRITICAL(&livenessMux);
}

void stopSession() {
  portENTER_CRITICAL(&livenessMux);
  esp_ping_handle_t oldHandle = pingHandle;
  pingHandle = nullptr;
  started = false;
  portEXIT_CRITICAL(&livenessMux);
  if (oldHandle) {
    esp_ping_stop(oldHandle);
    // Deletion is asynchronous; the IDF ping task releases its socket, packet
    // and stack after observing the cleared initialization flag.
    esp_ping_delete_session(oldHandle);
  }
  // The IDF deletes the ping task asynchronously.  Wait one normal interval
  // before allocating a replacement so a gateway/DHCP change cannot briefly
  // leave two ping task stacks alive on this memory-constrained build.
  nextStartMs = millis() + WIFI_GATEWAY_LIVENESS_INTERVAL_MS;
}

bool startSession(const IPAddress& gateway) {
  if (!gateway || ESP.getFreeHeap() < WIFI_GATEWAY_LIVENESS_MIN_HEAP) return false;

  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.count = ESP_PING_COUNT_INFINITE;
  config.interval_ms = WIFI_GATEWAY_LIVENESS_INTERVAL_MS;
  config.timeout_ms = WIFI_GATEWAY_LIVENESS_TIMEOUT_MS;
  config.data_size = 16;
  config.task_stack_size = 2048;
  config.task_prio = 1;
  IP_ADDR4(&config.target_addr, gateway[0], gateway[1], gateway[2], gateway[3]);

  esp_ping_callbacks_t callbacks = {};
  callbacks.on_ping_success = pingSuccess;
  callbacks.on_ping_timeout = pingTimeout;

  esp_ping_handle_t newHandle = nullptr;
  const esp_err_t createResult = esp_ping_new_session(&config, &callbacks, &newHandle);
  if (createResult != ESP_OK || !newHandle) {
    Log.warning(F("[WIFI][LIVENESS] guard unavailable create_error=%d heap=%u" CR),
                createResult, ESP.getFreeHeap());
    nextStartMs = millis() + WIFI_GATEWAY_LIVENESS_INTERVAL_MS;
    return false;
  }

  pingTarget = gateway;
  portENTER_CRITICAL(&livenessMux);
  pingHandle = newHandle;
  consecutiveFailures = 0;
  lastResultMs = millis();
  portEXIT_CRITICAL(&livenessMux);
  if (esp_ping_start(newHandle) != ESP_OK) {
    stopSession();
    return false;
  }
  started = true;
  Log.notice(F("[WIFI][LIVENESS] gateway guard started target=%s interval_ms=%lu timeout_ms=%lu failures=%u heap=%u" CR),
             pingTarget.toString().c_str(), (unsigned long)WIFI_GATEWAY_LIVENESS_INTERVAL_MS,
             (unsigned long)WIFI_GATEWAY_LIVENESS_TIMEOUT_MS,
             (unsigned int)WIFI_GATEWAY_LIVENESS_FAILURES, ESP.getFreeHeap());
  return true;
}
} // namespace

void networkLivenessBegin() {
  nextStartMs = millis();
  wifiWasAvailable = WiFi.status() == WL_CONNECTED;
  if (wifiWasAvailable) startSession(WiFi.gatewayIP());
}

void networkLivenessLoop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiWasAvailable = false;
    return;
  }
  if (!wifiWasAvailable) {
    // Pings during a known association outage must not immediately trigger a
    // second recovery once the normal Wi-Fi reconnect has succeeded.
    portENTER_CRITICAL(&livenessMux);
    consecutiveFailures = 0;
    lastResultMs = millis();
    portEXIT_CRITICAL(&livenessMux);
    wifiWasAvailable = true;
  }

  const uint32_t now = millis();
  const IPAddress currentGateway = WiFi.gatewayIP();
  if (started && currentGateway != pingTarget) {
    Log.notice(F("[WIFI][LIVENESS] gateway changed old=%s new=%s" CR),
               pingTarget.toString().c_str(), currentGateway.toString().c_str());
    stopSession();
    return;
  }
  if (!started) {
    if ((int32_t)(now - nextStartMs) >= 0) startSession(currentGateway);
    return;
  }

  uint8_t failures;
  uint32_t resultAge;
  portENTER_CRITICAL(&livenessMux);
  failures = consecutiveFailures;
  // A callback can update lastResultMs after 'now' was sampled above. Sample
  // age under the same lock to avoid uint32 underflow and a false recovery.
  resultAge = millis() - lastResultMs;
  portEXIT_CRITICAL(&livenessMux);

  const uint32_t staleAfter = (WIFI_GATEWAY_LIVENESS_INTERVAL_MS * 2UL) +
                              (WIFI_GATEWAY_LIVENESS_TIMEOUT_MS * 2UL);
  const bool taskStalled = resultAge >= staleAfter;
  if (failures < WIFI_GATEWAY_LIVENESS_FAILURES && !taskStalled) return;
  // A recovery attempt is allowed at most once per failure window. Successful
  // ping replies clear the callback counter; this cooldown also protects
  // against a router that temporarily rate-limits ICMP.
  if (lastRecoveryMs && now - lastRecoveryMs < staleAfter) return;

  ++recoveries;
  lastRecoveryMs = now;
  portENTER_CRITICAL(&livenessMux);
  consecutiveFailures = 0;
  lastResultMs = now;
  portEXIT_CRITICAL(&livenessMux);
  runtimeRememberWiFiFailure();
  Log.error(F("[WIFI][LIVENESS] gateway unreachable target=%s consecutive=%u ping_task_stalled=%T recovery=%lu; forcing reassociation" CR),
            pingTarget.toString().c_str(), failures, taskStalled, (unsigned long)recoveries);
  WiFi.disconnect(false, false);
}

NetworkLivenessSnapshot networkLivenessSnapshot() {
  NetworkLivenessSnapshot result = {};
  portENTER_CRITICAL(&livenessMux);
  result.started = started;
  result.target = (uint32_t)pingTarget;
  result.replies = replies;
  result.timeouts = timeouts;
  result.recoveries = recoveries;
  result.consecutiveFailures = consecutiveFailures;
  result.lastResultAgeMs = millis() - lastResultMs;
  portEXIT_CRITICAL(&livenessMux);
  return result;
}
#endif
