#include "../../main/WifiBufferBudget.h"
#include <assert.h>
#include <stdio.h>
struct Config {
  int dynamic_rx_buf_num, dynamic_tx_buf_num, static_rx_buf_num, static_tx_buf_num;
  int tx_buf_type, rx_ba_win, magic, security;
  int ampdu_rx_enable, ampdu_tx_enable;
};
int main() {
  Config config = {32, 32, 4, 0, 1, 6, 123, 456, 1, 1};
  applyWifiBufferBudget(config);
  assert(config.dynamic_rx_buf_num == 2 && config.dynamic_tx_buf_num == 2);
  assert(config.rx_ba_win == 2 && config.static_rx_buf_num == 2);
  assert(config.ampdu_rx_enable == 0 && config.ampdu_tx_enable == 0);
  assert(config.static_tx_buf_num == 0 && config.magic == 123 && config.security == 456);
  Config smaller = {2, 2, 2, 0, 1, 6, 123, 456, 0, 0};
  applyWifiBufferBudget(smaller);
  assert(smaller.dynamic_rx_buf_num == 2 && smaller.dynamic_tx_buf_num == 2);
  assert(smaller.rx_ba_win == 2);
  assert(smaller.static_rx_buf_num == 2 && smaller.ampdu_rx_enable == 0);
  Config unlimited = {0, 0, 4, 0, 1, 6, 123, 456, 1, 1};
  applyWifiBufferBudget(unlimited);
  assert(unlimited.dynamic_rx_buf_num == 2 && unlimited.dynamic_tx_buf_num == 2);
  Config staticTX = {32, 32, 8, 8, 0, 6, 123, 456, 1, 1};
  applyWifiBufferBudget(staticTX);
  assert(staticTX.static_tx_buf_num == 8 && staticTX.dynamic_tx_buf_num == 32);
  assert(staticTX.tx_buf_type == 0 && staticTX.static_rx_buf_num == 2);
  assert(staticTX.dynamic_rx_buf_num == 2);
  Config tiny = {1, 1, 2, 0, 1, 2, 123, 456, 0, 0};
  applyWifiBufferBudget(tiny);
  assert(tiny.dynamic_rx_buf_num == 2 && tiny.dynamic_tx_buf_num == 1);
  assert(tiny.rx_ba_win == 2); // RX must cover all static RX buffers.
  puts("PASS: bounded RX/TX/DMA, disabled aggregation, smaller caps and preserved security/static TX");
}
