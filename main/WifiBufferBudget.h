#pragma once

// A gateway prioritizes bounded burst memory over bulk Wi-Fi throughput.
// Operate on the caller's SDK defaults; never replace its function pointers,
// security settings, static TX buffers or initialization magic.
template <typename Config>
void applyWifiBufferBudget(Config& config) {
  // Match IDF 4.4.4's minimum non-PSRAM rank: four static RX and eight
  // dynamic RX/TX buffers. The earlier two-buffer experiment stalled bulk TCP
  // even on a cold no-BLE boot with more than 74 KiB of usable heap. Heap
  // availability alone cannot replace the driver's packet-slot capacity.
  config.static_rx_buf_num = 4;
  config.ampdu_rx_enable = 0;
  config.ampdu_tx_enable = 0;
  constexpr int rxBudget = 8;
  constexpr int txBudget = 8;
  constexpr int baBudget = 2;
  config.dynamic_rx_buf_num = rxBudget;
  if (config.dynamic_rx_buf_num < config.static_rx_buf_num)
    config.dynamic_rx_buf_num = config.static_rx_buf_num;
  if (config.tx_buf_type == 1)
    config.dynamic_tx_buf_num = txBudget;
  if (config.rx_ba_win > baBudget) config.rx_ba_win = baBudget;
  if (config.rx_ba_win > config.dynamic_rx_buf_num)
    config.rx_ba_win = config.dynamic_rx_buf_num;
  if (config.static_rx_buf_num > 0 && config.rx_ba_win > config.static_rx_buf_num * 2)
    config.rx_ba_win = config.static_rx_buf_num * 2;
}
