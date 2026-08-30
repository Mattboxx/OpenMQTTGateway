/*
  OpenMQTTGateway lightweight BLE advertisement presence tracker.

  Controller-only design: raw ESP-IDF VHCI receives legacy BLE advertising
  reports. There is no NimBLE/Bluedroid host, GATT client, connection support,
  decoder pipeline, application worker task, or unbounded device list.
*/
#include "User_config.h"

#ifdef ZgatewayBLETracker

#  include <Preferences.h>
#  include <esp_bt.h>
#  include <esp_err.h>
#  include <esp_system.h>
#  include <esp_task_wdt.h>

// initArduino() calls a weak btInUse() hook before setup(). Its default return
// value is false, which permanently releases all controller memory. This strong
// BLE-only implementation keeps that memory available without pulling in the
// Arduino dual-mode Bluetooth wrapper or either host stack.
extern "C" bool btInUse() {
  return true;
}

static constexpr uint32_t BLE_TRACKER_INIT_GUARD_MAGIC = 0x424C4547UL;
static constexpr uint16_t HCI_OPCODE_RESET = 0x0C03;
static constexpr uint16_t HCI_OPCODE_SET_EVENT_MASK = 0x0C01;
static constexpr uint16_t HCI_OPCODE_LE_SET_EVENT_MASK = 0x2001;
static constexpr uint16_t HCI_OPCODE_LE_SET_SCAN_PARAMETERS = 0x200B;
static constexpr uint16_t HCI_OPCODE_LE_SET_SCAN_ENABLE = 0x200C;
static constexpr uint8_t HCI_PACKET_COMMAND = 0x01;
static constexpr uint8_t HCI_PACKET_EVENT = 0x04;
static constexpr uint8_t HCI_EVENT_COMMAND_COMPLETE = 0x0E;
static constexpr uint8_t HCI_EVENT_LE_META = 0x3E;
static constexpr uint8_t HCI_SUBEVENT_LE_ADVERTISING_REPORT = 0x02;
static constexpr uint8_t BLE_TRACKER_REPORT_QUEUE_SIZE = 12;

RTC_NOINIT_ATTR uint32_t bleTrackerInitGuard;

BLETrackerConfig_s BLETrackerConfig[BLE_TRACKER_MAX];
static SemaphoreHandle_t bleTrackerMutex;
static QueueHandle_t bleTrackerReportQueue;
static bool bleTrackerStarted;
static bool bleTrackerScanning;
static bool bleTrackerRuntimeBlocked;
static volatile bool bleTrackerStarting;
static TaskHandle_t bleTrackerStartTaskHandle;
static uint32_t bleTrackerStartTaskStartedAt;
static bool bleTrackerDiscoveryDirty = true;
static bool bleTrackerConfigWasStored;
static uint32_t bleTrackerLastStartAttempt;
static uint32_t bleTrackerAdvertisements;
static uint32_t bleTrackerMatchedAdvertisements;
static volatile uint32_t bleTrackerDroppedReports;
static bool bleTrackerPendingPublish[BLE_TRACKER_MAX];
static uint8_t bleTrackerPendingReason[BLE_TRACKER_MAX];
static bool bleTrackerInitialStatePending[BLE_TRACKER_MAX];
static uint32_t bleTrackerInitialStateSince[BLE_TRACKER_MAX];
static bool bleTrackerWebPauseRequested;
static uint32_t bleTrackerLastResumeAttempt;

enum BLETrackerPublishReason : uint8_t {
  BLE_TRACKER_REASON_DETECTED = 1,
  BLE_TRACKER_REASON_REFRESH = 2
};

enum BLETrackerHCIState : uint8_t {
  BLE_HCI_IDLE,
  BLE_HCI_SEND_RESET,
  BLE_HCI_WAIT_RESET,
  BLE_HCI_SEND_EVENT_MASK,
  BLE_HCI_WAIT_EVENT_MASK,
  BLE_HCI_SEND_LE_EVENT_MASK,
  BLE_HCI_WAIT_LE_EVENT_MASK,
  BLE_HCI_SEND_SCAN_PARAMETERS,
  BLE_HCI_WAIT_SCAN_PARAMETERS,
  BLE_HCI_SEND_SCAN_ENABLE,
  BLE_HCI_WAIT_SCAN_ENABLE,
  BLE_HCI_READY,
  BLE_HCI_FAILED
};

static BLETrackerHCIState bleTrackerHciState = BLE_HCI_IDLE;
static volatile bool bleTrackerHciSendAvailable;
static volatile bool bleTrackerCommandComplete;
static volatile uint16_t bleTrackerCompletedOpcode;
static volatile uint8_t bleTrackerCompletedStatus;
static portMUX_TYPE bleTrackerHciMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t bleTrackerHciCommandSentAt;

struct BLETrackerCandidate_s {
  char mac[18];
  char name[25];
  int rssi;
  uint32_t lastSeen;
};

struct BLETrackerRawReport_s {
  char mac[18];
  char name[25];
  int8_t rssi;
};

static constexpr uint8_t BLE_TRACKER_CANDIDATE_MAX = 10;
static BLETrackerCandidate_s bleTrackerCandidates[BLE_TRACKER_CANDIDATE_MAX];

bool isValidBLETrackerMac(const char* mac) {
  if (!mac || strlen(mac) != 17) return false;
  for (uint8_t i = 0; i < 17; i++) {
    if ((i + 1) % 3 == 0) {
      if (mac[i] != ':') return false;
    } else if (!isxdigit(static_cast<unsigned char>(mac[i]))) {
      return false;
    }
  }
  return true;
}

