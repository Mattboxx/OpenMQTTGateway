/*
  Lightweight BLE advertisement presence tracker.

  This module intentionally does not use the full OpenMQTTGateway Bluetooth
  decoder/connector pipeline. It observes advertisements only and exposes a
  small set of explicitly configured fixed MAC addresses.
*/
#ifndef config_BLETracker_h
#define config_BLETracker_h

#ifndef config_BT_h
#  ifndef BLE_TRACKER_MAX
#    define BLE_TRACKER_MAX 4
#  endif
#ifndef BLE_TRACKER_START_DELAY_MS
#  define BLE_TRACKER_START_DELAY_MS 20000UL
#endif
#ifndef BLE_TRACKER_SCAN_INTERVAL_MS
#  define BLE_TRACKER_SCAN_INTERVAL_MS 300U
#endif
#ifndef BLE_TRACKER_SCAN_WINDOW_MS
#  define BLE_TRACKER_SCAN_WINDOW_MS 30U
#endif
#ifndef BLE_TRACKER_MIN_HEAP_TO_START
#  define BLE_TRACKER_MIN_HEAP_TO_START 50000U
#endif
#ifndef BLE_TRACKER_MIN_HEAP_AFTER_START
#  define BLE_TRACKER_MIN_HEAP_AFTER_START 28000U
#endif

struct BLETrackerConfig_s {
  bool enabled;
  char mac[18];
  char name[33];
  uint32_t timeoutSeconds;
  int minRssi;
  uint32_t lastSeen;
  uint32_t lastPublish;
  int lastRssi;
  bool present;
  uint32_t rawMatches;
  uint32_t rssiRejected;
  int lastRawRssi;
};

extern BLETrackerConfig_s BLETrackerConfig[BLE_TRACKER_MAX];
extern void launchBTDiscovery(bool overrideDiscovery);
extern bool configureBLETracker(uint8_t slot, bool enabled, const char* mac, const char* name,
                                uint32_t timeoutSeconds, int minRssi);
extern BLETrackerConfig_s getBLETrackerConfig(uint8_t slot);
extern void saveBLETrackerConfig();
extern bool isValidBLETrackerMac(const char* mac);
extern String getBLETrackerCandidatesHtml();
#endif

extern void setupBLETracker();
extern void loopBLETracker();
extern void stopBLETracker(bool deinitRadio);
extern bool isBLETrackerStarting();
extern bool pauseBLETrackerScanForWeb();
extern void resumeBLETrackerScanAfterWeb();
extern String stateBLETrackerMeasures();

#endif
