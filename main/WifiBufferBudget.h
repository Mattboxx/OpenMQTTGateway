#pragma once

// A gateway prioritizes bounded burst memory over bulk Wi-Fi throughput.
// Operate on the caller's SDK defaults; never replace its function pointers,
// security settings, static TX buffers or initialization magic.
template <typename Config>
void applyWifiBufferBudget(Config& config) {
  // The pinned IDF allows two static RX buffers, but recommends disabling
  // AMPDU below six. This low-throughput gateway trades packet aggregation
  // for roughly 3.2 KB less permanent DMA allocation versus Arduino's four.
  if (config.static_rx_buf_num > 2) config.static_rx_buf_num = 2;
  config.ampdu_rx_enable = 0;
  config.ampdu_tx_enable = 0;
  constexpr int rxBudget = 2;
  constexpr int txBudget = 2;
  constexpr int baBudget = 2;
  if (config.dynamic_rx_buf_num <= 0 || config.dynamic_rx_buf_num > rxBudget)
    config.dynamic_rx_buf_num = rxBudget;
  if (config.dynamic_rx_buf_num < config.static_rx_buf_num)
    config.dynamic_rx_buf_num = config.static_rx_buf_num;
  if (config.tx_buf_type == 1 &&
      (config.dynamic_tx_buf_num <= 0 || config.dynamic_tx_buf_num > txBudget))
    config.dynamic_tx_buf_num = txBudget;
  if (config.rx_ba_win > baBudget) config.rx_ba_win = baBudget;
  if (config.rx_ba_win > config.dynamic_rx_buf_num)
    config.rx_ba_win = config.dynamic_rx_buf_num;
  if (config.static_rx_buf_num > 0 && config.rx_ba_win > config.static_rx_buf_num * 2)
    config.rx_ba_win = config.static_rx_buf_num * 2;
}