static void normalizeBLETrackerMac(const char* source, char* destination) {
  for (uint8_t i = 0; i < 17; i++) destination[i] = toupper(static_cast<unsigned char>(source[i]));
  destination[17] = '\0';
}

static void initBLETrackerConfig() {
  memset(BLETrackerConfig, 0, sizeof(BLETrackerConfig));
  memset(bleTrackerCandidates, 0, sizeof(bleTrackerCandidates));
  memset(bleTrackerPendingPublish, 0, sizeof(bleTrackerPendingPublish));
  memset(bleTrackerPendingReason, 0, sizeof(bleTrackerPendingReason));
  memset(bleTrackerInitialStatePending, 0, sizeof(bleTrackerInitialStatePending));
  memset(bleTrackerInitialStateSince, 0, sizeof(bleTrackerInitialStateSince));
  for (uint8_t slot = 0; slot < BLE_TRACKER_MAX; slot++) {
    BLETrackerConfig[slot].timeoutSeconds = 120;
    BLETrackerConfig[slot].minRssi = -90;
    BLETrackerConfig[slot].lastRssi = -127;
    BLETrackerConfig[slot].lastRawRssi = -127;
  }
}

bool configureBLETracker(uint8_t slot, bool enabled, const char* mac, const char* name,
                         uint32_t timeoutSeconds, int minRssi) {
  if (slot >= BLE_TRACKER_MAX || (enabled && !isValidBLETrackerMac(mac))) return false;
  timeoutSeconds = constrain(timeoutSeconds, 5UL, 86400UL);
  minRssi = constrain(minRssi, -100, -20);

  if (bleTrackerMutex && xSemaphoreTake(bleTrackerMutex, pdMS_TO_TICKS(2000)) == pdFALSE) {
    Log.error(F("[BLE][ADV] configuration mutex timeout slot=%u" CR), slot + 1);
    return false;
  }

  BLETrackerConfig_s& tracker = BLETrackerConfig[slot];
  bool wasEnabled = tracker.enabled;
  char normalizedMac[18] = {0};
  if (isValidBLETrackerMac(mac)) normalizeBLETrackerMac(mac, normalizedMac);
  bool identityChanged = strcmp(tracker.mac, normalizedMac) != 0;
  tracker.enabled = enabled;
  strlcpy(tracker.mac, normalizedMac, sizeof(tracker.mac));
  if (name && name[0]) {
    strlcpy(tracker.name, name, sizeof(tracker.name));
  } else {
    snprintf(tracker.name, sizeof(tracker.name), "Dispositivo BLE %u", slot + 1);
  }
  tracker.timeoutSeconds = timeoutSeconds;
  tracker.minRssi = minRssi;
  if (identityChanged || !enabled) {
    tracker.lastSeen = 0;
    tracker.lastPublish = 0;
    tracker.lastRssi = -127;
    tracker.rawMatches = 0;
    tracker.rssiRejected = 0;
    tracker.lastRawRssi = -127;
    tracker.present = false;
    bleTrackerPendingPublish[slot] = false;
  }
  if (enabled && (identityChanged || !wasEnabled)) {
    // Keep Home Assistant's retained state during a reboot or a newly armed
    // slot. The first genuine advertisement wins; otherwise publish away only
    // after one complete timeout measured from the moment scanning is ready.
    bleTrackerInitialStatePending[slot] = true;
    bleTrackerInitialStateSince[slot] = bleTrackerScanning ? millis() : 0;
  } else if (!enabled) {
    bleTrackerInitialStatePending[slot] = false;
    bleTrackerInitialStateSince[slot] = 0;
  }
  bleTrackerDiscoveryDirty = true;
  if (bleTrackerMutex) xSemaphoreGive(bleTrackerMutex);

  Log.verbose(F("[BLE][ADV] configured slot=%u enabled=%T name=%s mac=%s timeout_s=%u min_rssi=%d" CR),
             slot + 1, enabled, tracker.name, tracker.mac, tracker.timeoutSeconds, tracker.minRssi);
  return true;
}

BLETrackerConfig_s getBLETrackerConfig(uint8_t slot) {
  BLETrackerConfig_s copy = {};
  if (slot >= BLE_TRACKER_MAX) return copy;
  if (bleTrackerMutex && xSemaphoreTake(bleTrackerMutex, pdMS_TO_TICKS(2000)) == pdFALSE) return copy;
  copy = BLETrackerConfig[slot];
  if (bleTrackerMutex) xSemaphoreGive(bleTrackerMutex);
  return copy;
}

void saveBLETrackerConfig() {
  DynamicJsonDocument jsonBuffer(1536);
  JsonArray trackers = jsonBuffer.createNestedArray("trackers");
  for (uint8_t slot = 0; slot < BLE_TRACKER_MAX; slot++) {
    BLETrackerConfig_s tracker = getBLETrackerConfig(slot);
    JsonObject item = trackers.createNestedObject();
    item["enabled"] = tracker.enabled;
    item["mac"] = tracker.mac;
    item["name"] = tracker.name;
    item["timeout"] = tracker.timeoutSeconds;
    item["minrssi"] = tracker.minRssi;
  }
  String conf;
  serializeJson(jsonBuffer, conf);
  preferences.begin(Gateway_Short_Name, false);
  size_t written = preferences.putString("BLETrackers", conf);
  preferences.end();
  bleTrackerConfigWasStored = written > 0;
  Log.verbose(F("[BLE][ADV] configuration saved slots=%u bytes=%u result=%u" CR),
             BLE_TRACKER_MAX, conf.length(), written);
}

