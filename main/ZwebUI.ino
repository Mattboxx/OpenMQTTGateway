/*  
  Theengs OpenMQTTGateway - We Unite Sensors in One Open-Source Interface

   Act as a gateway between your 433mhz, infrared IR, BLE, LoRa signal and one interface like an MQTT broker 
   Send and receiving command by MQTT

    Copyright: (c)Florian ROBERT
  
    This file is part of OpenMQTTGateway.
    
    OpenMQTTGateway is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenMQTTGateway is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "User_config.h"
#if defined(ZwebUI) && defined(ESP32)
#  include <ArduinoJson.h>
#  include <SPIFFS.h>
#  include <Update.h>
#  include <WebServer.h> // Docs for this are here - https://github.com/espressif/arduino-esp32/tree/master/libraries/WebServer
#  ifdef ZgatewayBLETracker
#    include <esp_coexist.h>
#  endif

#  include "ArduinoLog.h"
#  include "config_WebContent.h"
#  include "config_WebUI.h"

#  if defined(ZgatewayCloud)
#    include "config_Cloud.h"
#  endif

#  if defined(ZdisplaySSD1306)
#    include "config_SSD1306.h"
#  endif

uint32_t requestToken = 0;
extern bool stateSnapshotOnly;

QueueHandle_t webUIQueue;

#  ifdef ZgatewayBLETracker
extern bool pauseBLETrackerScanForWeb();
extern void resumeBLETrackerScanAfterWeb();
#  endif

class BLEAwareWebServer : public WebServer {
public:
  explicit BLEAwareWebServer(uint16_t port) : WebServer(port) {}

  void enableTcpNoDelay() {
    _server.setNoDelay(true);
  }

  void handleClient() override {
#  ifdef ZgatewayBLETracker
    // The ESP32 uses one 2.4 GHz radio for Wi-Fi and BLE. Pause passive BLE
    // scanning for the complete lifetime of an HTTP client, including the
    // wait for request data and all response chunks. This prevents repeated
    // WebUI requests from starving Wi-Fi long enough to miss AP beacons.
    const bool clientPending = _currentStatus != HC_NONE || _server.hasClient();
    if (clientPending) {
      _bleResumeAfter = 0;
      if (!_webRadioGuardActive) {
        esp_err_t preferenceResult = esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
        if (preferenceResult != ESP_OK) {
          Log.warning(F("[WebUI] unable to prefer WiFi during request error=%d" CR), preferenceResult);
        }
        _blePausedForRequest = pauseBLETrackerScanForWeb();
        _webRadioGuardActive = true;
      }
    }
#  endif

    WebServer::handleClient();

#  ifdef ZgatewayBLETracker
    if (_webRadioGuardActive && _currentStatus == HC_NONE) {
      // WiFiClient::write can return after the response has entered the TCP
      // buffers but before every packet is transmitted. Keep BLE quiet during
      // this drain interval, and extend it when another client arrives.
      if (!_bleResumeAfter) _bleResumeAfter = millis() + 2000UL;
      if ((int32_t)(millis() - _bleResumeAfter) >= 0) {
        if (_blePausedForRequest) resumeBLETrackerScanAfterWeb();
        esp_err_t preferenceResult = esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
        if (preferenceResult != ESP_OK) {
          Log.warning(F("[WebUI] unable to restore balanced WiFi/BLE coexistence error=%d" CR), preferenceResult);
        }
        _blePausedForRequest = false;
        _webRadioGuardActive = false;
        _bleResumeAfter = 0;
      }
    }
#  endif
  }

protected:
#  ifdef ZgatewayBLETracker
  bool _blePausedForRequest = false;
  bool _webRadioGuardActive = false;
  uint32_t _bleResumeAfter = 0;
#  endif
};

BLEAwareWebServer server(80);

/*------------------- External functions ----------------------*/
extern void eraseConfig();
extern unsigned long uptime();

/*------------------- Web Console Globals ----------------------*/

#  define ROW_LENGTH 1024

const uint16_t LOG_BUFFER_SIZE = 6096;
uint32_t log_buffer_pointer;
void* log_buffer_mutex;
char log_buffer[LOG_BUFFER_SIZE]; // Log buffer in HEAP

const uint16_t MAX_LOGSZ = LOG_BUFFER_SIZE - 96;
const uint16_t TOPSZ = 151; // Max number of characters in topic string
uint8_t masterlog_level; // Master log level used to override set log level
bool reset_web_log_flag = false; // Reset web console log

const char* www_username = WEBUI_LOGIN;
String authFailResponse = "Authentication Failed";
bool webUISecure = WEBUI_AUTH;
boolean displayMetric = DISPLAY_METRIC;

/*********************************************************************************************\
 * ESP32 AutoMutex
\*********************************************************************************************/

//////////////////////////////////////////
// automutex.
// create a mute in your driver with:
// void *mutex = nullptr;
//
// then protect any function with
// TasAutoMutex m(&mutex, "somename");
// - mutex is automatically initialised if not already intialised.
// - it will be automagically released when the function is over.
// - the same thread can take multiple times (recursive).
// - advanced options m.give() and m.take() allow you fine control within a function.
// - if take=false at creat, it will not be initially taken.
// - name is used in serial log of mutex deadlock.
// - maxWait in ticks is how long it will wait before failing in a deadlock scenario (and then emitting on serial)
class TasAutoMutex {
  SemaphoreHandle_t mutex;
  bool taken;
  int maxWait;
  const char* name;

public:
  TasAutoMutex(SemaphoreHandle_t* mutex, const char* name = "", int maxWait = 40, bool take = true);
  ~TasAutoMutex();
  void give();
  void take();
  static void init(SemaphoreHandle_t* ptr);
};
//////////////////////////////////////////

TasAutoMutex::TasAutoMutex(SemaphoreHandle_t* mutex, const char* name, int maxWait, bool take) {
  if (mutex) {
    if (!(*mutex)) {
      TasAutoMutex::init(mutex);
    }
    this->mutex = *mutex;
    this->maxWait = maxWait;
    this->name = name;
    if (take) {
      this->taken = xSemaphoreTakeRecursive(this->mutex, this->maxWait);
      //      if (!this->taken){
      //        Serial.printf("\r\nMutexfail %s\r\n", this->name);
      //      }
    }
  } else {
    this->mutex = (SemaphoreHandle_t) nullptr;
  }
}

TasAutoMutex::~TasAutoMutex() {
  if (this->mutex) {
    if (this->taken) {
      xSemaphoreGiveRecursive(this->mutex);
      this->taken = false;
    }
  }
}

void TasAutoMutex::init(SemaphoreHandle_t* ptr) {
  SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
  (*ptr) = mutex;
  // needed, else for ESP8266 as we will initialis more than once in logging
  //  (*ptr) = (void *) 1;
}

void TasAutoMutex::give() {
  if (this->mutex) {
    if (this->taken) {
      xSemaphoreGiveRecursive(this->mutex);
      this->taken = false;
    }
  }
}

void TasAutoMutex::take() {
  if (this->mutex) {
    if (!this->taken) {
      this->taken = xSemaphoreTakeRecursive(this->mutex, this->maxWait);
      //      if (!this->taken){
      //        Serial.printf("\r\nMutexfail %s\r\n", this->name);
      //      }
    }
  }
}

// Get span until single character in string
size_t strchrspn(const char* str1, int character) {
  size_t ret = 0;
  char* start = (char*)str1;
  char* end = strchr(str1, character);
  if (end) ret = end - start;
  return ret;
}

int WifiGetRssiAsQuality(int rssi) {
  int quality = 0;

  if (rssi <= -100) {
    quality = 0;
  } else if (rssi >= -50) {
    quality = 100;
  } else {
    quality = 2 * (rssi + 100);
  }
  return quality;
}

char* GetTextIndexed(char* destination, size_t destination_size, uint32_t index, const char* haystack) {
  // Returns empty string if not found
  // Returns text of found
  char* write = destination;
  const char* read = haystack;

  index++;
  while (index--) {
    size_t size = destination_size - 1;
    write = destination;
    char ch = '.';
    while ((ch != '\0') && (ch != '|')) {
      ch = pgm_read_byte(read++);
      if (size && (ch != '|')) {
        *write++ = ch;
        size--;
      }
    }
    if (0 == ch) {
      if (index) {
        write = destination;
      }
      break;
    }
  }
  *write = '\0';
  return destination;
}

const char kUnescapeCode[] = "&><\"\'\\";
const char kEscapeCode[] PROGMEM = "&amp;|&gt;|&lt;|&quot;|&apos;|&#92;";

String HtmlEscape(const String unescaped) {
  char escaped[10];
  size_t ulen = unescaped.length();
  String result;
  result.reserve(ulen); // pre-reserve the required space to avoid mutiple reallocations
  for (size_t i = 0; i < ulen; i++) {
    char c = unescaped[i];
    char* p = strchr(kUnescapeCode, c);
    if (p != nullptr) {
      result += GetTextIndexed(escaped, sizeof(escaped), p - kUnescapeCode, kEscapeCode);
    } else {
      result += c;
    }
  }
  return result;
}

void AddLogData(uint32_t loglevel, const char* log_data, const char* log_data_payload = nullptr, const char* log_data_retained = nullptr) {
  // Store log_data in buffer
  // To lower heap usage log_data_payload may contain the payload data from MqttPublishPayload()
  //  and log_data_retained may contain optional retained message from MqttPublishPayload()
#  ifdef ESP32
  // this takes the mutex, and will be release when the class is destroyed -
  // i.e. when the functon leaves  You CAN call mutex.give() to leave early.
  TasAutoMutex mutex((SemaphoreHandle_t*)&log_buffer_mutex);
#  endif // ESP32

  char empty[2] = {0};
  if (!log_data_payload) {
    log_data_payload = empty;
  }
  if (!log_data_retained) {
    log_data_retained = empty;
  }

  if (!log_buffer) {
    return;
  } // Leave now if there is no buffer available

  // Delimited, zero-terminated buffer of log lines.
  // Each entry has this format: [index][loglevel][log data]['\1']

  // Truncate log messages longer than MAX_LOGSZ which is the log buffer size minus 64 spare
  uint32_t log_data_len = strlen(log_data) + strlen(log_data_payload) + strlen(log_data_retained);
  char too_long[TOPSZ];
  if (log_data_len > MAX_LOGSZ) {
    snprintf_P(too_long, sizeof(too_long) - 20, PSTR("%s%s"), log_data, log_data_payload); // 20 = strlen("... 123456 truncated")
    snprintf_P(too_long, sizeof(too_long), PSTR("%s... %d truncated"), too_long, log_data_len);
    log_data = too_long;
    log_data_payload = empty;
    log_data_retained = empty;
  }

  log_buffer_pointer &= 0xFF;
  if (!log_buffer_pointer) {
    log_buffer_pointer++; // Index 0 is not allowed as it is the end of char string
  }
  while (log_buffer_pointer == log_buffer[0] || // If log already holds the next index, remove it
         strlen(log_buffer) + strlen(log_data) + strlen(log_data_payload) + strlen(log_data_retained) + 4 > LOG_BUFFER_SIZE) // 4 = log_buffer_pointer + '\1' + '\0'
  {
    char* it = log_buffer;
    it++; // Skip log_buffer_pointer
    it += strchrspn(it, '\1'); // Skip log line
    it++; // Skip delimiting "\1"
    memmove(log_buffer, it, LOG_BUFFER_SIZE - (it - log_buffer)); // Move buffer forward to remove oldest log line
  }
  snprintf_P(log_buffer, LOG_BUFFER_SIZE, PSTR("%s%c%c%s%s%s%s\1"),
             log_buffer, log_buffer_pointer++, '0' + loglevel, "", log_data, log_data_payload, log_data_retained);
  log_buffer_pointer &= 0xFF;
  if (!log_buffer_pointer) {
    log_buffer_pointer++; // Index 0 is not allowed as it is the end of char string
  }
}

bool NeedLogRefresh(uint32_t req_loglevel, uint32_t index) {
  if (!log_buffer) {
    return false;
  } // Leave now if there is no buffer available

#  ifdef ESP32
  // this takes the mutex, and will be release when the class is destroyed -
  // i.e. when the functon leaves  You CAN call mutex.give() to leave early.
  TasAutoMutex mutex((SemaphoreHandle_t*)&log_buffer_mutex);
#  endif // ESP32

  // Skip initial buffer fill
  if (strlen(log_buffer) < LOG_BUFFER_SIZE / 2) {
    return false;
  }

  char* line;
  size_t len;
  if (!GetLog(req_loglevel, &index, &line, &len)) {
    return false;
  }
  return ((line - log_buffer) < LOG_BUFFER_SIZE / 4);
}

bool GetLog(uint32_t req_loglevel, uint32_t* index_p, char** entry_pp, size_t* len_p) {
  if (!log_buffer) {
    return false;
  } // Leave now if there is no buffer available
  if (uptime() < 3) {
    return false;
  } // Allow time to setup correct log level

  uint32_t index = *index_p;
  if (!req_loglevel || (index == log_buffer_pointer)) {
    return false;
  }

#  ifdef ESP32
  // this takes the mutex, and will be release when the class is destroyed -
  // i.e. when the functon leaves  You CAN call mutex.give() to leave early.
  TasAutoMutex mutex((SemaphoreHandle_t*)&log_buffer_mutex);
#  endif // ESP32

  if (!index) { // Dump all
    index = log_buffer[0];
  }

  do {
    size_t len = 0;
    uint32_t loglevel = 0;
    char* entry_p = log_buffer;
    do {
      uint32_t cur_idx = *entry_p;
      entry_p++;
      size_t tmp = strchrspn(entry_p, '\1');
      tmp++; // Skip terminating '\1'
      if (cur_idx == index) { // Found the requested entry
        loglevel = *entry_p - '0';
        entry_p++; // Skip loglevel
        len = tmp - 1;
        break;
      }
      entry_p += tmp;
    } while (entry_p < log_buffer + LOG_BUFFER_SIZE && *entry_p != '\0');
    index++;
    if (index > 255) {
      index = 1;
    } // Skip 0 as it is not allowed
    *index_p = index;
    if ((len > 0) &&
        (loglevel <= req_loglevel) &&
        (masterlog_level <= req_loglevel)) {
      *entry_pp = entry_p;
      *len_p = len;
      return true;
    }
    delay(0);
  } while (index != log_buffer_pointer);
  return false;
}

/*------------------- Local functions ----------------------*/

#  ifdef WEBUI_DEVELOPMENT
//format bytes
String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}

bool exists(String path) {
  bool yes = false;
  File file = FILESYSTEM.open(path, "r");
  if (!file.isDirectory()) {
    yes = true;
  }
  file.close();
  return yes;
}
#  endif

/**
 * @brief / - Page
 * 
 */
void handleRoot() {
  WEBUI_TRACE_LOG(F("handleRoot: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("Arg: %d, %s=%s" CR), i, server.argName(i).c_str(), server.arg(i).c_str());
    }
    if (server.hasArg("m")) {
      if (currentWebUIMessage) {
        server.send(200, "application/json", "{t}{s}<b>" + String(currentWebUIMessage->title) + "</b>{e}{s}" + String(currentWebUIMessage->line1) + "{e}{s}" + String(currentWebUIMessage->line2) + "{e}{s}" + String(currentWebUIMessage->line3) + "{e}{s}" + String(currentWebUIMessage->line4) + "{e}</table>");
      } else {
        server.send(200, "application/json", "{t}{s}Uptime:{m}" + String(uptime()) + "{e}</table>");
      }
    } else if (server.hasArg("rst")) { // TODO: This should redirect to the RST page
      Log.warning(F("[WebUI] Restart" CR));
      char jsonChar[100];
      serializeJson(modules, jsonChar, measureJson(modules) + 1);
      char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

      snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Restart").c_str());
      String response = String(buffer);
      response += String(restart_script);
      response += String(script);
      response += String(style);
      snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, reset_body, jsonChar, gateway_name, "Restart");
      response += String(buffer);
      snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
      response += String(buffer);
      server.send(200, "text/html", response);

      delay(2000); // Wait for web page to be sent before

      ESPRestart(5);
    } else {
      // WEBUI_TRACE_LOG(F("Arguments %s" CR), message);
      server.send(200, "text/plain", "00:14:36.767 RSL: RESULT = {\"Topic\":\"topic\"}");
    }
  } else {
    char jsonChar[100];
    serializeJson(modules, jsonChar, measureJson(modules) + 1);

    char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Main Menu").c_str());
    String response = String(buffer);
    response += String(root_script);
    response += String(script);
    response += String(style);
    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, root_body, jsonChar, gateway_name);
    response += String(buffer);
    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
    response += String(buffer);
    server.send(200, "text/html", response);
  }
}

/**
 * @brief /CN - Configuration Page
 * 
 */
void handleCN() {
  WEBUI_SECURE
  WEBUI_TRACE_LOG(F("handleCN: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleCN Arg: %d, %s=%s" CR), i, server.argName(i).c_str(), server.arg(i).c_str());
    }
  } else {
    char jsonChar[100];
    serializeJson(modules, jsonChar, measureJson(modules) + 1);

    char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Configuration").c_str());
    String response = String(buffer);
    response += String(script);
    response += String(style);
    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_body, jsonChar, gateway_name);
    response += String(buffer);
    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
    response += String(buffer);
    server.send(200, "text/html", response);
  }
}

/**
 * @brief /WU - Configuration Page
 * T: handleWU: uri: /wu, args: 3, method: 1
 * T: handleWU Arg: 0, dm=on - displayMetric
 * T: handleWU Arg: 1, sw=on - webUISecure
 * T: handleWU Arg: 2, save=
 */
