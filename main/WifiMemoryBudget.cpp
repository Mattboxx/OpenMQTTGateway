#include "WifiMemoryBudget.h"
#if defined(ESP32) && defined(OMG_WIFI_MEMORY_BUDGET)
#include "WifiBufferBudget.h"
#include <esp_wifi.h>

namespace {
WifiMemoryBudgetSnapshot budget;
}
extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t* config);
extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t* config) {
  if (!config) return __real_esp_wifi_init(config);
  wifi_init_config_t bounded = *config;
  applyWifiBufferBudget(bounded);
  const esp_err_t result = __real_esp_wifi_init(&bounded);
  // An SDK rejecting the reduced limits must not strand a remote gateway.
  // Invalid configuration is rejected before initialization; retry the exact
  // core-supplied defaults, reporting the budget as inactive in that case.
  if (result == ESP_ERR_INVALID_ARG) {
    budget = {false, config->dynamic_rx_buf_num,
              config->tx_buf_type == 1 ? config->dynamic_tx_buf_num : config->static_tx_buf_num,
              config->rx_ba_win, config->static_rx_buf_num,
              config->ampdu_rx_enable != 0, config->ampdu_tx_enable != 0};
    return __real_esp_wifi_init(config);
  }
  // WiFi initialization and /diag both run on the main task. The driver's
  // documented API consumes this temporary structure before returning.
  budget = {result == ESP_OK, bounded.dynamic_rx_buf_num,
            bounded.tx_buf_type == 1 ? bounded.dynamic_tx_buf_num : bounded.static_tx_buf_num,
            bounded.rx_ba_win, bounded.static_rx_buf_num,
            bounded.ampdu_rx_enable != 0, bounded.ampdu_tx_enable != 0};
  return result;
}
WifiMemoryBudgetSnapshot wifiMemoryBudgetSnapshot() { return budget; }
#endif