static void loadBLETrackerConfig() {
  preferences.begin(Gateway_Short_Name, true);
  String conf = preferences.isKey("BLETrackers") ? preferences.getString("BLETrackers", "") : "";
  preferences.end();
  if (!conf.length()) {
    Log.verbose(F("[BLE][ADV] no saved configuration; all slots disabled" CR));
    return;
  }
  bleTrackerConfigWasStored = true;
  DynamicJsonDocument jsonBuffer(1536);
  DeserializationError error = deserializeJson(jsonBuffer, conf);
  if (error) {
    Log.error(F("[BLE][ADV] invalid saved configuration error=%s bytes=%u" CR), error.c_str(), conf.length());
    return;
  }
  JsonArray trackers = jsonBuffer["trackers"].as<JsonArray>();
  uint8_t slot = 0;
  for (JsonObject item : trackers) {
    if (slot >= BLE_TRACKER_MAX) break;
    configureBLETracker(slot, item["enabled"] | false, item["mac"] | "", item["name"] | "",
                        item["timeout"] | 120UL, item["minrssi"] | -90);
    slot++;
  }
  Log.verbose(F("[BLE][ADV] configuration loaded slots=%u" CR), slot);
}

static void rememberBLETrackerCandidate(const char* mac, const char* name, int rssi, uint32_t now) {
  int selected = -1;
  int oldest = 0;
  for (uint8_t i = 0; i < BLE_TRACKER_CANDIDATE_MAX; i++) {
    if (strcasecmp(bleTrackerCandidates[i].mac, mac) == 0) selected = i;
    if (!bleTrackerCandidates[i].mac[0]) {
      oldest = i;
      if (selected < 0) selected = i;
      break;
    }
    if (bleTrackerCandidates[i].lastSeen < bleTrackerCandidates[oldest].lastSeen) oldest = i;
  }
  if (selected < 0) selected = oldest;
  bool replacing = strcasecmp(bleTrackerCandidates[selected].mac, mac) != 0;
  if (replacing) bleTrackerCandidates[selected].name[0] = '\0';
  normalizeBLETrackerMac(mac, bleTrackerCandidates[selected].mac);
  if (name && name[0]) strlcpy(bleTrackerCandidates[selected].name, name, sizeof(bleTrackerCandidates[selected].name));
  bleTrackerCandidates[selected].rssi = rssi;
  bleTrackerCandidates[selected].lastSeen = now;
}

String getBLETrackerCandidatesHtml() {
  String html;
  if (!bleTrackerMutex || xSemaphoreTake(bleTrackerMutex, pdMS_TO_TICKS(2000)) == pdFALSE) return html;
  for (uint8_t i = 0; i < BLE_TRACKER_CANDIDATE_MAX; i++) {
    if (!bleTrackerCandidates[i].mac[0]) continue;
    String label = bleTrackerCandidates[i].name[0] ? String(bleTrackerCandidates[i].name) : String("Senza nome");
    label.replace("&", "&amp;");
    label.replace("<", "&lt;");
    label.replace(">", "&gt;");
    label.replace("\"", "&quot;");
    html += "<option value='" + String(bleTrackerCandidates[i].mac) + "'>" + label + " (" +
            String(bleTrackerCandidates[i].rssi) + " dBm)</option>";
  }
  xSemaphoreGive(bleTrackerMutex);
  return html;
}

static void processBLEAdvertisement(const char* mac, const char* name, int rssi) {
  uint32_t now = millis();
  bleTrackerAdvertisements++;
  if (!bleTrackerMutex || xSemaphoreTake(bleTrackerMutex, pdMS_TO_TICKS(20)) == pdFALSE) return;
  rememberBLETrackerCandidate(mac, name, rssi, now);
  for (uint8_t slot = 0; slot < BLE_TRACKER_MAX; slot++) {
    BLETrackerConfig_s& tracker = BLETrackerConfig[slot];
    if (!tracker.enabled || strcasecmp(tracker.mac, mac) != 0) continue;
    tracker.rawMatches++;
    tracker.lastRawRssi = rssi;
    if (rssi < tracker.minRssi) {
      tracker.rssiRejected++;
      continue;
    }
    bool arrived = !tracker.present;
    tracker.present = true;
    tracker.lastSeen = now;
    tracker.lastRssi = rssi;
    bleTrackerInitialStatePending[slot] = false;
    bleTrackerInitialStateSince[slot] = 0;
    bleTrackerMatchedAdvertisements++;
    if (arrived || now - tracker.lastPublish >= 30000UL) {
      tracker.lastPublish = now;
      bleTrackerPendingPublish[slot] = true;
      bleTrackerPendingReason[slot] = arrived ? BLE_TRACKER_REASON_DETECTED : BLE_TRACKER_REASON_REFRESH;
    }
  }
  xSemaphoreGive(bleTrackerMutex);
}

static void formatBLEAddress(const uint8_t* littleEndianAddress, char* output) {
  static const char hex[] = "0123456789ABCDEF";
  uint8_t pos = 0;
  for (int8_t i = 5; i >= 0; i--) {
    uint8_t value = littleEndianAddress[i];
    output[pos++] = hex[value >> 4];
    output[pos++] = hex[value & 0x0F];
    if (i) output[pos++] = ':';
  }
  output[pos] = '\0';
}

static void parseBLEName(const uint8_t* payload, uint8_t payloadLength, char* output, size_t outputSize) {
  output[0] = '\0';
  uint8_t offset = 0;
  while (offset < payloadLength) {
    uint8_t fieldLength = payload[offset];
    if (!fieldLength || offset + fieldLength >= payloadLength) break;
    uint8_t type = payload[offset + 1];
    if ((type == 0x08 || type == 0x09) && fieldLength > 1) {
      size_t nameLength = min(static_cast<size_t>(fieldLength - 1), outputSize - 1);
      memcpy(output, payload + offset + 2, nameLength);
      output[nameLength] = '\0';
      return;
    }
    offset += fieldLength + 1;
  }
}