void handleWU() {
  WEBUI_TRACE_LOG(F("handleWU: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleWU Arg: %d, %s=%s" CR), i, server.argName(i).c_str(), server.arg(i).c_str());
    }
    bool update = false;

    if (displayMetric != server.hasArg("dm")) {
      update = true;
    }
    displayMetric = server.hasArg("dm");

    if (webUISecure != server.hasArg("sw")) {
      update = true;
    }
    webUISecure = server.hasArg("sw");

    if (server.hasArg("save") && update) {
      WebUIConfig_save();
    }
  }

  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);

  char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Configure WebUI").c_str());
  String response = String(buffer);
  response += String(script);
  response += String(style);
  int logLevel = Log.getLevel();
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_webui_body, jsonChar, gateway_name, (displayMetric ? "checked" : ""), (webUISecure ? "checked" : ""));
  response += String(buffer);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
  response += String(buffer);
  server.send(200, "text/html", response);
}

#if defined(ZsensorGPIOInput) && defined(GPIO_INPUT_RUNTIME_CONFIG)
String generateGPIOInputPinData() {
  String pins = "[";
  bool first = true;
#  if defined(ESP32)
  const int lastPin = 39;
#  elif defined(ESP8266)
  const int lastPin = 16;
#  else
  const int lastPin = 69;
#  endif
  for (int pin = 0; pin <= lastPin; pin++) {
    uint8_t supportedModes = 0;
    for (uint8_t candidateMode = 0; candidateMode < GPIO_INPUT_MODE_COUNT; candidateMode++) {
      if (gpioInputPinValidationError(pin, candidateMode) == nullptr)
        supportedModes |= (1U << candidateMode);
    }
    if (!supportedModes) continue;
    if (!first) pins += ',';
    pins += '[' + String(pin) + ',' + String(supportedModes) + ']';
    first = false;
  }
  pins += ']';
  return pins;
}

String generateGPIOInputModeOptions(uint8_t selectedMode) {
  String options;
  const char* labels[GPIO_INPUT_MODE_COUNT] = {
      "Driven / external resistor (INPUT)",
      "Internal pull-up (PULLUP)",
      "Internal pull-down (PULLDOWN)"};
  for (uint8_t mode = 0; mode < GPIO_INPUT_MODE_COUNT; mode++) {
    options += "<option value='" + String(mode) + "'";
    if (mode == selectedMode) options += " selected";
    options += ">" + String(labels[mode]) + "</option>";
  }
  return options;
}

String generateGPIOInputDeviceClassOptions(uint8_t selectedClass) {
  String options;
  const char* labels[GPIO_INPUT_CLASS_COUNT] = {
      "Generic", "Opening", "Door", "Garage door", "Window", "Motion",
      "Occupancy", "Moisture / leak", "Smoke", "Vibration", "Problem / fault"};
  for (uint8_t deviceClass = 0; deviceClass < GPIO_INPUT_CLASS_COUNT; deviceClass++) {
    options += "<option value='" + String(deviceClass) + "'";
    if (deviceClass == selectedClass) options += " selected";
    options += ">" + String(labels[deviceClass]) + "</option>";
  }
  return options;
}

String generateGPIOInputChannelHtml(uint8_t channel, int currentLevel) {
  String suffix = String(channel);
  const bool currentActive = gpioInputChannels[channel].enabled && currentLevel == gpioInputChannels[channel].activeLevel;
  String row;
  row.reserve(2600);
  row += "<section class='channel-card'><div class='channel-title'><b>Input " + String(channel + 1) +
         (channel == 0 ? " &middot; Primary" : "") + "</b><span class='status-chip " +
         (gpioInputChannels[channel].enabled ? (currentActive ? "active" : "idle") : "disabled") + "'>" +
         (gpioInputChannels[channel].enabled ? (String(currentLevel == HIGH ? "HIGH" : "LOW") +
                                                (currentActive ? " &middot; ACTIVE" : " &middot; idle"))
                                                 : "disabled") + "</span></div>";
  row += "<label class='toggle-row'><input type='checkbox' name='ge" + suffix + "'" +
         (gpioInputChannels[channel].enabled ? " checked" : "") + "><span>Enable this sensor</span></label>";
  row += "<div class='form-grid'><p><b>Friendly name</b><small>Used by MQTT and Home Assistant</small><input name='gn" + suffix +
         "' maxlength='" + String(GPIO_INPUT_NAME_SIZE - 1) + "' value='" +
         HtmlEscape(String(gpioInputChannels[channel].name)) + "'></p>";
  row += "<p><b>GPIO pin</b><small id='gpnote" + suffix + "'>Reserved or incompatible pins are hidden.</small><select id='gp" + suffix + "' name='gp" + suffix +
         "' data-selected='" + String(gpioInputChannels[channel].pin) + "'></select></p>";
  row += "<p><b>Electrical mode</b><small id='gh" + suffix + "' class='field-hint'></small><select id='gm" + suffix +
         "' name='gm" + suffix + "' onchange='gih(" + suffix + ")'>" +
         generateGPIOInputModeOptions(gpioInputChannels[channel].mode) + "</select></p>";
  row += "<p><b>Active when</b><small>State reported as ON in Home Assistant</small><select name='ga" + suffix + "'><option value='1'" +
         String(gpioInputChannels[channel].activeLevel == HIGH ? " selected" : "") + ">Signal is HIGH</option><option value='0'" +
         String(gpioInputChannels[channel].activeLevel == LOW ? " selected" : "") + ">Signal is LOW</option></select></p>";
  row += "<p><b>Debounce</b><small>Filters contact bounce and noisy transitions</small><span class='input-unit'><input name='gd" + suffix +
         "' type='number' min='" + String(GPIO_INPUT_DEBOUNCE_MIN) + "' max='" + String(GPIO_INPUT_DEBOUNCE_MAX) +
         "' value='" + String(gpioInputChannels[channel].debounceMs) + "'><span>ms</span></span></p>";
  row += "<p><b>MQTT state memory</b><small>Keeps the latest valid state in the broker</small><label class='toggle-row'><input type='checkbox' name='gr" + suffix + "'" +
         (gpioInputChannels[channel].retainState ? " checked" : "") + "><span>Retain last state</span></label></p>";
  row += "<p><b>Home Assistant type</b><small>Controls icon and semantic display</small><select name='gc" + suffix + "'>" +
         generateGPIOInputDeviceClassOptions(gpioInputChannels[channel].deviceClass) + "</select></p></div></section>";
  return row;
}

void handleGIRow() {
  WEBUI_SECURE
  if (!server.hasArg("c")) {
    server.send(400, "text/plain", "Missing GPIO input channel");
    return;
  }
  String channelText = server.arg("c");
  char* parseEnd = nullptr;
  long channel = strtol(channelText.c_str(), &parseEnd, 10);
  if (!channelText.length() || !parseEnd || *parseEnd != '\0' || channel < 0 || channel >= GPIO_INPUT_MAX) {
    server.send(400, "text/plain", "Invalid GPIO input channel");
    return;
  }
  const int currentLevel = gpioInputChannels[channel].enabled ? digitalRead(gpioInputChannels[channel].pin) : LOW;
  String row = generateGPIOInputChannelHtml((uint8_t)channel, currentLevel);
  Log.notice(F("[WebUI][GPIO] row channel=%u bytes=%u heap=%u" CR),
             channel + 1, row.length(), ESP.getFreeHeap());
  server.send(200, "text/html", row);
}

#  if GPIO_OUTPUT_MAX > 0
String generateGPIOOutputPinData() {
  String pins = "[";
  bool first = true;
#    if defined(ESP32)
  const int lastPin = 33;
#    elif defined(ESP8266)
  const int lastPin = 16;
#    else
  const int lastPin = 69;
#    endif
  for (int pin = 0; pin <= lastPin; pin++) {
    uint8_t supportedModes = 0;
    for (uint8_t mode = 0; mode < GPIO_OUTPUT_MODE_COUNT; mode++) {
      if (gpioOutputPinValidationError(pin, mode) == nullptr) supportedModes |= (1U << mode);
    }
    if (!supportedModes) continue;
    if (!first) pins += ',';
    pins += '[' + String(pin) + ',' + String(supportedModes) + ']';
    first = false;
  }
  pins += ']';
  return pins;
}

String generateGPIOOutputModeOptions(uint8_t selectedMode) {
  const char* labels[GPIO_OUTPUT_MODE_COUNT] = {
      "Push-pull (drives HIGH and LOW)",
      "Open-drain (LOW or released)"};
  String options;
  for (uint8_t mode = 0; mode < GPIO_OUTPUT_MODE_COUNT; mode++) {
    options += "<option value='" + String(mode) + "'";
    if (mode == selectedMode) options += " selected";
    options += ">" + String(labels[mode]) + "</option>";
  }
  return options;
}

String generateGPIOOutputStartupOptions(uint8_t selectedStartup) {
  const char* labels[GPIO_OUTPUT_STARTUP_COUNT] = {
      "Always OFF (safest)", "Always ON", "Restore last commanded state"};
  String options;
  for (uint8_t startup = 0; startup < GPIO_OUTPUT_STARTUP_COUNT; startup++) {
    options += "<option value='" + String(startup) + "'";
    if (startup == selectedStartup) options += " selected";
    options += ">" + String(labels[startup]) + "</option>";
  }
  return options;
}

String generateGPIOOutputChannelHtml(uint8_t channel) {
  String suffix = String(channel);
  const bool isOn = gpioOutputIsOn(channel);
  const int electricalLevel = gpioOutputChannels[channel].enabled ? digitalRead(gpioOutputChannels[channel].pin) : LOW;
  String row;
  row.reserve(2400);
  row += "<section class='channel-card'><div class='channel-title'><b>Output " + String(channel + 1) +
         "</b><span class='status-chip " +
         (gpioOutputChannels[channel].enabled ? (isOn ? "active" : "idle") : "disabled") + "'>" +
         (gpioOutputChannels[channel].enabled ? (String(isOn ? "ON" : "OFF") + " &middot; " +
                                                  String(electricalLevel == HIGH ? "HIGH" : "LOW"))
                                               : "disabled") + "</span></div>";
  row += "<label class='toggle-row'><input type='checkbox' name='oe" + suffix + "'" +
         (gpioOutputChannels[channel].enabled ? " checked" : "") + "><span>Enable this output and expose it to Home Assistant</span></label>";
  row += "<div class='form-grid'><p><b>Friendly name</b><small>Switch name in Home Assistant</small><input name='on" + suffix +
         "' maxlength='" + String(GPIO_OUTPUT_NAME_SIZE - 1) + "' value='" +
         HtmlEscape(String(gpioOutputChannels[channel].name)) + "'></p>";
  row += "<p><b>GPIO pin</b><small>Only output-capable pins unused by RF are listed</small><select id='op" + suffix + "' name='op" + suffix +
         "' data-selected='" + String(gpioOutputChannels[channel].pin) + "'></select></p>";
  row += "<p><b>Electrical mode</b><small id='oh" + suffix + "' class='field-hint'></small><select id='om" + suffix +
         "' name='om" + suffix + "' onchange='goh(" + suffix + ")'>" +
         generateGPIOOutputModeOptions(gpioOutputChannels[channel].mode) + "</select></p>";
  row += "<p><b>ON level</b><small>Invert this for active-low relays, LEDs or buzzers</small><select name='oa" + suffix + "'><option value='1'" +
         String(gpioOutputChannels[channel].activeLevel == HIGH ? " selected" : "") + ">HIGH means ON</option><option value='0'" +
         String(gpioOutputChannels[channel].activeLevel == LOW ? " selected" : "") + ">LOW means ON (inverted)</option></select></p>";
  row += "<p><b>State after boot</b><small>Applied before Wi-Fi and MQTT connect</small><select name='os" + suffix + "'>" +
         generateGPIOOutputStartupOptions(gpioOutputChannels[channel].startupState) + "</select></p>";
  row += "<p><b>MQTT state memory</b><small>Keeps the last reported state in the broker</small><label class='toggle-row'><input type='checkbox' name='or" + suffix + "'" +
         (gpioOutputChannels[channel].retainState ? " checked" : "") + "><span>Retain output state</span></label></p></div>";
  row += "<div class='info-box'><b>Electrical safety</b><br>ESP32 GPIOs are 3.3 V only. Open-drain needs an external pull-up and must never be pulled above 3.3 V.</div></section>";
  return row;
}

void handleGORow() {
  WEBUI_SECURE
  if (!server.hasArg("c")) {
    server.send(400, "text/plain", "Missing GPIO output channel");
    return;
  }
  String channelText = server.arg("c");
  char* parseEnd = nullptr;
  long channel = strtol(channelText.c_str(), &parseEnd, 10);
  if (!channelText.length() || !parseEnd || *parseEnd != '\0' || channel < 0 || channel >= GPIO_OUTPUT_MAX) {
    server.send(400, "text/plain", "Invalid GPIO output channel");
    return;
  }
  String row = generateGPIOOutputChannelHtml((uint8_t)channel);
  Log.notice(F("[WebUI][GPIO] output row channel=%u bytes=%u heap=%u" CR),
             channel + 1, row.length(), ESP.getFreeHeap());
  server.send(200, "text/html", row);
}
#  endif

