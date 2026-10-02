#pragma once
struct WifiMemoryBudgetSnapshot {
  bool initialized;
  int rx;
  int tx;
  int ba;
  int staticRx;
  bool ampduRx;
  bool ampduTx;
};
#if defined(ESP32) && defined(OMG_WIFI_MEMORY_BUDGET)
WifiMemoryBudgetSnapshot wifiMemoryBudgetSnapshot();
#else
inline WifiMemoryBudgetSnapshot wifiMemoryBudgetSnapshot() { return {}; }
#endif