static void queueHCIAdvertisingReports(const uint8_t* data, uint16_t len) {
  if (len < 5 || data[3] != HCI_SUBEVENT_LE_ADVERTISING_REPORT || !bleTrackerReportQueue) return;
  uint8_t reportCount = data[4];
  uint16_t offset = 5;
  for (uint8_t reportIndex = 0; reportIndex < reportCount; reportIndex++) {
    // event type + address type + address + data length + RSSI
    if (offset + 10 > len) return;
    uint8_t payloadLength = data[offset + 8];
    uint16_t nextOffset = offset + 10U + payloadLength;
    if (nextOffset > len) return;
    BLETrackerRawReport_s report = {};
    formatBLEAddress(data + offset + 2, report.mac);
    parseBLEName(data + offset + 9, payloadLength, report.name, sizeof(report.name));
    report.rssi = static_cast<int8_t>(data[offset + 9 + payloadLength]);
    if (xQueueSend(bleTrackerReportQueue, &report, 0) != pdTRUE) bleTrackerDroppedReports++;
    offset = nextOffset;
  }
}

static void bleTrackerVHCIHostSendAvailable() {
  bleTrackerHciSendAvailable = true;
}

static int bleTrackerVHCIHostReceive(uint8_t* data, uint16_t len) {
  if (!data || len < 3 || data[0] != HCI_PACKET_EVENT) return 0;
  if (data[1] == HCI_EVENT_COMMAND_COMPLETE && len >= 7) {
    portENTER_CRITICAL(&bleTrackerHciMux);
    bleTrackerCompletedOpcode = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
    bleTrackerCompletedStatus = data[6];
    bleTrackerCommandComplete = true;
    portEXIT_CRITICAL(&bleTrackerHciMux);
  } else if (data[1] == HCI_EVENT_LE_META) {
    queueHCIAdvertisingReports(data, len);
  }
  return 0;
}

static esp_vhci_host_callback_t bleTrackerVHCICallbacks = {
  bleTrackerVHCIHostSendAvailable,
  bleTrackerVHCIHostReceive
};

static bool sendBLETrackerHCICommand(uint16_t opcode, const uint8_t* parameters, uint8_t parameterLength) {
  if (!esp_vhci_host_check_send_available()) return false;
  uint8_t packet[16] = {HCI_PACKET_COMMAND, static_cast<uint8_t>(opcode & 0xFF),
                        static_cast<uint8_t>(opcode >> 8), parameterLength};
  if (parameterLength) memcpy(packet + 4, parameters, parameterLength);
  esp_vhci_host_send_packet(packet, parameterLength + 4);
  bleTrackerHciSendAvailable = false;
  bleTrackerHciCommandSentAt = millis();
  return true;
}

static void failBLETrackerHCI(const char* operation, uint16_t opcode, uint8_t status) {
  bleTrackerHciState = BLE_HCI_FAILED;
  bleTrackerRuntimeBlocked = true;
  bleTrackerScanning = false;
  bleTrackerStarted = false;
  bleTrackerInitGuard = 0;
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) esp_bt_controller_disable();
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) esp_bt_controller_deinit();
  Log.error(F("[BLE][ADV] HCI failure operation=%s opcode=0x%04X status=0x%02X heap=%u" CR),
            operation, opcode, status, ESP.getFreeHeap());
}

static void processBLETrackerHCICompletion() {
  uint16_t opcode;
  uint8_t status;
  portENTER_CRITICAL(&bleTrackerHciMux);
  if (!bleTrackerCommandComplete) {
    portEXIT_CRITICAL(&bleTrackerHciMux);
    return;
  }
  opcode = bleTrackerCompletedOpcode;
  status = bleTrackerCompletedStatus;
  bleTrackerCommandComplete = false;
  portEXIT_CRITICAL(&bleTrackerHciMux);

  if (status) {
    failBLETrackerHCI("command-complete", opcode, status);
    return;
  }
  if (bleTrackerHciState == BLE_HCI_WAIT_RESET && opcode == HCI_OPCODE_RESET) {
    bleTrackerHciState = BLE_HCI_SEND_EVENT_MASK;
  } else if (bleTrackerHciState == BLE_HCI_WAIT_EVENT_MASK && opcode == HCI_OPCODE_SET_EVENT_MASK) {
    bleTrackerHciState = BLE_HCI_SEND_LE_EVENT_MASK;
  } else if (bleTrackerHciState == BLE_HCI_WAIT_LE_EVENT_MASK && opcode == HCI_OPCODE_LE_SET_EVENT_MASK) {
    bleTrackerHciState = BLE_HCI_SEND_SCAN_PARAMETERS;
  } else if (bleTrackerHciState == BLE_HCI_WAIT_SCAN_PARAMETERS && opcode == HCI_OPCODE_LE_SET_SCAN_PARAMETERS) {
    bleTrackerHciState = BLE_HCI_SEND_SCAN_ENABLE;
  } else if (bleTrackerHciState == BLE_HCI_WAIT_SCAN_ENABLE && opcode == HCI_OPCODE_LE_SET_SCAN_ENABLE) {
    bleTrackerHciState = BLE_HCI_READY;
    bleTrackerScanning = true;
    const uint32_t scanReadyAt = millis();
    for (uint8_t slot = 0; slot < BLE_TRACKER_MAX; slot++) {
      if (bleTrackerInitialStatePending[slot] && bleTrackerInitialStateSince[slot] == 0)
        bleTrackerInitialStateSince[slot] = scanReadyAt;
    }
    bleTrackerInitGuard = 0;
    Log.verbose(F("[BLE][ADV] passive scan ready heap=%u max_alloc=%u interval_ms=%u window_ms=%u" CR),
               ESP.getFreeHeap(), ESP.getMaxAllocHeap(), BLE_TRACKER_SCAN_INTERVAL_MS, BLE_TRACKER_SCAN_WINDOW_MS);
  }
}