void handleGI() {
  WEBUI_TRACE_LOG(F("handleGI: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE

  if (server.hasArg("save")) {
    GPIOInputChannelConfig_s requested[GPIO_INPUT_MAX];
#  if GPIO_OUTPUT_MAX > 0
    GPIOOutputChannelConfig_s requestedOutputs[GPIO_OUTPUT_MAX];
#  endif
    bool update = false;
    for (uint8_t channel = 0; channel < GPIO_INPUT_MAX; channel++) {
      String suffix = String(channel);
      requested[channel].enabled = server.hasArg("ge" + suffix);

      String modeText = server.arg("gm" + suffix);
      char* modeParseEnd = nullptr;
      long mode = strtol(modeText.c_str(), &modeParseEnd, 10);
      if (!modeText.length() || !modeParseEnd || *modeParseEnd != '\0' ||
          mode < 0 || mode >= GPIO_INPUT_MODE_COUNT) {
        server.send(400, "text/plain", "Invalid electrical mode for input " + String(channel + 1));
        return;
      }
      requested[channel].mode = (uint8_t)mode;

      String pinText = server.arg("gp" + suffix);
      char* parseEnd = nullptr;
      long pin = strtol(pinText.c_str(), &parseEnd, 10);
      if (!pinText.length() || !parseEnd || *parseEnd != '\0' || pin < 0 || pin > 255) {
        server.send(400, "text/plain", "Invalid GPIO number for input " + String(channel + 1));
        return;
      }
      const char* pinError = gpioInputPinValidationError((int)pin, requested[channel].mode);
      if (pinError) {
        server.send(400, "text/plain", "Invalid GPIO for input " + String(channel + 1) + ": " + pinError);
        return;
      }
      requested[channel].pin = (uint8_t)pin;

      String requestedName = server.arg("gn" + suffix);
      requestedName.trim();
      if (!requestedName.length()) requestedName = channel == 0 ? "GPIOInput" : "GPIO Input " + String(channel + 1);
      if (requestedName.length() >= GPIO_INPUT_NAME_SIZE) {
        server.send(400, "text/plain", "Name too long for input " + String(channel + 1));
        return;
      }
      strncpy(requested[channel].name, requestedName.c_str(), sizeof(requested[channel].name) - 1);
      requested[channel].name[sizeof(requested[channel].name) - 1] = '\0';

      String activeText = server.arg("ga" + suffix);
      if (activeText != "0" && activeText != "1") {
        server.send(400, "text/plain", "Invalid active level for input " + String(channel + 1));
        return;
      }
      requested[channel].activeLevel = activeText == "1" ? HIGH : LOW;

      String debounceText = server.arg("gd" + suffix);
      char* debounceParseEnd = nullptr;
      long debounce = strtol(debounceText.c_str(), &debounceParseEnd, 10);
      if (!debounceText.length() || !debounceParseEnd || *debounceParseEnd != '\0' ||
          debounce < GPIO_INPUT_DEBOUNCE_MIN || debounce > GPIO_INPUT_DEBOUNCE_MAX) {
        server.send(400, "text/plain", "Debounce for input " + String(channel + 1) +
                                       " must be between " + String(GPIO_INPUT_DEBOUNCE_MIN) +
                                       " and " + String(GPIO_INPUT_DEBOUNCE_MAX) + " ms");
        return;
      }
      requested[channel].debounceMs = (uint16_t)debounce;
      requested[channel].retainState = server.hasArg("gr" + suffix);

      String classText = server.arg("gc" + suffix);
      char* classParseEnd = nullptr;
      long deviceClass = strtol(classText.c_str(), &classParseEnd, 10);
      if (!classText.length() || !classParseEnd || *classParseEnd != '\0' ||
          deviceClass < 0 || deviceClass >= GPIO_INPUT_CLASS_COUNT) {
        server.send(400, "text/plain", "Invalid device class for input " + String(channel + 1));
        return;
      }
      requested[channel].deviceClass = (uint8_t)deviceClass;

      if (requested[channel].enabled) {
        for (uint8_t previous = 0; previous < channel; previous++) {
          if (requested[previous].enabled && requested[previous].pin == requested[channel].pin) {
            server.send(400, "text/plain", "GPIO " + String(pin) + " is enabled more than once");
            return;
          }
        }
      }

      if (requested[channel].enabled != gpioInputChannels[channel].enabled ||
          requested[channel].pin != gpioInputChannels[channel].pin ||
          strcmp(requested[channel].name, gpioInputChannels[channel].name) != 0 ||
          requested[channel].mode != gpioInputChannels[channel].mode ||
          requested[channel].activeLevel != gpioInputChannels[channel].activeLevel ||
          requested[channel].debounceMs != gpioInputChannels[channel].debounceMs ||
          requested[channel].retainState != gpioInputChannels[channel].retainState ||
          requested[channel].deviceClass != gpioInputChannels[channel].deviceClass) {
        update = true;
      }
    }

#  if GPIO_OUTPUT_MAX > 0
    for (uint8_t channel = 0; channel < GPIO_OUTPUT_MAX; channel++) {
      String suffix = String(channel);
      requestedOutputs[channel].enabled = server.hasArg("oe" + suffix);

      String modeText = server.arg("om" + suffix);
      char* modeEnd = nullptr;
      long mode = strtol(modeText.c_str(), &modeEnd, 10);
      if (!modeText.length() || !modeEnd || *modeEnd != '\0' || mode < 0 || mode >= GPIO_OUTPUT_MODE_COUNT) {
        server.send(400, "text/plain", "Invalid electrical mode for output " + String(channel + 1));
        return;
      }
      requestedOutputs[channel].mode = (uint8_t)mode;

      String pinText = server.arg("op" + suffix);
      char* pinEnd = nullptr;
      long pin = strtol(pinText.c_str(), &pinEnd, 10);
      if (!pinText.length() || !pinEnd || *pinEnd != '\0' || pin < 0 || pin > 255) {
        server.send(400, "text/plain", "Invalid GPIO number for output " + String(channel + 1));
        return;
      }
      const char* pinError = gpioOutputPinValidationError((int)pin, requestedOutputs[channel].mode);
      if (pinError) {
        server.send(400, "text/plain", "Invalid GPIO for output " + String(channel + 1) + ": " + pinError);
        return;
      }
      requestedOutputs[channel].pin = (uint8_t)pin;

      String requestedName = server.arg("on" + suffix);
      requestedName.trim();
      if (!requestedName.length()) requestedName = "GPIO Output " + String(channel + 1);
      if (requestedName.length() >= GPIO_OUTPUT_NAME_SIZE) {
        server.send(400, "text/plain", "Name too long for output " + String(channel + 1));
        return;
      }
      strncpy(requestedOutputs[channel].name, requestedName.c_str(), sizeof(requestedOutputs[channel].name) - 1);
      requestedOutputs[channel].name[sizeof(requestedOutputs[channel].name) - 1] = '\0';

      String activeText = server.arg("oa" + suffix);
      if (activeText != "0" && activeText != "1") {
        server.send(400, "text/plain", "Invalid ON level for output " + String(channel + 1));
        return;
      }
      requestedOutputs[channel].activeLevel = activeText == "1" ? HIGH : LOW;

      String startupText = server.arg("os" + suffix);
      char* startupEnd = nullptr;
      long startup = strtol(startupText.c_str(), &startupEnd, 10);
      if (!startupText.length() || !startupEnd || *startupEnd != '\0' || startup < 0 || startup >= GPIO_OUTPUT_STARTUP_COUNT) {
        server.send(400, "text/plain", "Invalid startup state for output " + String(channel + 1));
        return;
      }
      requestedOutputs[channel].startupState = (uint8_t)startup;
      requestedOutputs[channel].retainState = server.hasArg("or" + suffix);

      if (requestedOutputs[channel].enabled) {
        for (uint8_t input = 0; input < GPIO_INPUT_MAX; input++) {
          if (requested[input].enabled && requested[input].pin == requestedOutputs[channel].pin) {
            server.send(400, "text/plain", "GPIO " + String(pin) + " is already used by input " + String(input + 1));
            return;
          }
        }
        for (uint8_t previous = 0; previous < channel; previous++) {
          if (requestedOutputs[previous].enabled && requestedOutputs[previous].pin == requestedOutputs[channel].pin) {
            server.send(400, "text/plain", "GPIO " + String(pin) + " is enabled on more than one output");
            return;
          }
        }
      }

      if (requestedOutputs[channel].enabled != gpioOutputChannels[channel].enabled ||
          requestedOutputs[channel].pin != gpioOutputChannels[channel].pin ||
          strcmp(requestedOutputs[channel].name, gpioOutputChannels[channel].name) != 0 ||
          requestedOutputs[channel].mode != gpioOutputChannels[channel].mode ||
          requestedOutputs[channel].activeLevel != gpioOutputChannels[channel].activeLevel ||
          requestedOutputs[channel].startupState != gpioOutputChannels[channel].startupState ||
          requestedOutputs[channel].retainState != gpioOutputChannels[channel].retainState) {
        update = true;
      }
    }
#  endif

    if (update) {
      for (uint8_t channel = 0; channel < GPIO_INPUT_MAX; channel++) {
        gpioInputChannels[channel] = requested[channel];
        Log.notice(F("[WebUI][GPIO] saving channel=%u enabled=%T name=%s pin=%u mode=%s active=%s debounce_ms=%u retain=%T class=%s" CR),
                   channel + 1, gpioInputChannels[channel].enabled,
                   gpioInputChannels[channel].name, gpioInputChannels[channel].pin,
                   gpioInputModeName(gpioInputChannels[channel].mode),
                   gpioInputChannels[channel].activeLevel == HIGH ? "HIGH" : "LOW",
                   gpioInputChannels[channel].debounceMs,
                   gpioInputChannels[channel].retainState,
                   gpioInputDeviceClassName(gpioInputChannels[channel].deviceClass));
      }
#  if GPIO_OUTPUT_MAX > 0
      for (uint8_t channel = 0; channel < GPIO_OUTPUT_MAX; channel++) {
        gpioOutputChannels[channel] = requestedOutputs[channel];
        Log.notice(F("[WebUI][GPIO] saving output channel=%u enabled=%T name=%s pin=%u mode=%s active=%s startup=%s retain=%T" CR),
                   channel + 1, gpioOutputChannels[channel].enabled,
                   gpioOutputChannels[channel].name, gpioOutputChannels[channel].pin,
                   gpioOutputModeName(gpioOutputChannels[channel].mode),
                   gpioOutputChannels[channel].activeLevel == HIGH ? "HIGH" : "LOW",
                   gpioOutputStartupName(gpioOutputChannels[channel].startupState),
                   gpioOutputChannels[channel].retainState);
      }
#  endif
#  ifndef ESPWifiManualSetup
      saveConfig();
#  endif

      char jsonChar[100];
      serializeJson(modules, jsonChar, measureJson(modules) + 1);
      char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];
      snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Save GPIO").c_str());
      String response = String(buffer) + String(restart_script) + String(script) + String(style);
      snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, reset_body, jsonChar, gateway_name, "GPIO input/output configuration saved");
      response += String(buffer);
      snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
      response += String(buffer);
      server.send(200, "text/html", response);
      delay(2000);
      ESPRestart(7);
      return;
    }
    Log.notice(F("[WebUI][GPIO] no configuration changes" CR));
  }

  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);
  char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Configure GPIO").c_str());
  String pageHeader = String(buffer);
  String gpioScript = "<script>var gpp=" + generateGPIOInputPinData() +
#  if GPIO_OUTPUT_MAX > 0
                      ",gop=" + generateGPIOOutputPinData() +
#  endif
                      ";function gpfill(id,data,selected){var p=document.getElementById(id);for(var e of data){var o=document.createElement('option');o.value=e[0];o.dataset.modes=e[1];o.textContent='GPIO '+e[0];if(e[0]===selected)o.selected=true;p.appendChild(o);}}function gip(i){gpfill('gp'+i,gpp,Number(document.getElementById('gp'+i).dataset.selected));}function gih(i){var m=document.getElementById('gm'+i).value,h=document.getElementById('gh'+i),p=document.getElementById('gp'+i),moved=false,bit=1<<Number(m);h.textContent=m==='1'?'Connect the contact between GPIO and GND. Open is normally HIGH.':m==='2'?'Connect the contact between GPIO and 3.3 V. Open is normally LOW.':'Use for a driven 0-3.3 V signal or an external resistor.';for(var o of p.options)o.disabled=!(Number(o.dataset.modes)&bit);if(p.selectedOptions[0]&&p.selectedOptions[0].disabled){for(var o of p.options)if(!o.disabled){o.selected=true;moved=true;break;}}document.getElementById('gpnote'+i).textContent=moved?'Pin changed automatically: the previous GPIO does not support this mode.':'Reserved or incompatible pins are hidden.';}"
#  if GPIO_OUTPUT_MAX > 0
                      "function gopfill(i){gpfill('op'+i,gop,Number(document.getElementById('op'+i).dataset.selected));}function goh(i){var m=document.getElementById('om'+i).value,h=document.getElementById('oh'+i),p=document.getElementById('op'+i),bit=1<<Number(m);h.textContent=m==='1'?'The pin can pull LOW or release the line; add an external pull-up.':'The pin actively drives both HIGH and LOW.';for(var o of p.options)o.disabled=!(Number(o.dataset.modes)&bit);}"
#  endif
                      "async function gil(){var d=document.getElementById('girows');d.innerHTML='';try{for(var i=0;i<" + String(GPIO_INPUT_MAX) + ";i++){var r=await fetch('gi-row?c='+i,{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);d.insertAdjacentHTML('beforeend',await r.text());gip(i);gih(i);}}catch(e){d.innerHTML=\"<div class='info-box'>Unable to load input settings: \"+e.message+\". Reload this page.</div>\";}"
#  if GPIO_OUTPUT_MAX > 0
                      "var o=document.getElementById('gorows');o.innerHTML='';try{for(var i=0;i<" + String(GPIO_OUTPUT_MAX) + ";i++){var r=await fetch('go-row?c='+i,{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);o.insertAdjacentHTML('beforeend',await r.text());gopfill(i);goh(i);}}catch(e){o.innerHTML=\"<div class='info-box'>Unable to load output settings: \"+e.message+\". Reload this page.</div>\";}"
#  endif
                      "}window.addEventListener('load',gil);</script>";
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, body_header, jsonChar, gateway_name);
  String bodyHeader = String(buffer);
  static const char gpioFieldset[] PROGMEM = "<fieldset class='set1'><legend><span><b>GPIO inputs and outputs</b></span></legend>";
  String intro = "<form method='post' action='gi'><div class='info-box'><b>Two sensors, two controllable outputs</b><br>The first input keeps its existing MQTT and Home Assistant identity. Enabled outputs become independent Home Assistant switches for a relay input, LED, active buzzer or another 3.3 V logic load. Pin conflicts are rejected and changes apply after restart.</div><h3>Digital inputs</h3><div id='girows'><div class='info-box'>Loading input settings...</div></div>"
#  if GPIO_OUTPUT_MAX > 0
                 "<h3>Digital outputs</h3><div id='gorows'><div class='info-box'>Loading output settings...</div></div>"
#  endif
                 ;
  static const char gpioSaveButton[] PROGMEM = "<br><button name='save' type='submit' class='button bgrn'>Save and restart</button></form></fieldset>";
  static const char gpioFooterMenu[] PROGMEM = body_footer_config_menu;
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
  String pageFooter = String(buffer);

  size_t contentLength = pageHeader.length() + strlen_P(script) + strlen_P(style) + gpioScript.length() +
                         bodyHeader.length() + strlen_P(gpioFieldset) + intro.length() +
                         strlen_P(gpioSaveButton) + strlen_P(gpioFooterMenu) + pageFooter.length();

  Log.notice(F("[WebUI][GPIO] response bytes=%u heap=%u max_alloc=%u" CR),
             contentLength, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  server.setContentLength(contentLength);
  server.send(200, "text/html", "");
  server.sendContent(pageHeader);
  server.sendContent_P(script);
  server.sendContent_P(style);
  server.sendContent(gpioScript);
  server.sendContent(bodyHeader);
  server.sendContent_P(gpioFieldset);
  server.sendContent(intro);
  server.sendContent_P(gpioSaveButton);
  server.sendContent_P(gpioFooterMenu);
  server.sendContent(pageFooter);
}
#endif

#if defined(ZgatewayBT) || defined(ZgatewayBLETracker)
String generateBLETrackerRowHtml(uint8_t slot) {
  BLETrackerConfig_s tracker = getBLETrackerConfig(slot);
  String suffix = String(slot);
  String status = !tracker.enabled ? "disabled" : (tracker.present ? "present" : "away");
  String chipClass = !tracker.enabled ? "" : (tracker.present ? "active" : "idle");
  String row;
  row.reserve(1536);
  row += "<section class='channel-card'><div class='channel-title'><b>BLE device " + String(slot + 1) +
         "</b><span class='status-chip " + chipClass + "'>" + status + "</span></div>";
  row += "<label class='toggle-row'><input type='checkbox' name='be" + suffix + "'" +
         String(tracker.enabled ? " checked" : "") + "><span>Expose this device in Home Assistant</span></label>";
  row += "<div class='form-grid'><p><b>Friendly name</b><small>Shown in Home Assistant</small><input name='bn" + suffix +
         "' maxlength='32' value='" + HtmlEscape(String(tracker.name)) + "' placeholder='Keys, phone, beacon...'></p>";
  row += "<p><b>BLE MAC address</b><small>Choose a suggestion or enter a fixed MAC</small><input name='bm" + suffix +
         "' list='ble-candidates' maxlength='17' pattern='[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}' value='" +
         HtmlEscape(String(tracker.mac)) + "' placeholder='AA:BB:CC:DD:EE:FF'></p>";
  row += "<p><b>Away timeout</b><small>Time without advertisements before reporting away</small><span class='input-unit'><input name='bt" + suffix +
         "' type='number' min='5' max='86400' value='" + String(tracker.timeoutSeconds) + "'><span>s</span></span></p>";
  row += "<p><b>Minimum signal</b><small>Lower values accept weaker signals (for example, -95 reaches farther than -80)</small><span class='input-unit'><input name='br" + suffix +
         "' type='number' min='-100' max='-20' value='" + String(tracker.minRssi) + "'><span>dBm</span></span></p></div>";
  if (tracker.enabled && tracker.present && tracker.lastRssi > -127) {
    row += "<small>Last RSSI: " + String(tracker.lastRssi) + " dBm &middot; last seen at uptime " + String(tracker.lastSeen / 1000UL) + " s</small>";
  } else if (tracker.enabled) {
    row += "<small>Not currently detected";
    if (tracker.rawMatches) {
      row += " &middot; matching MAC packets: " + String(tracker.rawMatches) +
             " &middot; last raw RSSI: " + String(tracker.lastRawRssi) + " dBm";
      if (tracker.rssiRejected)
        row += " &middot; below threshold: " + String(tracker.rssiRejected);
    }
    row += "</small>";
  }
  row += "</section>";
  return row;
}