static void driveBLETrackerHCI() {
  processBLETrackerHCICompletion();
  if (bleTrackerHciState == BLE_HCI_FAILED || bleTrackerHciState == BLE_HCI_READY) return;
  if ((bleTrackerHciState == BLE_HCI_WAIT_RESET ||
       bleTrackerHciState == BLE_HCI_WAIT_EVENT_MASK ||
       bleTrackerHciState == BLE_HCI_WAIT_LE_EVENT_MASK ||
       bleTrackerHciState == BLE_HCI_WAIT_SCAN_PARAMETERS ||
       bleTrackerHciState == BLE_HCI_WAIT_SCAN_ENABLE) &&
      millis() - bleTrackerHciCommandSentAt > 5000UL) {
    failBLETrackerHCI("command-timeout", 0, 0xFF);
    return;
  }
  if (!bleTrackerHciSendAvailable && !esp_vhci_host_check_send_available()) return;

  if (bleTrackerHciState == BLE_HCI_SEND_RESET) {
    if (sendBLETrackerHCICommand(HCI_OPCODE_RESET, nullptr, 0)) bleTrackerHciState = BLE_HCI_WAIT_RESET;
  } else if (bleTrackerHciState == BLE_HCI_SEND_EVENT_MASK) {
    // Same conservative base mask used by the NimBLE host. In particular,
    // 0x2000000000000000 enables the HCI LE Meta-Event (event 0x3E).
    const uint8_t eventMask[8] = {0x90, 0x80, 0x00, 0x02, 0x00, 0x80, 0x00, 0x20};
    if (sendBLETrackerHCICommand(HCI_OPCODE_SET_EVENT_MASK, eventMask, sizeof(eventMask)))
      bleTrackerHciState = BLE_HCI_WAIT_EVENT_MASK;
  } else if (bleTrackerHciState == BLE_HCI_SEND_LE_EVENT_MASK) {
    // LE event bit 1 enables legacy LE Advertising Report (subevent 0x02).
    const uint8_t leEventMask[8] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (sendBLETrackerHCICommand(HCI_OPCODE_LE_SET_EVENT_MASK, leEventMask, sizeof(leEventMask)))
      bleTrackerHciState = BLE_HCI_WAIT_LE_EVENT_MASK;
  } else if (bleTrackerHciState == BLE_HCI_SEND_SCAN_PARAMETERS) {
    uint16_t interval = constrain(static_cast<uint16_t>((BLE_TRACKER_SCAN_INTERVAL_MS * 1000UL) / 625UL),
                                  static_cast<uint16_t>(0x0004), static_cast<uint16_t>(0x4000));
    uint16_t window = constrain(static_cast<uint16_t>((BLE_TRACKER_SCAN_WINDOW_MS * 1000UL) / 625UL),
                                static_cast<uint16_t>(0x0004), interval);
    uint8_t parameters[7] = {0x00, static_cast<uint8_t>(interval), static_cast<uint8_t>(interval >> 8),
                             static_cast<uint8_t>(window), static_cast<uint8_t>(window >> 8), 0x00, 0x00};
    if (sendBLETrackerHCICommand(HCI_OPCODE_LE_SET_SCAN_PARAMETERS, parameters, sizeof(parameters)))
      bleTrackerHciState = BLE_HCI_WAIT_SCAN_PARAMETERS;
  } else if (bleTrackerHciState == BLE_HCI_SEND_SCAN_ENABLE) {
    // Duplicate filtering stays disabled so presence timeouts keep refreshing.
    uint8_t parameters[2] = {0x01, 0x00};
    if (sendBLETrackerHCICommand(HCI_OPCODE_LE_SET_SCAN_ENABLE, parameters, sizeof(parameters)))
      bleTrackerHciState = BLE_HCI_WAIT_SCAN_ENABLE;
  }
}

static void enqueueBLETrackerState(uint8_t slot, const BLETrackerConfig_s& tracker, const char* reason) {
  StaticJsonDocument<384> jsonBuffer;
  JsonObject state = jsonBuffer.to<JsonObject>();
  String origin = String("/BTtracker/") + String(slot + 1);
  state["origin"] = origin;
  state["presence"] = tracker.present;
  if (tracker.present && tracker.lastRssi > -127) {
    state["rssi"] = tracker.lastRssi;
  } else {
    // -127 is our internal "never detected" sentinel, not a real RSSI.
    // JSON null makes Home Assistant expose an unknown value instead of a
    // misleading -127 dBm measurement.
    state["rssi"] = nullptr;
  }
  state["mac"] = tracker.mac;
  state["name"] = tracker.name;
  state["last_seen"] = tracker.lastSeen / 1000UL;
  state["retain"] = true;
  enqueueJsonObject(state, QueueSemaphoreTimeOutTask);
  Log.verbose(F("[BLE][ADV] state slot=%u name=%s mac=%s presence=%T rssi=%d reason=%s heap=%u" CR),
             slot + 1, tracker.name, tracker.mac, tracker.present, tracker.lastRssi, reason, ESP.getFreeHeap());
}

#  ifdef ZmqttDiscovery
void launchBTDiscovery(bool overrideDiscovery) {
  if (!overrideDiscovery && !bleTrackerDiscoveryDirty) return;
  bleTrackerDiscoveryDirty = false;
  for (uint8_t slot = 0; slot < BLE_TRACKER_MAX; slot++) {
    BLETrackerConfig_s tracker = getBLETrackerConfig(slot);
    String slotText = String(slot + 1);
    String stateTopic = String("/BTtracker/") + slotText;
    String baseId = String(gateway_name) + "-ble-tracker-" + slotText;
    if (!tracker.enabled || !tracker.mac[0]) {
      if (bleTrackerConfigWasStored || !overrideDiscovery) {
        eraseTopic("binary_sensor", baseId.c_str());
        eraseTopic("sensor", (baseId + "-rssi").c_str());
      }
      continue;
    }
    createDiscovery("binary_sensor", stateTopic.c_str(), tracker.name, baseId.c_str(),
                    "", "presence", "{{ 'ON' if value_json.presence else 'OFF' }}", "ON", "OFF", "",
                    static_cast<int>(tracker.timeoutSeconds),
                    "", "", true, "", "", "", "", "", false, stateClassNone);
    String rssiName = String(tracker.name) + " RSSI";
    String rssiId = baseId + "-rssi";
    createDiscovery("sensor", stateTopic.c_str(), rssiName.c_str(), rssiId.c_str(),
                    "", "signal_strength", "{{ value_json.rssi }}", "", "", "dBm", 0,
                    "", "", true, "", "", "", "", "", false, stateClassMeasurement);
    if (!bleTrackerInitialStatePending[slot])
      enqueueBLETrackerState(slot, tracker, overrideDiscovery ? "mqtt-discovery" : "configuration");
    Log.verbose(F("[BLE][ADV] Home Assistant discovery slot=%u name=%s mac=%s" CR),
               slot + 1, tracker.name, tracker.mac);
  }
}
#  else
void launchBTDiscovery(bool) {}
#  endif

static bool startBLETrackerRadio() {
  uint32_t heapBefore = ESP.getFreeHeap();
  if (heapBefore < BLE_TRACKER_MIN_HEAP_TO_START) {
    Log.warning(F("[BLE][ADV] start deferred free_heap=%u required=%u" CR),
                heapBefore, BLE_TRACKER_MIN_HEAP_TO_START);
    return false;
  }

  esp_bt_controller_status_t initialStatus = esp_bt_controller_get_status();
  Log.verbose(F("[BLE][ADV] controller initialization start status=%d heap=%u max_alloc=%u" CR),
             initialStatus, heapBefore, ESP.getMaxAllocHeap());
  bleTrackerInitGuard = BLE_TRACKER_INIT_GUARD_MAGIC;

  esp_bt_controller_config_t controllerConfig = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  controllerConfig.mode = ESP_BT_MODE_BLE;
  controllerConfig.ble_max_conn = 1;
  controllerConfig.normal_adv_size = 20;

  // Subscribe the task performing this known-risk ESP-IDF call to the task
  // watchdog. If the controller ever stalls internally, the RTC watchdog can
  // reset the chip even though the Arduino loop cannot run its own timeout.
  // A retained init guard then boots the gateway with BLE disabled instead of
  // leaving WiFi pingable while WebUI and MQTT are frozen.
  esp_err_t watchdogResult = esp_task_wdt_add(nullptr);
  const bool watchdogAdded = watchdogResult == ESP_OK;
  if (!watchdogAdded) {
    Log.warning(F("[BLE][ADV] controller init task watchdog unavailable error=%s (%d)" CR),
                esp_err_to_name(watchdogResult), watchdogResult);
  }
  esp_err_t result = esp_bt_controller_init(&controllerConfig);
  if (watchdogAdded) esp_task_wdt_delete(nullptr);
  if (result != ESP_OK) {
    Log.error(F("[BLE][ADV] controller init failed error=%s (%d) initial_status=%d final_status=%d heap=%u" CR),
              esp_err_to_name(result), result, initialStatus, esp_bt_controller_get_status(), ESP.getFreeHeap());
    bleTrackerInitGuard = 0;
    bleTrackerRuntimeBlocked = true;
    return false;
  }

  result = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (result != ESP_OK) {
    Log.error(F("[BLE][ADV] controller enable failed error=%s (%d) status=%d heap=%u" CR),
              esp_err_to_name(result), result, esp_bt_controller_get_status(), ESP.getFreeHeap());
    esp_bt_controller_deinit();
    bleTrackerInitGuard = 0;
    bleTrackerRuntimeBlocked = true;
    return false;
  }

  result = esp_vhci_host_register_callback(&bleTrackerVHCICallbacks);
  if (result != ESP_OK) {
    Log.error(F("[BLE][ADV] VHCI callback registration failed error=%s (%d)" CR), esp_err_to_name(result), result);
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    bleTrackerInitGuard = 0;
    bleTrackerRuntimeBlocked = true;
    return false;
  }

  uint32_t heapAfter = ESP.getFreeHeap();
  if (heapAfter < BLE_TRACKER_MIN_HEAP_AFTER_START) {
    Log.error(F("[BLE][ADV] controller disabled: insufficient heap before=%u after=%u required=%u" CR),
              heapBefore, heapAfter, BLE_TRACKER_MIN_HEAP_AFTER_START);
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    bleTrackerInitGuard = 0;
    bleTrackerRuntimeBlocked = true;
    return false;
  }

  bleTrackerStarted = true;
  bleTrackerHciState = BLE_HCI_SEND_RESET;
  bleTrackerInitGuard = 0;
  Log.verbose(F("[BLE][ADV] controller ready heap_before=%u heap_after=%u consumed=%u max_alloc=%u" CR),
             heapBefore, heapAfter, heapBefore - heapAfter, ESP.getMaxAllocHeap());
  return true;
}