void handleBTRow() {
  WEBUI_SECURE
  if (!server.hasArg("c")) {
    server.send(400, "text/plain", "Missing BLE slot");
    return;
  }
  int slot = server.arg("c").toInt();
  if (slot < 0 || slot >= BLE_TRACKER_MAX) {
    server.send(400, "text/plain", "Invalid BLE slot");
    return;
  }
  String row = generateBLETrackerRowHtml(slot);
  Log.verbose(F("[WebUI][BLE] row=%u bytes=%u heap=%u max_alloc=%u" CR),
             slot + 1, row.length(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  server.setContentLength(row.length());
  server.send(200, "text/html", row);
}

void handleBTCandidates() {
  WEBUI_SECURE
  String candidates = getBLETrackerCandidatesHtml();
  Log.verbose(F("[WebUI][BLE] candidates bytes=%u heap=%u max_alloc=%u" CR),
             candidates.length(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  server.setContentLength(candidates.length());
  server.send(200, "text/html", candidates);
}

void handleBTTrackers() {
  WEBUI_TRACE_LOG(F("handleBTTrackers: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE

  String notice;
  if (server.hasArg("save")) {
    struct RequestedTracker {
      bool enabled;
      String mac;
      String name;
      uint32_t timeout;
      int minRssi;
    } requested[BLE_TRACKER_MAX];

    for (uint8_t slot = 0; slot < BLE_TRACKER_MAX; slot++) {
      String suffix = String(slot);
      requested[slot].enabled = server.hasArg("be" + suffix);
      requested[slot].mac = server.arg("bm" + suffix);
      requested[slot].mac.trim();
      requested[slot].name = server.arg("bn" + suffix);
      requested[slot].name.trim();
      requested[slot].timeout = server.arg("bt" + suffix).toInt();
      requested[slot].minRssi = server.arg("br" + suffix).toInt();

      if (requested[slot].enabled && !isValidBLETrackerMac(requested[slot].mac.c_str())) {
        server.send(400, "text/plain", "Invalid BLE MAC for slot " + String(slot + 1) + ". Use AA:BB:CC:DD:EE:FF");
        return;
      }
      if (requested[slot].name.length() > 32) {
        server.send(400, "text/plain", "Name too long for BLE slot " + String(slot + 1));
        return;
      }
      if (requested[slot].timeout < 5 || requested[slot].timeout > 86400) {
        server.send(400, "text/plain", "Timeout for BLE slot " + String(slot + 1) + " must be between 5 and 86400 seconds");
        return;
      }
      if (requested[slot].minRssi < -100 || requested[slot].minRssi > -20) {
        server.send(400, "text/plain", "RSSI threshold for BLE slot " + String(slot + 1) + " must be between -100 and -20 dBm");
        return;
      }
      if (requested[slot].enabled) {
        for (uint8_t previous = 0; previous < slot; previous++) {
          if (requested[previous].enabled && requested[previous].mac.equalsIgnoreCase(requested[slot].mac)) {
            server.send(400, "text/plain", "The same BLE MAC is enabled in more than one slot");
            return;
          }
        }
      }
    }

    for (uint8_t slot = 0; slot < BLE_TRACKER_MAX; slot++) {
      if (!configureBLETracker(slot, requested[slot].enabled, requested[slot].mac.c_str(), requested[slot].name.c_str(),
                               requested[slot].timeout, requested[slot].minRssi)) {
        server.send(500, "text/plain", "Unable to update BLE slot " + String(slot + 1));
        return;
      }
    }
    saveBLETrackerConfig();
    launchBTDiscovery(false);
    notice = "<div class='info-box'><b>Saved.</b> No restart is required. Home Assistant discovery and retained presence states have been refreshed.</div>";
    Log.verbose(F("[WebUI][BLE] tracker configuration saved" CR));
  }

  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);
  // These formatted fragments are below 768 bytes. Keeping the general 3 KB
  // WebUI buffer on this handler's stack leaves too little stack headroom.
  constexpr size_t BLE_WEB_FRAGMENT_BUFFER_SIZE = 768;
  char buffer[BLE_WEB_FRAGMENT_BUFFER_SIZE];
  const uint32_t heapBeforePage = ESP.getFreeHeap();
  snprintf(buffer, sizeof(buffer), header_html, (String(gateway_name) + " - BLE presence").c_str());
  String pageHeader = String(buffer);
  String btScript = "<script>async function btl(){var d=document.getElementById('brows');d.innerHTML='';try{var c=await fetch('bt-candidates',{cache:'no-store'});if(c.ok)document.getElementById('ble-candidates').innerHTML=await c.text();for(var i=0;i<" + String(BLE_TRACKER_MAX) + ";i++){var r=await fetch('bt-row?c='+i,{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);d.insertAdjacentHTML('beforeend',await r.text());}}catch(e){d.innerHTML=\"<div class='info-box'>Unable to load BLE settings: \"+e.message+\". Reload this page.</div>\";}}window.addEventListener('load',btl);</script>";
  snprintf(buffer, sizeof(buffer), body_header, jsonChar, gateway_name);
  String bodyHeader = String(buffer);
  static const char btFieldset[] PROGMEM = "<fieldset class='set1'><legend><span><b>BLE presence in Home Assistant</b></span></legend>";
  String intro = notice + "<div class='info-box'>Enable only the devices you want to expose. Each slot creates a presence binary sensor and an RSSI sensor in Home Assistant. The presence changes to away after the timeout.</div><div class='info-box'><b>Nearby devices:</b> MAC addresses already seen appear as suggestions while typing. A device that randomizes its BLE MAC cannot be followed reliably by MAC.</div><form method='post' action='bt'><datalist id='ble-candidates'></datalist><div id='brows'><div class='info-box'>Loading BLE settings...</div></div>";
  static const char btSaveButton[] PROGMEM = "<br><button name='save' type='submit' class='button bgrn'>Save BLE devices</button></form></fieldset>";
  static const char btFooterMenu[] PROGMEM = body_footer_config_menu;
  snprintf(buffer, sizeof(buffer), footer, OMG_VERSION);
  String pageFooter = String(buffer);

  size_t contentLength = pageHeader.length() + strlen_P(script) + strlen_P(style) + btScript.length() +
                         bodyHeader.length() + strlen_P(btFieldset) + intro.length() +
                         strlen_P(btSaveButton) + strlen_P(btFooterMenu) + pageFooter.length();

  server.setContentLength(contentLength);
  server.send(200, "text/html", "");
  server.sendContent(pageHeader);
  server.sendContent_P(script);
  server.sendContent_P(style);
  server.sendContent(btScript);
  server.sendContent(bodyHeader);
  server.sendContent_P(btFieldset);
  server.sendContent(intro);
  server.sendContent_P(btSaveButton);
  server.sendContent_P(btFooterMenu);
  server.sendContent(pageFooter);

  Log.verbose(F("[WebUI][BLE] shell bytes=%u heap_before=%u heap_after=%u min_heap=%u max_alloc=%u" CR),
             contentLength, heapBeforePage, ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
}
#endif

/**
 * @brief /WI - Configure WiFi Page
 * T: handleWI: uri: /wi, args: 4, method: 1
 * T: handleWI Arg: 0, s1=SSID
 * T: handleWI Arg: 1, p1=xxxxxx
 * T: handleWI Arg: 3, save=
 */
void handleWI() {
  WEBUI_TRACE_LOG(F("handleWI: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  String WiFiScan = "";
  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleWI Arg: %d, %s=%s" CR), i, server.argName(i).c_str(),
                      server.argName(i) == "p1" ? "[redacted]" : server.arg(i).c_str());
    }
    if (server.hasArg("scan")) {
      bool limitScannedNetworks = true;
      int n = WiFi.scanNetworks();

      WEBUI_TRACE_LOG(F("handleWI scan: found %d" CR), n);
      if (0 == n) {
        // WSContentSend_P(PSTR(D_NO_NETWORKS_FOUND));
        // limitScannedNetworks = false; // in order to show D_SCAN_FOR_WIFI_NETWORKS
      } else {
        //sort networks
        int indices[n];
        for (uint32_t i = 0; i < n; i++) {
          indices[i] = i;
        }

        // RSSI SORT
        for (uint32_t i = 0; i < n; i++) {
          for (uint32_t j = i + 1; j < n; j++) {
            if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
              std::swap(indices[i], indices[j]);
            }
          }
        }

        uint32_t networksToShow = n;
        if ((limitScannedNetworks) && (networksToShow > MAX_WIFI_NETWORKS_TO_SHOW)) {
          networksToShow = MAX_WIFI_NETWORKS_TO_SHOW;
        }

        for (uint32_t i = 0; i < n; i++) {
          if (-1 == indices[i]) {
            continue;
          }
          String cssid = WiFi.SSID(indices[i]);
          uint32_t cschn = WiFi.channel(indices[i]);
          for (uint32_t j = i + 1; j < n; j++) {
            if ((cssid == WiFi.SSID(indices[j])) && (cschn == WiFi.channel(indices[j]))) {
              WEBUI_TRACE_LOG(F("handleWI scan: duplicate %s" CR), WiFi.SSID(indices[j]).c_str());
              indices[j] = -1; // set dup aps to index -1
            }
          }
        }

        //display networks in page
        for (uint32_t i = 0; i < networksToShow; i++) {
          if (-1 == indices[i]) {
            continue;
          } // skip dups
          int32_t rssi = WiFi.RSSI(indices[i]);
          WEBUI_TRACE_LOG(F("D_LOG_WIFI D_SSID  %s, D_BSSID  %s,  D_CHANNEL  %d,  D_RSSI  %d" CR),
                          WiFi.SSID(indices[i]).c_str(), WiFi.BSSIDstr(indices[i]).c_str(), WiFi.channel(indices[i]), rssi);
          int quality = WifiGetRssiAsQuality(rssi);
          String ssid_copy = WiFi.SSID(indices[i]);
          if (!ssid_copy.length()) {
            ssid_copy = F("no_name");
          }

          WiFiScan += "<div><a href='#p' onclick='c(this)'>" + HtmlEscape(ssid_copy) + "</a>&nbsp;(" + WiFi.channel(indices[i]) + ")&nbsp<span class='q'>" + quality + "% (" + rssi + " dBm)</span></div>";
        }
      }
      WEBUI_TRACE_LOG(F("handleWI scan: results %s" CR), WiFiScan.c_str());

      char jsonChar[100];
      serializeJson(modules, jsonChar, measureJson(modules) + 1);

      char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

      snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Configure WiFi").c_str());
      String response = String(buffer);
      response += String(wifi_script);
      response += String(script);
      response += String(style);
      snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_wifi_body, jsonChar, gateway_name, WiFiScan.c_str(), WiFi.SSID().c_str());
      response += String(buffer);
      snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
      response += String(buffer);
      server.send(200, "text/html", response);
      return;

    } else if (server.hasArg("save")) {
      StaticJsonDocument<JSON_MSG_BUFFER> WEBtoSYSBuffer;
      JsonObject WEBtoSYS = WEBtoSYSBuffer.to<JsonObject>();
      bool update = false;
      if (server.hasArg("s1")) {
        WEBtoSYS["wifi_ssid"] = server.arg("s1");
        if (strncmp((char*)WiFi.SSID().c_str(), server.arg("s1").c_str(), parameters_size)) {
          update = true;
        }
      }
      if (server.hasArg("p1")) {
        WEBtoSYS["wifi_pass"] = server.arg("p1");
        if (strncmp((char*)WiFi.psk().c_str(), server.arg("p1").c_str(), parameters_size)) {
          update = true;
        }
      }
      if (update) {
        String topic = String(mqtt_topic) + String(gateway_name) + String(subjectMQTTtoSYSset);
        Log.warning(F("[WebUI] Save WiFi and Restart" CR));
        char jsonChar[100];
        serializeJson(modules, jsonChar, measureJson(modules) + 1);
        char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

        snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Save WiFi and Restart").c_str());
        String response = String(buffer);
        response += String(restart_script);
        response += String(script);
        response += String(style);
        snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, reset_body, jsonChar, gateway_name, "Save WiFi and Restart");
        response += String(buffer);
        snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
        response += String(buffer);
        server.send(200, "text/html", response);

        delay(2000); // Wait for web page to be sent before
        XtoSYS((char*)topic.c_str(), WEBtoSYS);
        return;
      } else {
        Log.warning(F("[WebUI] No changes" CR));
      }
    }
  }
  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);

  char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Configure WiFi").c_str());
  String response = String(buffer);
  response += String(wifi_script);
  response += String(script);
  response += String(style);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_wifi_body, jsonChar, gateway_name, WiFiScan.c_str(), WiFi.SSID().c_str());
  response += String(buffer);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
  response += String(buffer);
  server.send(200, "text/html", response);
}

/**
 * @brief /MQ - Configure MQTT Page
 * T: handleMQ: uri: /mq, args: 8, method: 1
 * T: handleMQ Arg: 0, mh=192.168.1.11
 * T: handleMQ Arg: 1, ml=1883
 * T: handleMQ Arg: 2, mu=1234
 * T: handleMQ Arg: 3, mp= 
 * T: handleMQ Arg: 4, sc=on
 * T: handleMQ Arg: 5, h=
 * T: handleMQ Arg: 6, mt=home/
 * T: handleMQ Arg: 7 dp=homeassistant (#ifdef ZmqttDiscovery)
 * T: handleMQ Arg: 8, save=
 */
void handleMQ() {
  WEBUI_TRACE_LOG(F("handleMQ: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleMQ Arg: %d, %s=%s" CR), i, server.argName(i).c_str(),
                      server.argName(i) == "mp" ? "[redacted]" : server.arg(i).c_str());
    }
    if (server.hasArg("save")) {
      StaticJsonDocument<JSON_MSG_BUFFER> WEBtoSYSBuffer;
      JsonObject WEBtoSYS = WEBtoSYSBuffer.to<JsonObject>();
      bool update = false;
      bool mqttConnectionUpdate = false;

#  if !MQTT_BROKER_MODE
      if (server.hasArg("mh")) {
        WEBtoSYS["mqtt_server"] = server.arg("mh");
        if (strncmp(cnt_parameters_array[CNT_DEFAULT_INDEX].mqtt_server, server.arg("mh").c_str(), parameters_size)) {
          update = true;
        }
      }

      if (server.hasArg("ml")) {
        WEBtoSYS["mqtt_port"] = server.arg("ml");
        if (strncmp(cnt_parameters_array[CNT_DEFAULT_INDEX].mqtt_port, server.arg("ml").c_str(), 6)) {
          update = true;
        }
      }

      if (server.hasArg("mu")) {
        WEBtoSYS["mqtt_user"] = server.arg("mu");
        if (strncmp(cnt_parameters_array[CNT_DEFAULT_INDEX].mqtt_user, server.arg("mu").c_str(), parameters_size)) {
          update = true;
        }
      }

      // An empty password means "keep the saved password".
      if (server.hasArg("mp") && server.arg("mp").length() > 0) {
        WEBtoSYS["mqtt_pass"] = server.arg("mp");
        if (strncmp(cnt_parameters_array[CNT_DEFAULT_INDEX].mqtt_pass, server.arg("mp").c_str(), parameters_size)) {
          update = true;
        }
      }

      // SC - Secure Connection argument is only present when true
      if (cnt_parameters_array[CNT_DEFAULT_INDEX].isConnectionSecure != server.hasArg("sc")) {
        update = true;
      }
      WEBtoSYS["mqtt_secure"] = server.hasArg("sc");

      mqttConnectionUpdate = update;

      if (!update) {
        Log.warning(F("[WebUI] clearing" CR));
        for (JsonObject::iterator it = WEBtoSYS.begin(); it != WEBtoSYS.end(); ++it) {
          WEBtoSYS.remove(it);
        }
      }
#  endif

      if (server.hasArg("h")) {
        WEBtoSYS["gateway_name"] = server.arg("h");
        if (strncmp(gateway_name, server.arg("h").c_str(), parameters_size)) {
          update = true;
          mqttConnectionUpdate = true;
        }
      }

      if (server.hasArg("mt")) {
        WEBtoSYS["mqtt_topic"] = server.arg("mt");
        if (strncmp(mqtt_topic, server.arg("mt").c_str(), parameters_size)) {
          update = true;
          mqttConnectionUpdate = true;
        }
      }
#  ifdef ZmqttDiscovery
      if (server.hasArg("dp")) {
        WEBtoSYS["discovery_prefix"] = server.arg("dp");
        if (strncmp(discovery_prefix, server.arg("dp").c_str(), parameters_size)) {
          update = true;
          mqttConnectionUpdate = true;
        }
      }
#  endif

#  if defined(MQTT_WOL_ENABLED) && !MQTT_BROKER_MODE
      bool requestedWOLEnabled = server.hasArg("we");
      bool requestedWOLTransport = server.hasArg("wt");
      bool requestedWOLBroker = server.hasArg("wb");
      bool requestedWOLAuth = server.hasArg("wa");
      String requestedWOLMac = server.hasArg("wm") ? server.arg("wm") : String(mqttWOLConfig.mac);
      requestedWOLMac.trim();
      byte parsedWOLMac[6];
      if ((requestedWOLEnabled || requestedWOLMac.length()) && !mqttWOLParseMAC(requestedWOLMac.c_str(), parsedWOLMac)) {
        server.send(400, "text/plain", "Invalid Wake-on-LAN MAC address");
        return;
      }

      unsigned long requestedWOLDelaySeconds = server.hasArg("wd") ? strtoul(server.arg("wd").c_str(), nullptr, 10) : mqttWOLConfig.initialDelayMs / 1000UL;
      unsigned long requestedWOLRepeatSeconds = server.hasArg("wr") ? strtoul(server.arg("wr").c_str(), nullptr, 10) : mqttWOLConfig.repeatIntervalMs / 1000UL;
      unsigned long requestedWOLFailures = server.hasArg("wf") ? strtoul(server.arg("wf").c_str(), nullptr, 10) : mqttWOLConfig.minFailures;
      if (requestedWOLDelaySeconds > 86400UL) requestedWOLDelaySeconds = 86400UL;
      if (requestedWOLRepeatSeconds > 86400UL) requestedWOLRepeatSeconds = 86400UL;
      if (requestedWOLFailures < 1UL) requestedWOLFailures = 1UL;
      if (requestedWOLFailures > 1000UL) requestedWOLFailures = 1000UL;

      WEBtoSYS["mqtt_wol_enabled"] = requestedWOLEnabled;
      WEBtoSYS["mqtt_wol_mac"] = requestedWOLMac;
      WEBtoSYS["mqtt_wol_delay_s"] = requestedWOLDelaySeconds;
      WEBtoSYS["mqtt_wol_failures"] = requestedWOLFailures;
      WEBtoSYS["mqtt_wol_repeat_s"] = requestedWOLRepeatSeconds;
      WEBtoSYS["mqtt_wol_transport"] = requestedWOLTransport;
      WEBtoSYS["mqtt_wol_broker"] = requestedWOLBroker;
      WEBtoSYS["mqtt_wol_auth"] = requestedWOLAuth;

      if (mqttWOLConfig.enabled != requestedWOLEnabled || strcmp(mqttWOLConfig.mac, requestedWOLMac.c_str()) != 0 ||
          mqttWOLConfig.initialDelayMs != requestedWOLDelaySeconds * 1000UL || mqttWOLConfig.minFailures != requestedWOLFailures ||
          mqttWOLConfig.repeatIntervalMs != requestedWOLRepeatSeconds * 1000UL || mqttWOLConfig.onTransportError != requestedWOLTransport ||
          mqttWOLConfig.onBrokerError != requestedWOLBroker || mqttWOLConfig.onAuthError != requestedWOLAuth) {
        update = true;
      }
#  endif

#  ifndef ESPWifiManualSetup
      if (update) {
        Log.notice(F("[WebUI] Saving MQTT/WOL configuration" CR));
        if (mqttConnectionUpdate) {
          WEBtoSYS["cnt_index"] = CNT_DEFAULT_INDEX;
          WEBtoSYS["save_cnt"] = true;
        }
        char jsonChar[100];
        serializeJson(modules, jsonChar, measureJson(modules) + 1);
        char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

        snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Save configuration").c_str());
        String response = String(buffer);
        if (mqttConnectionUpdate) response += String(restart_script);
        response += String(script);
        response += String(style);
        if (mqttConnectionUpdate) {
          snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, reset_body, jsonChar, gateway_name, "MQTT configuration saved");
        } else {
          snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_saved_body, jsonChar, gateway_name, "Wake-on-LAN configuration saved");
        }
        response += String(buffer);
        snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
        response += String(buffer);
        server.send(200, "text/html", response);

        delay(2000); // Wait for web page to be sent before
        String topic = String(mqtt_topic) + String(gateway_name) + String(subjectMQTTtoSYSset);
        XtoSYS((char*)topic.c_str(), WEBtoSYS);
        return;
      } else {
        Log.warning(F("[WebUI] No changes" CR));
      }
#  endif
    }
  }

  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);

  char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Configure MQTT").c_str());
  String response = String(buffer);
  response += String(script);
  response += String(style);
  String gatewayNameEscaped = HtmlEscape(String(gateway_name));
  String mqttTopicEscaped = HtmlEscape(String(mqtt_topic));
  // mqtt server (mh), mqtt port (ml), mqtt username (mu), mqtt password (mp), secure connection (sc), server certificate (msc), mqtt topic (mt), discovery prefix (dp) (last one only #ifdef ZmqttDiscovery)