static void bleTrackerStartTask(void*) {
  bool started = startBLETrackerRadio();
  Log.verbose(F("[BLE][ADV] asynchronous controller initialization completed started=%T heap=%u" CR),
              started, ESP.getFreeHeap());
  bleTrackerStarting = false;
  bleTrackerStartTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void setupBLETracker() {
  initBLETrackerConfig();
  bleTrackerMutex = xSemaphoreCreateMutex();
  bleTrackerReportQueue = xQueueCreate(BLE_TRACKER_REPORT_QUEUE_SIZE, sizeof(BLETrackerRawReport_s));
  if (!bleTrackerMutex || !bleTrackerReportQueue) {
    bleTrackerRuntimeBlocked = true;
    Log.error(F("[BLE][ADV] disabled: unable to allocate runtime synchronization" CR));
    return;
  }
  loadBLETrackerConfig();

  if (bleTrackerInitGuard == BLE_TRACKER_INIT_GUARD_MAGIC && esp_reset_reason() != ESP_RST_POWERON) {
    bleTrackerRuntimeBlocked = true;
    Log.error(F("[BLE][ADV] safe mode: previous controller initialization did not complete; disabled for this boot" CR));
  }
  bleTrackerInitGuard = 0;

  uint32_t heapBeforeRelease = ESP.getFreeHeap();
  esp_err_t releaseResult = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  Log.verbose(F("[BLE][ADV] Classic BT memory release result=%s heap_before=%u heap_after=%u recovered=%u" CR),
             esp_err_to_name(releaseResult), heapBeforeRelease, ESP.getFreeHeap(), ESP.getFreeHeap() - heapBeforeRelease);
  Log.verbose(F("[BLE][ADV] VHCI observer scheduled delay_ms=%u slots=%u heap_start_min=%u heap_after_min=%u" CR),
             BLE_TRACKER_START_DELAY_MS, BLE_TRACKER_MAX, BLE_TRACKER_MIN_HEAP_TO_START,
             BLE_TRACKER_MIN_HEAP_AFTER_START);

  // Initialize while the heap is still contiguous and before WiFi, CC1101 and
  // RTL_433 start their tasks. HCI scanning itself begins later from loop().
  // This avoids the intermittent ESP-IDF controller-init deadlock observed
  // when Bluetooth was initialized after the shared WiFi radio was active.
  if (!bleTrackerRuntimeBlocked && !startBLETrackerRadio()) {
    Log.warning(F("[BLE][ADV] early controller initialization did not start; runtime retry remains available" CR));
  }
}

void stopBLETracker(bool deinitRadio) {
  if (bleTrackerStarting) {
    // Deinitializing the controller concurrently with esp_bt_controller_init()
    // is unsafe. OTA entry points normally reject this short window; other
    // callers recover with a clean reboot instead of racing ESP-IDF.
    Log.error(F("[BLE][ADV] stop requested during controller initialization; restarting safely" CR));
    delay(100);
    ESP.restart();
    return;
  }
  bleTrackerScanning = false;
  bleTrackerWebPauseRequested = false;
  if (deinitRadio && esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
    uint32_t before = ESP.getFreeHeap();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    bleTrackerStarted = false;
    bleTrackerHciState = BLE_HCI_IDLE;
    Log.verbose(F("[BLE][ADV] controller stopped heap_before=%u heap_after=%u" CR), before, ESP.getFreeHeap());
  }
  bleTrackerInitGuard = 0;
}

bool isBLETrackerStarting() {
  return bleTrackerStarting;
}

static bool setBLETrackerScanEnabledForWeb(bool enabled) {
  if (bleTrackerRuntimeBlocked || !bleTrackerStarted ||
      esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED ||
      bleTrackerHciState != BLE_HCI_READY) {
    return false;
  }
  if (bleTrackerScanning == enabled) return true;

  portENTER_CRITICAL(&bleTrackerHciMux);
  bleTrackerCommandComplete = false;
  portEXIT_CRITICAL(&bleTrackerHciMux);

  const uint32_t sendStarted = millis();
  while (!bleTrackerHciSendAvailable && !esp_vhci_host_check_send_available()) {
    if (millis() - sendStarted >= 1000UL) {
      Log.warning(F("[BLE][ADV] web scan %s deferred: HCI transport busy" CR), enabled ? "resume" : "pause");
      return false;
    }
    delay(1);
  }

  const uint8_t parameters[2] = {static_cast<uint8_t>(enabled ? 0x01 : 0x00), 0x00};
  if (!sendBLETrackerHCICommand(HCI_OPCODE_LE_SET_SCAN_ENABLE, parameters, sizeof(parameters))) return false;

  const uint32_t commandStarted = millis();
  while (millis() - commandStarted < 1500UL) {
    bool completed = false;
    uint16_t opcode = 0;
    uint8_t status = 0xFF;
    portENTER_CRITICAL(&bleTrackerHciMux);
    if (bleTrackerCommandComplete) {
      completed = true;
      opcode = bleTrackerCompletedOpcode;
      status = bleTrackerCompletedStatus;
      bleTrackerCommandComplete = false;
    }
    portEXIT_CRITICAL(&bleTrackerHciMux);

    if (completed) {
      if (opcode == HCI_OPCODE_LE_SET_SCAN_ENABLE && status == 0) {
        bleTrackerScanning = enabled;
        Log.verbose(F("[BLE][ADV] scan %s for WebUI heap=%u" CR), enabled ? "resumed" : "paused", ESP.getFreeHeap());
        return true;
      }
      Log.warning(F("[BLE][ADV] web scan %s failed opcode=0x%04X status=0x%02X" CR),
                  enabled ? "resume" : "pause", opcode, status);
      return false;
    }
    delay(1);
  }

  Log.warning(F("[BLE][ADV] web scan %s timed out" CR), enabled ? "resume" : "pause");
  return false;
}

bool pauseBLETrackerScanForWeb() {
  bleTrackerWebPauseRequested = true;
  if (!bleTrackerScanning) return false;
  return setBLETrackerScanEnabledForWeb(false);
}

void resumeBLETrackerScanAfterWeb() {
  bleTrackerWebPauseRequested = false;
  if (!bleTrackerScanning && !setBLETrackerScanEnabledForWeb(true)) {
    Log.warning(F("[BLE][ADV] unable to resume scan after WebUI response" CR));
  }
}

static void publishBLETrackerChanges() {
  uint32_t now = millis();
  for (uint8_t slot = 0; slot < BLE_TRACKER_MAX; slot++) {
    BLETrackerConfig_s copy = {};
    const char* reason = nullptr;
    if (xSemaphoreTake(bleTrackerMutex, pdMS_TO_TICKS(20)) == pdFALSE) return;
    BLETrackerConfig_s& tracker = BLETrackerConfig[slot];
    const uint32_t timeoutMs = tracker.timeoutSeconds * 1000UL;
    if (tracker.enabled && bleTrackerInitialStatePending[slot] && bleTrackerInitialStateSince[slot] != 0 &&
        now - bleTrackerInitialStateSince[slot] >= timeoutMs) {
      tracker.present = false;
      tracker.lastPublish = now;
      copy = tracker;
      reason = "initial-timeout";
      bleTrackerInitialStatePending[slot] = false;
      bleTrackerInitialStateSince[slot] = 0;
      bleTrackerPendingPublish[slot] = false;
    } else if (tracker.enabled && tracker.present && now - tracker.lastSeen >= timeoutMs) {
      tracker.present = false;
      tracker.lastPublish = now;
      copy = tracker;
      reason = "timeout";
      bleTrackerPendingPublish[slot] = false;
    } else if (bleTrackerPendingPublish[slot]) {
      copy = tracker;
      reason = bleTrackerPendingReason[slot] == BLE_TRACKER_REASON_DETECTED ? "detected" : "refresh";
      bleTrackerPendingPublish[slot] = false;
    }
    xSemaphoreGive(bleTrackerMutex);
    if (reason) enqueueBLETrackerState(slot, copy, reason);
  }
}

void loopBLETracker() {
  BLETrackerRawReport_s report;
  uint8_t processed = 0;
  while (bleTrackerReportQueue && processed < 8 && xQueueReceive(bleTrackerReportQueue, &report, 0) == pdTRUE) {
    processBLEAdvertisement(report.mac, report.name, report.rssi);
    processed++;
  }
  publishBLETrackerChanges();
  if (bleTrackerRuntimeBlocked) return;

  uint32_t now = millis();
  if (!bleTrackerStarted) {
    if (bleTrackerStarting) {
      if (now - bleTrackerStartTaskStartedAt >= 12000UL) {
        // Controller initialization has occasionally stalled inside ESP-IDF.
        // It runs outside the Arduino loop so MQTT/WebUI remain responsive;
        // restart into the RTC-guarded BLE safe mode instead of hanging until
        // the user physically removes power.
        Log.error(F("[BLE][ADV] controller initialization timeout elapsed_ms=%u; restarting in safe mode" CR),
                  now - bleTrackerStartTaskStartedAt);
        delay(100);
        ESP.restart();
      }
      return;
    }
    if (now < BLE_TRACKER_START_DELAY_MS || now - bleTrackerLastStartAttempt < 10000UL) return;
    bleTrackerLastStartAttempt = now;
    bleTrackerStartTaskStartedAt = now;
    bleTrackerStarting = true;
    BaseType_t created = xTaskCreatePinnedToCore(bleTrackerStartTask, "ble-init", 4096, nullptr, 1,
                                                &bleTrackerStartTaskHandle, ARDUINO_RUNNING_CORE);
    if (created != pdPASS) {
      bleTrackerStarting = false;
      bleTrackerStartTaskHandle = nullptr;
      Log.warning(F("[BLE][ADV] unable to create controller initialization task heap=%u" CR), ESP.getFreeHeap());
    }
    return;
  }
  driveBLETrackerHCI();
  if (bleTrackerStarted && bleTrackerHciState == BLE_HCI_READY && !bleTrackerScanning &&
      !bleTrackerWebPauseRequested && now - bleTrackerLastResumeAttempt >= 5000UL) {
    bleTrackerLastResumeAttempt = now;
    if (!setBLETrackerScanEnabledForWeb(true))
      Log.warning(F("[BLE][ADV] automatic scan resume deferred; retrying in 5 seconds" CR));
  }
}

String stateBLETrackerMeasures() {
  StaticJsonDocument<320> jsonBuffer;
  JsonObject state = jsonBuffer.to<JsonObject>();
  state["started"] = bleTrackerStarted;
  state["starting"] = bleTrackerStarting;
  state["blocked"] = bleTrackerRuntimeBlocked;
  state["scanning"] = bleTrackerScanning;
  state["advertisements"] = bleTrackerAdvertisements;
  state["matched"] = bleTrackerMatchedAdvertisements;
  state["dropped"] = bleTrackerDroppedReports;
  state["pending"] = bleTrackerReportQueue ? uxQueueMessagesWaiting(bleTrackerReportQueue) : 0;
  state["heap"] = ESP.getFreeHeap();
  String output;
  serializeJson(state, output);
  return output;
}

#endif