#  if MQTT_BROKER_MODE
#    ifdef ZmqttDiscovery
  String discoveryPrefixEscaped = HtmlEscape(String(discovery_prefix));
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_mqtt_body, jsonChar, gateway_name, "", "1883", "", "", gatewayNameEscaped.c_str(), mqttTopicEscaped.c_str(), discoveryPrefixEscaped.c_str());
#    else
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_mqtt_body, jsonChar, gateway_name, "", "1883", "", "", gatewayNameEscaped.c_str(), mqttTopicEscaped.c_str());
#    endif
#  else
  String mqttServerEscaped = HtmlEscape(String(cnt_parameters_array[CNT_DEFAULT_INDEX].mqtt_server));
  String mqttUserEscaped = HtmlEscape(String(cnt_parameters_array[CNT_DEFAULT_INDEX].mqtt_user));
#    ifdef ZmqttDiscovery
  String discoveryPrefixEscaped = HtmlEscape(String(discovery_prefix));
#      ifdef MQTT_WOL_ENABLED
  String mqttWOLMacEscaped = HtmlEscape(String(mqttWOLConfig.mac));
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_mqtt_body, jsonChar, gateway_name,
           mqttServerEscaped.c_str(), cnt_parameters_array[CNT_DEFAULT_INDEX].mqtt_port, mqttUserEscaped.c_str(),
           (cnt_parameters_array[CNT_DEFAULT_INDEX].isConnectionSecure ? "checked" : ""), gatewayNameEscaped.c_str(),
           mqttTopicEscaped.c_str(), discoveryPrefixEscaped.c_str(), (mqttWOLConfig.enabled ? "checked" : ""),
           mqttWOLMacEscaped.c_str(), mqttWOLConfig.initialDelayMs / 1000UL, mqttWOLConfig.minFailures,
           mqttWOLConfig.repeatIntervalMs / 1000UL, (mqttWOLConfig.onTransportError ? "checked" : ""),
           (mqttWOLConfig.onBrokerError ? "checked" : ""), (mqttWOLConfig.onAuthError ? "checked" : ""));
#      else
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_mqtt_body, jsonChar, gateway_name, mqttServerEscaped.c_str(),
           cnt_parameters_array[CNT_DEFAULT_INDEX].mqtt_port, mqttUserEscaped.c_str(),
           (cnt_parameters_array[CNT_DEFAULT_INDEX].isConnectionSecure ? "checked" : ""), gatewayNameEscaped.c_str(),
           mqttTopicEscaped.c_str(), discoveryPrefixEscaped.c_str());
#      endif
#    else
#      ifdef MQTT_WOL_ENABLED
  String mqttWOLMacEscaped = HtmlEscape(String(mqttWOLConfig.mac));
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_mqtt_body, jsonChar, gateway_name,
           mqttServerEscaped.c_str(), cnt_parameters_array[CNT_DEFAULT_INDEX].mqtt_port, mqttUserEscaped.c_str(),
           (cnt_parameters_array[CNT_DEFAULT_INDEX].isConnectionSecure ? "checked" : ""), gatewayNameEscaped.c_str(),
           mqttTopicEscaped.c_str(), (mqttWOLConfig.enabled ? "checked" : ""), mqttWOLMacEscaped.c_str(),
           mqttWOLConfig.initialDelayMs / 1000UL, mqttWOLConfig.minFailures, mqttWOLConfig.repeatIntervalMs / 1000UL,
           (mqttWOLConfig.onTransportError ? "checked" : ""), (mqttWOLConfig.onBrokerError ? "checked" : ""),
           (mqttWOLConfig.onAuthError ? "checked" : ""));
#      else
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_mqtt_body, jsonChar, gateway_name, mqttServerEscaped.c_str(),
           cnt_parameters_array[CNT_DEFAULT_INDEX].mqtt_port, mqttUserEscaped.c_str(),
           (cnt_parameters_array[CNT_DEFAULT_INDEX].isConnectionSecure ? "checked" : ""), gatewayNameEscaped.c_str(), mqttTopicEscaped.c_str());
#      endif
#    endif
#  endif
  response += String(buffer);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
  response += String(buffer);
  server.send(200, "text/html", response);
}

#  ifndef ESPWifiManualSetup
/**
 * @brief /CG - Configure gateway Page
 * T: handleCG: uri: /gw, args: 2, method: 1
 * T: handleCG Arg: 0, gp=1234
 * T: handleCG Arg: 1, save=
 */

void handleCG() {
  WEBUI_TRACE_LOG(F("handleCG: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  bool update = false;
  StaticJsonDocument<JSON_MSG_BUFFER> jsonBuffer;
  JsonObject WEBtoSYS = jsonBuffer.to<JsonObject>();

  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleCG Arg: %d, %s=%s" CR), i, server.argName(i).c_str(),
                      server.argName(i) == "gp" ? "[redacted]" : server.arg(i).c_str());
    }
    if (server.hasArg("save") && server.hasArg("gp") && strcmp(ota_pass, server.arg("gp").c_str())) {
      if (copyConfigString(ota_pass, sizeof(ota_pass), server.arg("gp").c_str(), "gw_pass")) {
        WEBtoSYS["gw_pass"] = ota_pass;
        update = true;
      }
    }
  }

  if (update) {
    Log.warning(F("[WebUI] Save Password and Restart" CR));

    char jsonChar[100];
    serializeJson(modules, jsonChar, measureJson(modules) + 1);
    char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Save Password and Restart").c_str());
    String response = String(buffer);
    response += String(restart_script);
    response += String(script);
    response += String(style);
    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, reset_body, jsonChar, gateway_name, "Save Password and Restart");
    response += String(buffer);
    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
    response += String(buffer);
    server.send(200, "text/html", response);

    delay(2000); // Wait for web page to be sent before
    String topic = String(mqtt_topic) + String(gateway_name) + String(subjectMQTTtoSYSset);
    XtoSYS((char*)topic.c_str(), WEBtoSYS);
  } else {
    Log.warning(F("[WebUI] No changes" CR));
  }

  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);

  char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Configure gateway").c_str());
  String response = String(buffer);
  response += String(script);
  response += String(style);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_gateway_body, jsonChar, gateway_name, ota_pass);
  response += String(buffer);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
  response += String(buffer);
  server.send(200, "text/html", response);
}
#  endif

/**
 * @brief /LO - Configure Logging Page
 * T: handleLO: uri: /lo, args: 2, method: 1
 * T: handleLO Arg: 0, lo=5
 * T: handleLO Arg: 1, save=
 */
void handleLO() {
  WEBUI_TRACE_LOG(F("handleLO: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleLO Arg: %d, %s=%s" CR), i, server.argName(i).c_str(), server.arg(i).c_str());
    }
    if (server.hasArg("save") && server.hasArg("lo") && server.arg("lo").toInt() != Log.getLevel()) {
      Log.fatal(F("[WebUI] Log level changed to: %d" CR), server.arg("lo").toInt());
      Log.setLevel(server.arg("lo").toInt());
    }
  }

  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);

  char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Configure Logging").c_str());
  String response = String(buffer);
  response += String(script);
  response += String(style);
  int logLevel = Log.getLevel();
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_logging_body, jsonChar, gateway_name, (logLevel == 0 ? "selected" : ""), (logLevel == 1 ? "selected" : ""), (logLevel == 2 ? "selected" : ""), (logLevel == 3 ? "selected" : ""), (logLevel == 4 ? "selected" : ""), (logLevel == 5 ? "selected" : ""), (logLevel == 6 ? "selected" : ""));
  response += String(buffer);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
  response += String(buffer);
  server.send(200, "text/html", response);
}

#  ifdef ZgatewayLORA
/**
 * @brief /LA - Configure LORA Page
 * T: handleLA: uri: /la, args: 11, method: 1
 * T: handleLA Arg: 0, lf=868100000
 * T: handleLA Arg: 1, lt=14
 * T: handleLA Arg: 2, ls=12
 * T: handleLA Arg: 3, lb=125
 * T: handleLA Arg: 4, lc=5
 * T: handleLA Arg: 5, ll=8
 * T: handleLA Arg: 6, lw=0
 * T: handleLA Arg: 7, lr=1
 * T: handleLA Arg: 8, li=0
 * T: handleLA Arg: 9, ok=0
 * T: handleLA Arg: 10, save=
 */
void handleLA() {
  WEBUI_TRACE_LOG(F("handleLA: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleLA Arg: %d, %s=%s" CR), i, server.argName(i).c_str(), server.arg(i).c_str());
    }
    if (server.hasArg("save")) {
      StaticJsonDocument<JSON_MSG_BUFFER> jsonBuffer;
      JsonObject WEBtoLORA = jsonBuffer.to<JsonObject>();
      bool update = false;
      if (server.hasArg("lf")) {
        WEBtoLORA["frequency"] = server.arg("lf");
        update = true;
      }

      if (server.hasArg("lt")) {
        WEBtoLORA["txpower"] = server.arg("lt");
        update = true;
      }

      if (server.hasArg("ls")) {
        WEBtoLORA["spreadingfactor"] = server.arg("ls");
        update = true;
      }

      if (server.hasArg("lb")) {
        WEBtoLORA["signalbandwidth"] = server.arg("lb");
        update = true;
      }

      if (server.hasArg("lc")) {
        WEBtoLORA["codingrate"] = server.arg("lc");
        update = true;
      }

      if (server.hasArg("ll")) {
        WEBtoLORA["preamblelength"] = server.arg("ll");
        update = true;
      }

      if (server.hasArg("lw")) {
        WEBtoLORA["syncword"] = server.arg("lw");
        update = true;
      }

      if (server.hasArg("lr")) {
        WEBtoLORA["enablecrc"] = server.arg("lr");
        update = true;
      } else {
        WEBtoLORA["enablecrc"] = false;
        update = true;
      }

      if (server.hasArg("li")) {
        WEBtoLORA["invertiq"] = server.arg("li");
        update = true;
      } else {
        WEBtoLORA["invertiq"] = false;
        update = true;
      }

      if (server.hasArg("ok")) {
        WEBtoLORA["onlyknown"] = server.arg("ok");
        update = true;
      } else {
        WEBtoLORA["onlyknown"] = false;
        update = true;
      }
      if (update) {
        Log.notice(F("[WebUI] Save data" CR));
        WEBtoLORA["save"] = true;
        LORAConfig_fromJson(WEBtoLORA);
        stateLORAMeasures();
        Log.trace(F("[WebUI] LORAConfig end" CR));
      }
    }
  }
  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);

  char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Configure LORA").c_str());
  String response = String(buffer);
  response += String(script);
  response += String(style);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_lora_body,
           jsonChar,
           gateway_name,
           LORAConfig.frequency == 868000000 ? "selected" : "",
           LORAConfig.frequency == 915000000 ? "selected" : "",
           LORAConfig.frequency == 433000000 ? "selected" : "",
           LORAConfig.txPower == 0 ? "selected" : "",
           LORAConfig.txPower == 1 ? "selected" : "",
           LORAConfig.txPower == 2 ? "selected" : "",
           LORAConfig.txPower == 3 ? "selected" : "",
           LORAConfig.txPower == 4 ? "selected" : "",
           LORAConfig.txPower == 5 ? "selected" : "",
           LORAConfig.txPower == 6 ? "selected" : "",
           LORAConfig.txPower == 7 ? "selected" : "",
           LORAConfig.txPower == 8 ? "selected" : "",
           LORAConfig.txPower == 9 ? "selected" : "",
           LORAConfig.txPower == 10 ? "selected" : "",
           LORAConfig.txPower == 11 ? "selected" : "",
           LORAConfig.txPower == 12 ? "selected" : "",
           LORAConfig.txPower == 13 ? "selected" : "",
           LORAConfig.txPower == 14 ? "selected" : "",
           LORAConfig.spreadingFactor == 7 ? "selected" : "",
           LORAConfig.spreadingFactor == 8 ? "selected" : "",
           LORAConfig.spreadingFactor == 9 ? "selected" : "",
           LORAConfig.spreadingFactor == 10 ? "selected" : "",
           LORAConfig.spreadingFactor == 11 ? "selected" : "",
           LORAConfig.spreadingFactor == 12 ? "selected" : "",
           LORAConfig.signalBandwidth == 7800 ? "selected" : "",
           LORAConfig.signalBandwidth == 10400 ? "selected" : "",
           LORAConfig.signalBandwidth == 15600 ? "selected" : "",
           LORAConfig.signalBandwidth == 20800 ? "selected" : "",
           LORAConfig.signalBandwidth == 31250 ? "selected" : "",
           LORAConfig.signalBandwidth == 41700 ? "selected" : "",
           LORAConfig.signalBandwidth == 62500 ? "selected" : "",
           LORAConfig.signalBandwidth == 125000 ? "selected" : "",
           LORAConfig.signalBandwidth == 250000 ? "selected" : "",
           LORAConfig.signalBandwidth == 500000 ? "selected" : "",
           LORAConfig.codingRateDenominator == 5 ? "selected" : "",
           LORAConfig.codingRateDenominator == 6 ? "selected" : "",
           LORAConfig.codingRateDenominator == 7 ? "selected" : "",
           LORAConfig.codingRateDenominator == 8 ? "selected" : "",
           LORAConfig.preambleLength,
           LORAConfig.syncWord,
           LORAConfig.crc ? "checked" : "",
           LORAConfig.invertIQ ? "checked" : "",
           LORAConfig.onlyKnown ? "checked" : "");

  response += String(buffer);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
  response += String(buffer);
  server.send(200, "text/html", response);
}
#  elif defined(ZgatewayRTL_433) || defined(ZgatewayPilight) || defined(ZgatewayRF) || defined(ZgatewayRF2) || defined(ZactuatorSomfy)
#    include <map>
std::map<int, String> activeReceiverOptions = {
    {0, "Inactive"},
#    if defined(ZgatewayPilight) && !defined(ZradioSX127x)
    {1, "PiLight"},
#    endif
#    if defined(ZgatewayRF) && !defined(ZradioSX127x)
    {2, "RF"},
#    endif
#    ifdef ZgatewayRTL_433
    {3, "RTL_433"},
#    endif
#    if defined(ZgatewayRF2) && !defined(ZradioSX127x)
    {4, "RF2 (restart required)"}
#    endif
};

bool isValidReceiver(int receiverId) {
  // Check if the receiverId exists in the activeReceiverOptions map
  return activeReceiverOptions.find(receiverId) != activeReceiverOptions.end();
}

String generateActiveReceiverOptions(int currentSelection) {
  String optionsHtml = "";
  for (const auto& option : activeReceiverOptions) {
    optionsHtml += "<option value='" + String(option.first) + "'";
    if (currentSelection == option.first) {
      optionsHtml += " selected";
    }
    optionsHtml += ">" + option.second + "</option>";
  }
  return optionsHtml;
}

/**
 * @brief /RF - Configure RF Page
 * T: handleRF: uri: /rf, args: 2, method: 1
 * T: handleRF Arg: 0, rf=868.30
 * T: handleRF Arg: 1, oo=0
 * T: handleRF Arg: 2, rs=0
 * T: handleRF Arg: 3, dg=0
 * T: handleRF Arg: 4, ar=0
 * T: handleRF Arg: 4, save=
 */

void handleRF() {
  WEBUI_TRACE_LOG(F("handleRF: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  bool update = false;
  StaticJsonDocument<JSON_MSG_BUFFER> jsonBuffer;
  JsonObject WEBtoRF = jsonBuffer.to<JsonObject>();

  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleRF Arg: %d, %s=%s" CR), i, server.argName(i).c_str(), server.arg(i).c_str());
    }
    if (server.hasArg("save")) {
      if (server.hasArg("rf")) {
        String freqStr = server.arg("rf");
        RFConfig.frequency = freqStr.toFloat();
        if (validFrequency(RFConfig.frequency)) {
          WEBtoRF["frequency"] = RFConfig.frequency;
          update = true;
        } else {
          Log.warning(F("[WebUI] Invalid Frequency" CR));
        }
      }
      if (server.hasArg("ar")) {
        int selectedReceiver = server.arg("ar").toInt();
        if (isValidReceiver(selectedReceiver)) { // Assuming isValidReceiver is a validation function
          RFConfig.activeReceiver = selectedReceiver;
          WEBtoRF["activereceiver"] = RFConfig.activeReceiver;
          update = true;
        } else {
          Log.warning(F("[WebUI] Invalid Active Receiver" CR));
        }
      }
      if (server.hasArg("oo")) {
        RFConfig.newOokThreshold = server.arg("oo").toInt();
        WEBtoRF["ookthreshold"] = RFConfig.newOokThreshold;
        update = true;
      }
      if (server.hasArg("rs")) {
        RFConfig.rssiThreshold = server.arg("rs").toInt();
        WEBtoRF["rssithreshold"] = RFConfig.rssiThreshold;
        update = true;
      }
      if (update) {
        Log.notice(F("[WebUI] Save data" CR));
        WEBtoRF["save"] = true;
        RFConfig_fromJson(WEBtoRF);
        stateRFMeasures();
        Log.trace(F("[WebUI] RFConfig end" CR));
      }
    }
  }

  String activeReceiverHtml = generateActiveReceiverOptions(RFConfig.activeReceiver);

  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);
  char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Configure RF").c_str());
  String response = String(buffer);
  response += String(script);
  response += String(style);

  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_rf_body, jsonChar, gateway_name, RFConfig.frequency, activeReceiverHtml.c_str());
  response += String(buffer);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
  response += String(buffer);
  server.send(200, "text/html", response);
}
#  endif

/**
 * @brief /RT - Reset configuration ( Erase and Restart ) from Configuration menu
 * 
 */
void handleRT() {
  WEBUI_TRACE_LOG(F("handleRT: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleRT Arg: %d, %s=%s" CR), i, server.argName(i).c_str(), server.arg(i).c_str());
    }
  }
  if (server.hasArg("non")) {
    char jsonChar[100];
    serializeJson(modules, jsonChar, measureJson(modules) + 1);
    Log.warning(F("[WebUI] Erase and Restart" CR));

    char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Erase and Restart").c_str());
    String response = String(buffer);
    response += String(restart_script);
    response += String(script);
    response += String(style);
    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, reset_body, jsonChar, gateway_name, "Erase and Restart");
    response += String(buffer);
    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
    response += String(buffer);
    server.send(200, "text/html", response);

    eraseConfig();
  } else {
    handleCN();
  }
}

#  if defined(ZgatewayCloud)
/**
 * @brief /CL - Cloud Configuration
 * 
 */
void handleCL() {
  WEBUI_TRACE_LOG(F("handleCL: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleCL Arg: %d, %s=%s" CR), i, server.argName(i).c_str(), server.arg(i).c_str());
    }
  }

  if (server.hasArg("save")) {
    // T: handleCL: uri: /cl, args: 2, method: 1
    // T: handleCL Arg: 0, cl-en=on
    // T: handleCL Arg: 1, save=
    if (server.hasArg("save") && server.method() == 1) {
      setCloudEnabled(server.hasArg("cl-en"));
    }
  }

  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);

  char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Configure Cloud").c_str());
  String response = String(buffer);
  response += String(script);
  response += String(style);

  char cloudEnabled[8] = {0};
  if (isCloudEnabled()) {
    strncpy(cloudEnabled, "checked", 8);
  }

  char deviceToken[5] = {0};
  if (!isCloudDeviceTokenSupplied()) {
    strncpy(deviceToken, " Not", 4);
  }

  requestToken = esp_random();
#    ifdef ESP32_ETHERNET
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_cloud_body, jsonChar, gateway_name, " cloud checked", " Not", (String(CLOUDGATEWAY) + "token/start").c_str(), (char*)ETH.macAddress().c_str(), ("http://" + String(TheengsUtils::ip2CharArray(ETH.localIP())) + "/").c_str(), gateway_name, uptime(), requestToken);
#    else
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, config_cloud_body, jsonChar, gateway_name, cloudEnabled, deviceToken, (String(CLOUDGATEWAY) + "token/start").c_str(), (char*)WiFi.macAddress().c_str(), ("http://" + String(TheengsUtils::ip2CharArray(WiFi.localIP())) + "/").c_str(), gateway_name, uptime(), requestToken);
#    endif
  response += String(buffer);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
  response += String(buffer);
  server.send(200, "text/html", response);
}

/**
 * @brief /TK - Receive Cloud Device Token
 * 
 */
void handleTK() {
  WEBUI_TRACE_LOG(F("handleTK: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleTK Arg: %d, %s=%s" CR), i, server.argName(i).c_str(), server.arg(i).c_str());
    }
  }

  if (server.hasArg("deviceToken") && server.hasArg("uptime") && server.hasArg("RT")) {
    String deviceToken = server.arg("deviceToken");

    if (setCloudDeviceToken(deviceToken) && server.arg("RT").toInt() == requestToken && server.arg("uptime").toInt() + 600 > uptime()) {
      setCloudEnabled(true);
      char jsonChar[100];
      serializeJson(modules, jsonChar, measureJson(modules) + 1);

      char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

      snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Received Device Token").c_str());
      String response = String(buffer);
      response += String(script);
      response += String(style);
      snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, token_body, jsonChar, gateway_name);
      response += String(buffer);
      snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
      response += String(buffer);
      server.send(200, "text/html", response);
    } else {
      WEBUI_TRACE_LOG(F("handleTK: uptime: %u, uptime: %u, ok: %T" CR), server.arg("uptime").toInt(), uptime(), server.arg("uptime").toInt() + 600 > uptime());
      WEBUI_TRACE_LOG(F("handleTK: RT: %d, RT: %d, ok: %T " CR), server.arg("RT").toInt(), requestToken, server.arg("RT").toInt() == requestToken);
      Log.error(F("[WebUI] Invalid Token Response: RT: %T, uptime: %T" CR), server.arg("RT").toInt() == requestToken, server.arg("uptime").toInt() + 600 > uptime());
      server.send(500, "text/html", "Internal ERROR - Invalid Token");
    }
  }
}

#  endif

/**
 * @brief /IN - Information Page
 * 
 */
void handleIN() {
  WEBUI_TRACE_LOG(F("handleCN: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleIN Arg: %d, %s=%s" CR), i, server.argName(i).c_str(), server.arg(i).c_str());
    }
  } else {
    char jsonChar[100];
    serializeJson(modules, jsonChar, measureJson(modules) + 1);

    stateSnapshotOnly = true;
    String informationDisplay = stateMeasures(); // .replace(",\"", "}1");  // .replace("\":", "=2")

// }1 json-oled }2 true } }1 Cloud }2 cloudEnabled}2true}1c
#  if defined(ZgatewayBLETracker)
    informationDisplay += "1<BR>BLE observer}2}1";
    informationDisplay += stateBLETrackerMeasures();
#  elif defined(ZgatewayBT)
    informationDisplay += "1<BR>BT}2}1"; // }1 the bracket is not needed as the previous message ends with }
    informationDisplay += stateBTMeasures(false);
#  endif
#  if defined(ZdisplaySSD1306)
    informationDisplay += "1<BR>SSD1306}2}1"; // }1 the bracket is not needed as the previous message ends with }
    informationDisplay += stateSSD1306Display();
#  endif
#  if defined(ZgatewayCloud)
    informationDisplay += "1<BR>Cloud}2}1";
    informationDisplay += stateCLOUDStatus();
#  endif
#  if defined(ZgatewayLORA)
    informationDisplay += "1<BR>LORA}2}1";
    informationDisplay += stateLORAMeasures();
#  endif
#  if defined(ZgatewayRF)
    informationDisplay += "1<BR>RF}2}1";
    informationDisplay += stateRFMeasures();
#  endif
    informationDisplay += "1<BR>WebUI}2}1";
    informationDisplay += stateWebUIStatus();
    stateSnapshotOnly = false;

    // stateBTMeasures causes a Stack canary watchpoint triggered (loopTask)
    // WEBUI_TRACE_LOG(F("[WebUI] informationDisplay before %s" CR), informationDisplay.c_str());

    // TODO: need to fix display of modules array within SYStoMQTT

    informationDisplay += "1}2";
    informationDisplay.replace(",\"", "}1");
    informationDisplay.replace("\":", "}2");
    informationDisplay.replace("{\"", "");
    informationDisplay.replace("\"", "\\\"");

    // WEBUI_TRACE_LOG(F("[WebUI] informationDisplay after %s" CR), informationDisplay.c_str());

    if (informationDisplay.length() > WEB_TEMPLATE_BUFFER_MAX_SIZE) {
      Log.warning(F("[WebUI] informationDisplay content length ( %d ) greater than WEB_TEMPLATE_BUFFER_MAX_SIZE.  Display truncated" CR), informationDisplay.length());
    }

    char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Information").c_str());
    String response = String(buffer);

    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, information_script, informationDisplay.c_str());
    response += String(buffer);

    response += String(script);
    response += String(style);
    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, information_body, jsonChar, gateway_name);
    response += String(buffer);

    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
    response += String(buffer);

    server.send(200, "text/html", response);
  }
}

/**
 * @brief /handleFavicon - Send Favicon
 * 
 */
void handleFavicon() {
  WEBUI_TRACE_LOG(F("handleCN: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  server.sendHeader("Content-Type", "image/x-icon");
  server.send_P(200, "image/x-icon", reinterpret_cast<const char*>(Openmqttgateway_logo_mini_ico), sizeof(Openmqttgateway_logo_mini_ico));
}

void handleUIStyle() {
  static constexpr size_t stylePrefixLength = sizeof("<style>") - 1;
  static constexpr size_t styleSuffixLength = sizeof("</style></head>") - 1;
  static constexpr size_t cssLength = sizeof(inline_ui_style) - 1 - stylePrefixLength - styleSuffixLength;
  server.sendHeader("Cache-Control", "public, max-age=86400");
  server.setContentLength(cssLength);
  server.send(200, "text/css", "");
  server.sendContent(inline_ui_style + stylePrefixLength, cssLength);
}

#  if defined(ESP32) && defined(MQTT_HTTPS_FW_UPDATE)
bool localFirmwareUploadAuthorized = false;
bool localFirmwareUploadStarted = false;
bool localFirmwareUploadSuccess = false;
bool localFirmwareUploadRequiresRestart = false;
uint32_t localFirmwareRestartAt = 0;
size_t localFirmwareUploadBytes = 0;
size_t localFirmwareNextProgressLog = 0;
String localFirmwareUploadError;

void setLocalFirmwareUploadError(const String& error) {
  if (!localFirmwareUploadError.length()) localFirmwareUploadError = error;
  Log.error(F("[WebUI][OTA] %s" CR), error.c_str());
}

void handleLocalFirmwareUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    localFirmwareUploadAuthorized = !webUISecure || server.authenticate(www_username, ota_pass);
    localFirmwareUploadStarted = false;
    localFirmwareUploadSuccess = false;
    localFirmwareUploadRequiresRestart = false;
    localFirmwareRestartAt = 0;
    localFirmwareUploadBytes = 0;
    localFirmwareNextProgressLog = 256U * 1024U;
    localFirmwareUploadError = "";

    if (!localFirmwareUploadAuthorized) {
      Log.warning(F("[WebUI][OTA] rejected unauthenticated local firmware upload" CR));
      return;
    }

    String filename = upload.filename;
    String lowerFilename = filename;
    lowerFilename.toLowerCase();
    if (!lowerFilename.endsWith(".bin")) {
      setLocalFirmwareUploadError("The selected file must have a .bin extension");
      return;
    }

#    ifdef ZgatewayBLETracker
    if (isBLETrackerStarting()) {
      setLocalFirmwareUploadError("BLE radio is starting; wait a few seconds and retry the upload");
      return;
    }
#    endif

    ProcessLock = true;
#    ifdef ZgatewayBT
    stopProcessing(true);
#    elif defined(ZgatewayBLETracker)
    stopBLETracker(true);
#    endif
    localFirmwareUploadRequiresRestart = true;
    gatewayState = GatewayState::LOCAL_OTA_IN_PROGRESS;
    last_ota_activity_millis = millis();
    lpDisplayPrint("Web OTA in progress");

    Update.clearError();
    Log.notice(F("[WebUI][OTA] local upload started file=%s available_bytes=%u heap=%u" CR),
               filename.c_str(), ESP.getFreeSketchSpace(), ESP.getFreeHeap());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      setLocalFirmwareUploadError(String("Unable to start OTA: ") + Update.errorString());
      return;
    }
    localFirmwareUploadStarted = true;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!localFirmwareUploadAuthorized || !localFirmwareUploadStarted || localFirmwareUploadError.length()) return;

    if (localFirmwareUploadBytes == 0 && (upload.currentSize == 0 || upload.buf[0] != 0xE9)) {
      setLocalFirmwareUploadError("The file is not an ESP32 application image");
      Update.abort();
      return;
    }

    size_t written = Update.write(upload.buf, upload.currentSize);
    if (written != upload.currentSize) {
      setLocalFirmwareUploadError(String("Flash write failed: ") + Update.errorString());
      Update.abort();
      return;
    }
    // A fast LAN client can otherwise feed flash writes continuously enough
    // to starve the Arduino/WiFi tasks on this memory-constrained build. Yield
    // briefly for every upload block; normal browser uploads remain quick,
    // while bursty clients can no longer trigger a watchdog/panic reset.
    delay(5);
    localFirmwareUploadBytes += written;
    last_ota_activity_millis = millis();
    if (localFirmwareUploadBytes >= localFirmwareNextProgressLog) {
      Log.notice(F("[WebUI][OTA] local upload progress bytes=%u heap=%u" CR),
                 localFirmwareUploadBytes, ESP.getFreeHeap());
      localFirmwareNextProgressLog += 256U * 1024U;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!localFirmwareUploadAuthorized || !localFirmwareUploadStarted || localFirmwareUploadError.length()) return;

    if (localFirmwareUploadBytes == 0) {
      setLocalFirmwareUploadError("The uploaded firmware file is empty");
      Update.abort();
      return;
    }
    if (!Update.end(true) || !Update.isFinished()) {
      setLocalFirmwareUploadError(String("Firmware validation failed: ") + Update.errorString());
      return;
    }
    localFirmwareUploadSuccess = true;
    Log.notice(F("[WebUI][OTA] local firmware validated bytes=%u md5=%s" CR),
               localFirmwareUploadBytes, Update.md5String().c_str());
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.abort();
    setLocalFirmwareUploadError("Firmware upload aborted by the client");
    gatewayState = GatewayState::ERROR;
    delay(100);
    ESP.restart();
  }
}

void handleLocalFirmwareUploadFinished() {
  WEBUI_SECURE
  server.sendHeader("Connection", "close");

  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);
  char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html,
           (String(gateway_name) + (localFirmwareUploadSuccess ? " - Firmware installed" : " - Firmware upload failed")).c_str());
  String response = String(buffer) + String(restart_script) + String(script) + String(style);
  String resultMessage;
  if (localFirmwareUploadSuccess) {
    resultMessage = "Local firmware validated and installed (" + String(localFirmwareUploadBytes) + " bytes)";
  } else {
    resultMessage = "Firmware was not installed: " + HtmlEscape(localFirmwareUploadError.length() ? localFirmwareUploadError : "upload did not complete");
  }
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, reset_body, jsonChar, gateway_name, resultMessage.c_str());
  response += String(buffer);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
  response += String(buffer);
  server.send(localFirmwareUploadSuccess ? 200 : 500, "text/html", response);

  Log.notice(F("[WebUI][OTA] local upload request complete success=%T bytes=%u restart_required=%T" CR),
             localFirmwareUploadSuccess, localFirmwareUploadBytes,
             localFirmwareUploadSuccess || localFirmwareUploadRequiresRestart);
  if (localFirmwareUploadSuccess || localFirmwareUploadRequiresRestart) {
    // Do not restart from inside WebServer's upload callback. Let the handler
    // return so the response can drain, the HTTP client can close and the
    // BLE/WiFi coexistence guard can release cleanly. WebUILoop performs the
    // actual restart after this short grace period.
    localFirmwareRestartAt = millis() + 8000UL;
    Log.notice(F("[WebUI][OTA] restart scheduled after HTTP response" CR));
  }
}

/**
 * @brief /UP - Firmware Upgrade Page
 * 
 */
void handleUP() {
  WEBUI_TRACE_LOG(F("handleUP: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  if (server.args()) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleUP Arg: %d, %s=%s" CR), i, server.argName(i).c_str(), server.arg(i).c_str());
    }
    DynamicJsonDocument jsonBuffer(JSON_MSG_BUFFER);
    JsonObject WEBtoSYS = jsonBuffer.to<JsonObject>();

    if (server.hasArg("o")) {
      WEBtoSYS["url"] = server.arg("o");
      WEBtoSYS["version"] = "test";
      WEBtoSYS["password"] = ota_pass;

      {
        sendRestartPage();

        String output;
        serializeJson(WEBtoSYS, output);
        Log.notice(F("[WebUI] XtoSYSupdate %s" CR), output.c_str());
      }

      String topic = String(mqtt_topic) + String(gateway_name) + String(subjectMQTTtoSYSupdate);
      MQTTHttpsFWUpdate((char*)topic.c_str(), WEBtoSYS);
      return;
    } else if (server.hasArg("le")) {
      uint32_t le = server.arg("le").toInt();
      if (le != 0) {
        WEBtoSYS["version"] = (le == 1 ? "latest" : (le == 2 ? "dev" : "unknown"));
        WEBtoSYS["password"] = ota_pass;
        {
          sendRestartPage();

          String output;
          serializeJson(WEBtoSYS, output);
          Log.notice(F("[WebUI] XtoSYSupdate %s" CR), output.c_str());
        }

        String topic = String(mqtt_topic) + String(gateway_name) + String(subjectMQTTtoSYSupdate);
        MQTTHttpsFWUpdate((char*)topic.c_str(), WEBtoSYS);
        return;
      }
    }
  }
  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);

  char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Firmware Upgrade").c_str());
  String response = String(buffer);
  response += String(script);
  response += String(style);
  String systemUrl = RELEASE_LINK + latestVersion + "/" + ENV_NAME + "-firmware.bin";
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, upgrade_body, jsonChar, gateway_name, systemUrl.c_str());
  response += String(buffer);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
  response += String(buffer);
  server.send(200, "text/html", response);
}
#  endif

void sendRestartPage() {
  char jsonChar[100];
  serializeJson(modules, jsonChar, measureJson(modules) + 1);
  char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Updating Firmware and Restart").c_str());
  String response = String(buffer);
  response += String(restart_script);
  response += String(script);
  response += String(style);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, reset_body, jsonChar, gateway_name, "Updating Firmware and Restart");
  response += String(buffer);
  snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
  response += String(buffer);
  server.send(200, "text/html", response);

  delay(2000); // Wait for web page to be sent before
}

/**
 * @brief /CS - Serial Console and Command Line
 * 
 */
void handleCS() {
  WEBUI_TRACE_LOG(F("handleCS: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
  WEBUI_SECURE
  if (server.args() && server.hasArg("c2")) {
    for (uint8_t i = 0; i < server.args(); i++) {
      WEBUI_TRACE_LOG(F("handleCS Arg: %d, %s=%s" CR), i, server.argName(i).c_str(), server.arg(i).c_str());
    }
    if (server.hasArg("c1")) {
      String c1 = server.arg("c1");

      String cmdTopic = String(mqtt_topic) + String(gateway_name) + "/" + c1.substring(0, c1.indexOf(' '));
      String command = c1.substring(c1.indexOf(' ') + 1);
      if (command.length()) {
        WEBUI_TRACE_LOG(F("[WebUI] handleCS inject MQTT Command topic: '%s', command: '%s'" CR), cmdTopic.c_str(), command.c_str());
        receivingDATA(cmdTopic.c_str(), command.c_str());
      } else {
        Log.warning(F("[WebUI] Missing command: '%s', command: '%s'" CR), cmdTopic.c_str(), command.c_str());
      }
    }

    uint32_t index = server.arg("c2").toInt();
    const bool consoleWasInitialized = reset_web_log_flag;
    if (!consoleWasInitialized) {
      index = 0;
      reset_web_log_flag = true;
    }

    // A full console buffer is about 6 KB. On memory-constrained ESP32 builds,
    // sending it in one response can monopolize the single HTTP client if the
    // Wi-Fi link stalls. Page the log without splitting entries; the returned
    // index points to the first entry not yet delivered, so the existing UI
    // automatically requests the next page without losing messages.
    constexpr size_t CONSOLE_RESPONSE_PAYLOAD_MAX = 1800;
    String payload;
    payload.reserve(CONSOLE_RESPONSE_PAYLOAD_MAX + 1);
    uint32_t responseIndex = index ? index : log_buffer_pointer;
    bool cflg = (index);
    char* line;
    size_t len;
    while (GetLog(1, &index, &line, &len)) {
      size_t lineLength = len > 0 ? len - 1 : 0;
      size_t required = lineLength + (cflg ? 1 : 0);
      if (payload.length() && payload.length() + required > CONSOLE_RESPONSE_PAYLOAD_MAX) {
        break;
      }
      if (cflg) {
        payload += "\n";
      }
      for (size_t x = 0; x < lineLength; x++) {
        payload += line[x];
      }
      cflg = true;
      responseIndex = index;
    }
    String message = String(responseIndex) + "}1" + String(consoleWasInitialized) + "}1";
    message += payload;
    message += "}1";
    WEBUI_TRACE_LOG(F("[WebUI][Console] response bytes=%u next_index=%u heap=%u" CR),
                    message.length(), responseIndex, ESP.getFreeHeap());
    server.send(200, "text/plain", message);
  } else {
    char jsonChar[100];
    serializeJson(modules, jsonChar, measureJson(modules) + 1);

    char buffer[WEB_TEMPLATE_BUFFER_MAX_SIZE];

    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, header_html, (String(gateway_name) + " - Console").c_str());
    String response = String(buffer);
    response += String(console_script);
    response += String(script);
    response += String(style);
    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, console_body, jsonChar, gateway_name);
    response += String(buffer);
    snprintf(buffer, WEB_TEMPLATE_BUFFER_MAX_SIZE, footer, OMG_VERSION);
    response += String(buffer);
    server.send(200, "text/html", response);
  }
}

/**
 * @brief Page not found handler
 * 
 */
void notFound() {
  WEBUI_SECURE
#  ifdef WEBUI_DEVELOPMENT
  String path = server.uri();
  if (!exists(path)) {
    if (exists(path + ".html")) {
      path += ".html";
    } else {
#  endif
      Log.warning(F("[WebUI] notFound: uri: %s, args: %d, method: %d" CR), server.uri(), server.args(), server.method());
      server.send(404, "text/plain", "Not found");
      return;
#  ifdef WEBUI_DEVELOPMENT
    }
  }
  WEBUI_TRACE_LOG(F("notFound returning: actual uri: %s, args: %d, method: %d" CR), path, server.args(), server.method());
  File file = FILESYSTEM.open(path, "r");
  server.streamFile(file, "text/html");
  file.close();
#  endif
}

void WebUISetup() {
  WEBUI_TRACE_LOG(F("ZwebUI setup start" CR));

  WebUIConfig_load();
  webUIQueue = xQueueCreate(5, sizeof(webUIQueueMessage*));

#  ifdef WEBUI_DEVELOPMENT
  FILESYSTEM.begin();
  {
    File root = FILESYSTEM.open("/");
    File file = root.openNextFile();
    while (file) {
      String fileName = file.name();
      size_t fileSize = file.size();
      WEBUI_TRACE_LOG(F("FS File: %s, size: %s" CR), fileName.c_str(), formatBytes(fileSize).c_str());
      file = root.openNextFile();
    }
  }
#  endif
  server.onNotFound(notFound);

  server.on("/", handleRoot); // Main Menu

  server.on("/in", handleIN); // Information
  server.on("/cs", handleCS); // Console
#  if defined(ESP32) && defined(MQTT_HTTPS_FW_UPDATE)
  server.on("/up", handleUP); // Firmware Upgrade
  server.on("/up-local", HTTP_POST, handleLocalFirmwareUploadFinished, handleLocalFirmwareUpload); // Local firmware upload
#  endif
  server.on("/cn", handleCN); // Configuration
  server.on("/wi", HTTP_POST, handleWI); // Configure Wifi
  server.on("/mq", HTTP_POST, handleMQ); // Configure MQTT
#  ifndef ESPWifiManualSetup
  server.on("/cg", HTTP_POST, handleCG); // Configure gateway"
#  endif
  server.on("/wu", handleWU); // Configure WebUI
#  if defined(ZsensorGPIOInput) && defined(GPIO_INPUT_RUNTIME_CONFIG)
  server.on("/gi", handleGI); // Configure GPIO inputs and outputs
  server.on("/gi-row", handleGIRow); // Progressive GPIO channel fragment
#    if GPIO_OUTPUT_MAX > 0
  server.on("/go-row", handleGORow); // Progressive GPIO output fragment
#    endif
#  endif
#  if defined(ZgatewayBT) || defined(ZgatewayBLETracker)
  server.on("/bt", handleBTTrackers); // Configure selected BLE presence devices
  server.on("/bt-row", handleBTRow); // Progressive BLE tracker fragment
  server.on("/bt-candidates", handleBTCandidates); // Progressive BLE candidate suggestions
#  endif
#  ifdef ZgatewayLORA
  server.on("/la", handleLA); // Configure LORA
#  elif defined(ZgatewayRTL_433) || defined(ZgatewayPilight) || defined(ZgatewayRF) || defined(ZgatewayRF2) || defined(ZactuatorSomfy)
  server.on("/rf", handleRF); // Configure RF
#  endif
#  if defined(ZgatewayCloud)
  server.on("/cl", handleCL); // Configure Cloud
  server.on("/tk", handleTK); // Store Device Token
#  endif
  server.on("/lo", handleLO); // Configure Logging

  server.on("/rt", handleRT); // Reset configuration ( Erase and Restart )
  server.on("/ui.css", handleUIStyle); // Shared, cacheable WebUI stylesheet
  server.on("/favicon.ico", handleFavicon); // Information
  server.begin();
  server.enableTcpNoDelay();
  Log.notice(F("[WebUI] TCP_NODELAY enabled" CR));

  Log.begin(LOG_LEVEL, &WebLog);

  Log.trace(F("[WebUI] displayMetric %T" CR), displayMetric);
  Log.trace(F("[WebUI] WebUI Secure %T" CR), webUISecure);
  Log.notice(F("OpenMQTTGateway URL: http://%s/" CR), WiFi.localIP().toString().c_str());
  displayPrint("URL: http://", (char*)WiFi.localIP().toString().c_str());
  Log.notice(F("ZwebUI setup done" CR));
}

unsigned long nextWebUIMessage = uptime() + DISPLAY_WEBUI_INTERVAL;

void WebUILoop() {
  server.handleClient();

#  if defined(ESP32) && defined(MQTT_HTTPS_FW_UPDATE)
  if (localFirmwareRestartAt && (int32_t)(millis() - localFirmwareRestartAt) >= 0) {
    localFirmwareRestartAt = 0;
    Log.notice(F("[WebUI][OTA] HTTP response released; performing deferred restart" CR));
    ESPRestart(6);
    return;
  }
#  endif

  if (uptime() >= nextWebUIMessage && uxQueueMessagesWaiting(webUIQueue)) {
    webUIQueueMessage* message = nullptr;
    xQueueReceive(webUIQueue, &message, portMAX_DELAY);
    newSSD1306Message = true;

    if (currentWebUIMessage) {
      free(currentWebUIMessage);
    }
    currentWebUIMessage = message;
    nextWebUIMessage = uptime() + DISPLAY_WEBUI_INTERVAL;
  }
}

void XtoWebUI(const char* topicOri, JsonObject& WebUIdata) { // json object decoding
  bool success = false;
  if (cmpToMainTopic(topicOri, subjectMQTTtoWebUIset)) {
    WEBUI_TRACE_LOG(F("MQTTtoWebUI json set" CR));
    // properties
    if (WebUIdata.containsKey("displayMetric")) {
      displayMetric = WebUIdata["displayMetric"].as<bool>();
      Log.notice(F("Set displayMetric: %T" CR), displayMetric);
      success = true;
    }
    // save, load, init, erase
    if (WebUIdata.containsKey("save") && WebUIdata["save"]) {
      success = WebUIConfig_save();
      if (success) {
        Log.notice(F("WebUI config saved" CR));
      }
    } else if (WebUIdata.containsKey("load") && WebUIdata["load"]) {
      success = WebUIConfig_load();
      if (success) {
        Log.notice(F("WebUI config loaded" CR));
      }
    } else if (WebUIdata.containsKey("init") && WebUIdata["init"]) {
      WebUIConfig_init();
      success = true;
      if (success) {
        Log.notice(F("WebUI config initialised" CR));
      }
    } else if (WebUIdata.containsKey("erase") && WebUIdata["erase"]) {
      // Erase config from NVS (non-volatile storage)
      preferences.begin(Gateway_Short_Name, false);
      success = preferences.remove("WebUIConfig");
      preferences.end();
      if (success) {
        Log.notice(F("WebUI config erased" CR));
      }
    }
    if (success) {
      stateWebUIStatus();
    } else {
      Log.error(F("[ WebUI ] XtoWebUI Fail json" CR), WebUIdata);
    }
  }
}

String stateWebUIStatus() {
  //Publish display state
  StaticJsonDocument<JSON_MSG_BUFFER> WebUIdataBuffer;
  JsonObject WebUIdata = WebUIdataBuffer.to<JsonObject>();
  WebUIdata["displayMetric"] = (bool)displayMetric;
  WebUIdata["webUISecure"] = (bool)webUISecure;
  WebUIdata["displayQueue"] = uxQueueMessagesWaiting(webUIQueue);

  String output;
  serializeJson(WebUIdata, output);

  // WebUIdata["currentMessage"] = currentWebUIMessage;
  WebUIdata["origin"] = subjectWebUItoMQTT;
  if (!stateSnapshotOnly) enqueueJsonObject(WebUIdata);
  return output;
}

bool WebUIConfig_save() {
  StaticJsonDocument<JSON_MSG_BUFFER> jsonBuffer;
  JsonObject jo = jsonBuffer.to<JsonObject>();
  jo["displayMetric"] = (bool)displayMetric;
  jo["webUISecure"] = (bool)webUISecure;
  // Save config into NVS (non-volatile storage)
  String conf = "";
  serializeJson(jsonBuffer, conf);
  preferences.begin(Gateway_Short_Name, false);
  int result = preferences.putString("WebUIConfig", conf);
  preferences.end();
  Log.trace(F("[WebUI] WebUIConfig_save: %s, result: %d" CR), conf.c_str(), result);
  return true;
}

void WebUIConfig_init() {
  displayMetric = DISPLAY_METRIC;
  webUISecure = WEBUI_AUTH;
  Log.notice(F("WebUI config initialised" CR));
}

bool WebUIConfig_load() {
  StaticJsonDocument<JSON_MSG_BUFFER> jsonBuffer;
  preferences.begin(Gateway_Short_Name, true);
  if (preferences.isKey("WebUIConfig")) {
    auto error = deserializeJson(jsonBuffer, preferences.getString("WebUIConfig", "{}"));
    preferences.end();
    if (error) {
      Log.error(F("WebUI config deserialization failed: %s, buffer capacity: %u" CR), error.c_str(), jsonBuffer.capacity());
      return false;
    }
    if (jsonBuffer.isNull()) {
      Log.warning(F("WebUI config is null" CR));
      return false;
    }
    JsonObject jo = jsonBuffer.as<JsonObject>();
    displayMetric = jo["displayMetric"].as<bool>();
    webUISecure = jo["webUISecure"].as<bool>();
    return true;
  } else {
    preferences.end();
    Log.notice(F("No WebUI config to load" CR));
    return false;
  }
}

/*
Workaround for c not having a string based switch/case function
*/
constexpr unsigned int webUIHash(const char* s, int off = 0) { // workaround for switching on a string https://stackoverflow.com/a/46711735/18643696
  return !s[off] ? 5381 : (webUIHash(s, off + 1) * 33) ^ s[off];
}

/*
Parse json message from module into a format for display
*/
void webUIPubPrint(const char* topicori, JsonObject& data) {
  WEBUI_TRACE_LOG(F("[ webUIPubPrint ] pub %s " CR), topicori);
  if (webUIQueue) {
    webUIQueueMessage* message = (webUIQueueMessage*)heap_caps_calloc(1, sizeof(webUIQueueMessage), MALLOC_CAP_8BIT);
    if (message != NULL) {
      // Initalize message
      strlcpy(message->line1, "", WEBUI_TEXT_WIDTH);
      strlcpy(message->line2, "", WEBUI_TEXT_WIDTH);
      strlcpy(message->line3, "", WEBUI_TEXT_WIDTH);
      strlcpy(message->line4, "", WEBUI_TEXT_WIDTH);
      char* topic = strdup(topicori);
      strlcpy(message->title, strtok(topic, "/"), WEBUI_TEXT_WIDTH);
      free(topic);

      //  WEBUI_TRACE_LOG(F("[ webUIPubPrint ] switch %s " CR), message->title);
      switch (webUIHash(message->title)) {
        case webUIHash("SYStoMQTT"): {
          // Line 1

          if (data["version"]) {
            strlcpy(message->line1, data["version"], WEBUI_TEXT_WIDTH);
          } else {
            strlcpy(message->line1, "", WEBUI_TEXT_WIDTH);
          }

          // Line 2

          String uptime = data["uptime"];
          String line = "uptime: " + uptime;
          line.toCharArray(message->line2, WEBUI_TEXT_WIDTH);

          // Line 3

          String freemem = data["freemem"];
          line = "freemem: " + freemem;
          line.toCharArray(message->line3, WEBUI_TEXT_WIDTH);

          // Line 4

          String ip = data["ip"];
          line = "ip: " + ip;
          line.toCharArray(message->line4, WEBUI_TEXT_WIDTH);

          // Queue completed message

          if (xQueueSend(webUIQueue, (void*)&message, 0) != pdTRUE) {
            Log.warning(F("[ WebUI ] ERROR: webUIQueue full, discarding %s" CR), message->title);
            free(message);
          } else {
            // Log.notice(F("[ WebUI ] Queued %s" CR), message->title);
          }
          break;
        }

#  ifdef ZgatewayRTL_433
        case webUIHash("RTL_433toMQTT"): {
          if (data["model"] && strncmp(data["model"], "status", 6)) { // Does not contain "status"
            // {"model":"Acurite-Tower","id":2043,"channel":"B","battery_ok":1,"temperature_C":5.3,"humidity":81,"mic":"CHECKSUM","protocol":"Acurite 592TXR Temp/Humidity, 5n1 Weather Station, 6045 Lightning, 3N1, Atlas","rssi":-81,"duration":121060}

            // Line 1

            strlcpy(message->line1, data["model"], WEBUI_TEXT_WIDTH);

            // Line 2

            String line2 = "";
            if (data["id"]) {
              String id = data["id"];
              line2 += "id: " + id + " ";
            }

            if (data["channel"]) {
              String channel = data["channel"];
              line2 += "channel: " + channel;
            }
            line2.toCharArray(message->line2, WEBUI_TEXT_WIDTH);
            // Line 3

            String line3 = "";

            if (data.containsKey("temperature_C")) {
              float temperature_C = data["temperature_C"];
              char temp[5];

              if (displayMetric) {
                dtostrf(temperature_C, 3, 1, temp);
                line3 = "temp: " + (String)temp + "°C ";
              } else {
                dtostrf(convertTemp_CtoF(temperature_C), 3, 1, temp);
                line3 = "temp: " + (String)temp + "°F ";
              }
            }

            float humidity = data["humidity"];
            if (data.containsKey("humidity") && humidity <= 100 && humidity >= 0) {
              char hum[5];
              dtostrf(humidity, 3, 1, hum);
              line3 += "hum: " + (String)hum + "% ";
            }
            if (data.containsKey("wind_avg_km_h")) {
              float wind_avg_km_h = data["wind_avg_km_h"];
              char wind[6];

              if (displayMetric) {
                dtostrf(wind_avg_km_h, 3, 1, wind);
                line3 += "wind: " + (String)wind + "km/h ";
              } else {
                dtostrf(convert_kmph2mph(wind_avg_km_h), 3, 1, wind);
                line3 += "wind: " + (String)wind + "mp/h ";
              }
            }

            float moisture = data["moisture"];
            if (data.containsKey("moisture") && moisture <= 100 && moisture >= 0) {
              char moist[5];
              dtostrf(moisture, 3, 1, moist);
              line3 += "moist: " + (String)moist + "% ";
            }

            line3.toCharArray(message->line3, WEBUI_TEXT_WIDTH);

            // Line 4

            String line4 = "";
            if (data["battery_ok"]) {
              line4 = "batt: " + data["battery_ok"].as<String>();
            } else {
              line4 = "pulses: " + data["pulses"].as<String>();
            }

            line4 += " rssi: " + data["rssi"].as<String>();
            line4.toCharArray(message->line4, WEBUI_TEXT_WIDTH);

            // Queue completed message

            if (xQueueSend(webUIQueue, (void*)&message, 0) != pdTRUE) {
              Log.warning(F("[ WebUI ] webUIQueue full, discarding signal %s" CR), message->title);
              free(message);
            } else {
              // Log.notice(F("[ WebUI ] Queued %s" CR), message->title);
            }
          } else {
            Log.error(F("[ WebUI ] rtl_433 not displaying %s" CR), message->title);
            free(message);
          }
          break;
        }
#  endif
#  ifdef ZsensorBME280
        case webUIHash("CLIMAtoMQTT"): {
          // {"tempc":17.06,"tempf":62.708,"hum":50.0752,"pa":98876.14,"altim":205.8725,"altift":675.4348}

          // Line 1

          strlcpy(message->line1, "bme280", WEBUI_TEXT_WIDTH);

          // Line 2

          String line2 = "";
          if (data.containsKey("tempc")) {
            char temp[5];
            float temperature_C = data["tempc"];

            if (displayMetric) {
              dtostrf(temperature_C, 3, 1, temp);
              line2 = "temp: " + (String)temp + "°C ";
            } else {
              dtostrf(convertTemp_CtoF(temperature_C), 3, 1, temp);
              line2 = "temp: " + (String)temp + "°F ";
            }
          }
          line2.toCharArray(message->line2, WEBUI_TEXT_WIDTH);

          // Line 3

          String line3 = "";
          float humidity = data["hum"];
          if (data.containsKey("hum") && humidity <= 100 && humidity >= 0) {
            char hum[5];
            dtostrf(humidity, 3, 1, hum);
            line3 += "hum: " + (String)hum + "% ";
          }
          line3.toCharArray(message->line3, WEBUI_TEXT_WIDTH);

          // Line 4

          float pa = (int)data["pa"] / 100;
          char pressure[6];

          String line4 = "";
          if (displayMetric) {
            dtostrf(pa, 3, 1, pressure);
            line4 = "pressure: " + (String)pressure + " hPa";
          } else {
            dtostrf(convert_hpa2inhg(pa), 3, 1, pressure);
            line4 = "pressure: " + (String)pressure + " inHg";
          }
          line4.toCharArray(message->line4, WEBUI_TEXT_WIDTH);

          // Queue completed message

          if (xQueueSend(webUIQueue, (void*)&message, 0) != pdTRUE) {
            Log.warning(F("[ WebUI ] webUIQueue full, discarding signal %s" CR), message->title);
            free(message);
          } else {
            // Log.notice(F("[ WebUI ] Queued %s" CR), message->title);
          }
          break;
        }
#  endif
#  ifdef ZgatewayBT
        case webUIHash("BTtoMQTT"): {
          // {"id":"AA:BB:CC:DD:EE:FF","mac_type":0,"adv_type":0,"name":"sps","manufacturerdata":"de071f1000b1612908","rssi":-70,"brand":"Inkbird","model":"T(H) Sensor","model_id":"IBS-TH1/TH2/P01B","type":"THBX","cidc":false,"acts":true,"tempc":20.14,"tempf":68.252,"hum":41.27,"batt":41}

          if (data["model_id"] != "MS-CDP" && data["model_id"] != "GAEN" && data["model_id"] != "APPLE_CONT" && data["model_id"] != "IBEACON") {
            // Line 2, 3, 4
            String line2 = "";
            String line3 = "";
            String line4 = "";

            // Properties
            String properties[6] = {"", "", "", "", "", ""};
            int property = -1;

            if (data["type"] == "THB" || data["type"] == "THBX" || data["type"] == "PLANT" || data["type"] == "AIR" || data["type"] == "BATT" || data["type"] == "ACEL" || (data["type"] == "UNIQ" && data["model_id"] == "SDLS")) {
              if (data.containsKey("tempc")) {
                property++;
                char temp[5];
                if (displayMetric) {
                  float temperature = data["tempc"];
                  dtostrf(temperature, 3, 1, temp);
                  properties[property] = "temp: " + (String)temp + "°C ";
                } else {
                  float temperature = data["tempf"];
                  dtostrf(temperature, 3, 1, temp);
                  properties[property] = "temp: " + (String)temp + "°F ";
                }
              }

              if (data.containsKey("tempc2_dp")) {
                property++;
                char tempdp[5];
                if (displayMetric) {
                  float temperature = data["tempc2_dp"];
                  dtostrf(temperature, 3, 1, tempdp);
                  properties[property] = "dewp: " + (String)tempdp + "°C ";
                } else {
                  float temperature = data["tempf2_dp"];
                  dtostrf(temperature, 3, 1, tempdp);
                  properties[property] = "dewp: " + (String)tempdp + "°F ";
                }
              }

              if (data.containsKey("extprobe")) {
                property++;
                properties[property] = " ext. probe";
              }

              if (data.containsKey("hum")) {
                property++;
                float humidity = data["hum"];
                char hum[5];

                dtostrf(humidity, 3, 1, hum);
                properties[property] = "hum: " + (String)hum + "% ";
              }

              if (data.containsKey("pm25")) {
                property++;
                int pm25int = data["pm25"];
                char pm25[3];
                itoa(pm25int, pm25, 10);
                if ((data.containsKey("pm10"))) {
                  properties[property] = "PM 2.5: " + (String)pm25 + " ";

                } else {
                  properties[property] = "pm2.5: " + (String)pm25 + "μg/m³ ";
                }
              }

              if (data.containsKey("pm10")) {
                property++;
                int pm10int = data["pm10"];
                char pm10[3];
                itoa(pm10int, pm10, 10);
                if ((data.containsKey("pm25"))) {
                  properties[property] = "/ 10: " + (String)pm10 + "μg/m³ ";

                } else {
                  properties[property] = "pm10: " + (String)pm10 + "μg/m³ ";
                }
              }

              if (data.containsKey("for")) {
                property++;
                int formint = data["for"];
                char form[3];
                itoa(formint, form, 10);
                properties[property] = "CH₂O: " + (String)form + "mg/m³ ";
              }

              if (data.containsKey("co2")) {
                property++;
                int co2int = data["co2"];
                char co2[4];
                itoa(co2int, co2, 10);
                properties[property] = "co2: " + (String)co2 + "ppm ";
              }

              if (data.containsKey("moi")) {
                property++;
                int moiint = data["moi"];
                char moi[4];
                itoa(moiint, moi, 10);
                properties[property] = "moi: " + (String)moi + "% ";
              }

              if (data.containsKey("lux")) {
                property++;
                int luxint = data["lux"];
                char lux[5];
                itoa(luxint, lux, 10);
                properties[property] = "lux: " + (String)lux + "lx ";
              }

              if (data.containsKey("fer")) {
                property++;
                int ferint = data["fer"];
                char fer[7];
                itoa(ferint, fer, 10);
                properties[property] = "fer: " + (String)fer + "µS/cm ";
              }

              if (data.containsKey("pres")) {
                property++;
                int presint = data["pres"];
                char pres[4];
                itoa(presint, pres, 10);
                properties[property] = "pres: " + (String)pres + "hPa ";
              }

              if (data.containsKey("batt")) {
                property++;
                int battery = data["batt"];
                char batt[5];
                itoa(battery, batt, 10);
                properties[property] = "batt: " + (String)batt + "% ";
              }

              if (data.containsKey("shake")) {
                property++;
                int shakeint = data["shake"];
                char shake[3];
                itoa(shakeint, shake, 10);
                properties[property] = "shake: " + (String)shake + " ";
              }

              if (data.containsKey("volt")) {
                property++;
                float voltf = data["volt"];
                char volt[5];
                dtostrf(voltf, 3, 1, volt);
                properties[property] = "volt: " + (String)volt + "V ";
              }

              if (data.containsKey("wake")) {
                property++;
                String wakestr = data["wake"];
                properties[property] = "wake: " + wakestr + " ";
              }

              if (data.containsKey("gravity")) {
                property++;
                property++;
                char sgrav[6];
                float gravityf = data["gravity"];
                dtostrf(gravityf, 5, 3, sgrav);
                properties[property] = "SG: " + (String)sgrav + " ";
              }

            } else if (data["type"] == "BBQ") {
              String tempcstr = "";
              int j = 7;
              if (data["model_id"] == "IBT-2X(S)") {
                j = 3;
              } else if (data["model_id"] == "IBT-4X(S/C)") {
                j = 5;
              }

              for (int i = 0; i < j; i++) {
                if (i == 0) {
                  if (displayMetric) {
                    tempcstr = "tempc";
                  } else {
                    tempcstr = "tempf";
                  }
                  i++;
                } else {
                  if (displayMetric) {
                    tempcstr = "tempc" + (String)i;
                  } else {
                    tempcstr = "tempf" + (String)i;
                  }
                }

                if (data.containsKey(tempcstr)) {
                  char temp[5];
                  float temperature = data[tempcstr];
                  dtostrf(temperature, 3, 1, temp);
                  properties[i - 1] = "tp" + (String)i + ": " + (String)temp;
                  if (displayMetric) {
                    properties[i - 1] += "°C ";
                  } else {
                    properties[i - 1] += "°F ";
                  }
                } else {
                  properties[i - 1] = "tp" + (String)i + ": " + "off ";
                }
              }
            } else if (data["type"] == "BODY") {
              if (data.containsKey("steps")) {
                property++;
                int stepsint = data["steps"];
                char steps[5];
                itoa(stepsint, steps, 10);
                properties[property] = "steps: " + (String)steps + " ";
                // next line
                property++;
              }

              if (data.containsKey("act_bpm")) {
                property++;
                int actbpmint = data["act_bpm"];
                char actbpm[3];
                itoa(actbpmint, actbpm, 10);
                properties[property] = "activity bpm: " + (String)actbpm + " ";
              }

              if (data.containsKey("bpm")) {
                property++;
                int bpmint = data["bpm"];
                char bpm[3];
                itoa(bpmint, bpm, 10);
                properties[property] = "bpm: " + (String)bpm + " ";
              }
            } else if (data["type"] == "SCALE") {
              if (data.containsKey("weighing_mode")) {
                property++;
                String mode = data["weighing_mode"];
                properties[property] = mode + " ";
                // next line
                property++;
              }

              if (data.containsKey("weight")) {
                property++;
                float weightf = data["weight"];
                char weight[7];
                dtostrf(weightf, 3, 1, weight);
                if (data.containsKey("unit")) {
                  String unit = data["unit"];
                  properties[property] = "weight: " + (String)weight + unit + " ";
                } else {
                  properties[property] = "weight: " + (String)weight;
                }
                // next line
                property++;
              }

              if (data.containsKey("impedance")) {
                property++;
                int impint = data["impedance"];
                char imp[3];
                itoa(impint, imp, 10);
                properties[property] = "impedance: " + (String)imp + "ohm ";
              }
            } else if (data["type"] == "UNIQ") {
              if (data["model_id"] == "M1017" || data["model_id"] == "HOBOMX2001") {
                if (data.containsKey("lvl_cm")) {
                  property++;
                  char lvl[5];
                  if (displayMetric) {
                    float lvlf = data["lvl_cm"];
                    dtostrf(lvlf, 3, 1, lvl);
                    properties[property] = "level: " + (String)lvl + "cm ";
                  } else {
                    float lvlf = data["lvl_in"];
                    dtostrf(lvlf, 3, 1, lvl);
                    properties[property] = "level: " + (String)lvl + "\" ";
                  }
                }

                if (data.containsKey("quality")) {
                  property++;
                  int qualint = data["quality"];
                  char qual[3];
                  itoa(qualint, qual, 10);
                  properties[property] = "qy: " + (String)qual + " ";
                }

                if (data.containsKey("batt")) {
                  property++;
                  int battery = data["batt"];
                  char batt[5];
                  itoa(battery, batt, 10);
                  properties[property] = "batt: " + (String)batt + "% ";
                }
              }
            }

            line2 = properties[0] + properties[1];
            line3 = properties[2] + properties[3];
            line4 = properties[4] + properties[5];

            if (!(line2 == "" && line3 == "" && line4 == "")) {
              // Titel
              char* topic = strdup(topicori);
              String heading = strtok(topic, "/");
              String line0 = heading + "           " + data["id"].as<String>().substring(9, 17);
              line0.toCharArray(message->title, WEBUI_TEXT_WIDTH);
              free(topic);

              // Line 1
              strlcpy(message->line1, data["model"], WEBUI_TEXT_WIDTH);

              line2.toCharArray(message->line2, WEBUI_TEXT_WIDTH);
              line3.toCharArray(message->line3, WEBUI_TEXT_WIDTH);
              line4.toCharArray(message->line4, WEBUI_TEXT_WIDTH);

              if (xQueueSend(webUIQueue, (void*)&message, 0) != pdTRUE) {
                Log.warning(F("[ WebUI ] webUIQueue full, discarding signal %s" CR), message->title);
                free(message);
              } else {
                // Log.notice(F("[ WebUI ] Queued %s" CR), message->title);
              }
            } else {
              WEBUI_TRACE_LOG(F("[ WebUI ] incomplete messaage %s" CR), topicori);
              free(message);
            }

            break;
          } else {
            WEBUI_TRACE_LOG(F("[ WebUI ] incorrect model_id %s" CR), topicori);
            free(message);
            break;
          }
        }
#  endif
#  ifdef ZsensorRN8209
        case webUIHash("RN8209toMQTT"): {
          // {"volt":1073178,"current":0,"power":0}

          // Line 1

          String line1 = "";
          if (data.containsKey("volt")) {
            char volt[5];
            float voltage = data["volt"];
            dtostrf(voltage, 3, 1, volt);
            line1 = "volt: " + (String)volt;
          }
          line1.toCharArray(message->line1, WEBUI_TEXT_WIDTH);

          // Line 2

          String line2 = "";
          if (data.containsKey("current")) {
            char curr[5];
            float current = data["current"];
            dtostrf(current, 3, 1, curr);
            line2 = "current: " + (String)curr + " A";
          }
          line2.toCharArray(message->line2, WEBUI_TEXT_WIDTH);

          // Line 3

          String line3 = "";
          if (data.containsKey("power")) {
            char pow[5];
            float power = data["power"];
            dtostrf(power, 3, 1, pow);
            line3 = "power: " + (String)pow + " W";
          }
          line3.toCharArray(message->line3, WEBUI_TEXT_WIDTH);

          // Queue completed message

          if (xQueueSend(webUIQueue, (void*)&message, 0) != pdTRUE) {
            Log.warning(F("[ WebUI ] webUIQueue full, discarding signal %s" CR), message->title);
            free(message);
          } else {
            // Log.notice(F("[ WebUI ] Queued %s" CR), message->title);
          }
          break;
        }
#  endif
#  ifdef ZgatewayLORA
        case webUIHash("LORAtoMQTT"): {
          // {"tempc":25.4,"hum":0,"batt":0}

          String line1 = "";
          if (data.containsKey("tempc")) {
            char temp[5];
            float temperature_C = data["tempc"];

            if (displayMetric) {
              dtostrf(temperature_C, 3, 1, temp);
              line1 = "temp: " + (String)temp + "°C ";
            } else {
              dtostrf(convertTemp_CtoF(temperature_C), 3, 1, temp);
              line1 = "temp: " + (String)temp + "°F ";
            }
          }
          line1.toCharArray(message->line1, WEBUI_TEXT_WIDTH);

          // Line 2

          String line2 = "";
          float humidity = data["hum"];
          if (data.containsKey("hum") && humidity <= 100 && humidity >= 0) {
            char hum[5];
            dtostrf(humidity, 3, 1, hum);
            line2 += "hum: " + (String)hum + "% ";
          }
          line2.toCharArray(message->line2, WEBUI_TEXT_WIDTH);

          // Line 3

          String line3 = "";
          float adc = data["adc"];
          if (data.containsKey("adc") && adc <= 100 && adc >= 0) {
            char cAdc[5];
            dtostrf(adc, 3, 1, cAdc);
            line3 += "adc: " + (String)cAdc + "µS/cm ";
          }
          line3.toCharArray(message->line2, WEBUI_TEXT_WIDTH);

          // Queue completed message

          if (xQueueSend(webUIQueue, (void*)&message, 0) != pdTRUE) {
            Log.warning(F("[ WebUI ] webUIQueue full, discarding signal %s" CR), message->title);
            free(message);
          } else {
            // Log.notice(F("[ WebUI ] Queued %s" CR), message->title);
          }
          break;
        }
#  endif
        default:
          Log.verbose(F("[ WebUI ] unhandled topic %s" CR), message->title);
          free(message);
      }
    } else {
      Log.error(F("[ WebUI ] insufficent memory " CR));
    }
  } else {
    Log.error(F("[ WebUI ] not initalized " CR));
  }
}

/*------------------- Serial logging interceptor ----------------------*/

// This pattern was borrowed from HardwareSerial and modified to support the WebUI display

SerialWeb WebLog(0); // Not sure about this, came from Hardwareserial
SerialWeb::SerialWeb(int x) {
}

/*
Initialize WebUI oled display for use, and display OMG logo
*/
void SerialWeb::begin() {
  // WebUI.begin(); // User OMG serial support
}

/*
Dummy virtual functions carried over from Serial
*/
int SerialWeb::available(void) {
}

/*
Dummy virtual functions carried over from Serial
*/
int SerialWeb::peek(void) {
}

/*
Dummy virtual functions carried over from Serial
*/
int SerialWeb::read(void) {
}

/*
Dummy virtual functions carried over from Serial
*/
void SerialWeb::flush(void) {
}

/*
Write line of text to the display with vertical scrolling of screen
*/
size_t SerialWeb::write(const uint8_t* buffer, size_t size) {
  // Default to Serial output if the display is not available
  addLog(buffer, size);
  return Serial.write(buffer, size);
}

char line[ROW_LENGTH];
int lineIndex = 0;
void addLog(const uint8_t* buffer, size_t size) {
  for (int i = 0; i < size; i++) {
    if (char(buffer[i]) == 10 | lineIndex > ROW_LENGTH - 2) {
      if (char(buffer[i]) != 10) {
        line[lineIndex++] = char(buffer[i]);
      }
      line[lineIndex++] = char(0);
      AddLogData(1, (const char*)&line[0]);
      lineIndex = 0;
    } else {
      line[lineIndex++] = char(buffer[i]);
    }
  }
}

#endif
