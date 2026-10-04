"""Run actual GPIO state/command functions with mocked pins and preferences.

No ESP32 pins, MQTT broker or Home Assistant entities are changed. The host
does not reproduce ESP32 electrical behavior or FreeRTOS scheduling.
"""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "main/ZsensorGPIOInput.ino").read_text(encoding="utf-8")
functions = source[source.index("struct GPIOInputChannelState_s {"):source.rindex("#endif")]
discovery = (ROOT / "main/ZmqttDiscovery.ino").read_text(encoding="utf-8")
cleanup_start = discovery.index("void cleanupMqttDiscovery() {")
cleanup_end = discovery.index("\n#  if defined(ZgatewayBT)", cleanup_start)
cleanup = discovery[cleanup_start:cleanup_end]
harness = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cctype>
#define ESP32
#define ZradioCC1101
#define ZgatewayRF
#define ZsensorGPIOInput
#define ZgatewayBLETracker
#define GPIO_INPUT_RUNTIME_CONFIG
#define GPIO_INPUT_MAX 2
#define GPIO_OUTPUT_MAX 2
#define GPIO_INPUT_ALLOWED_MASK 0x9F06636010ULL
#define GPIO_OUTPUT_ALLOWED_MASK 0x306636010ULL
#define INPUT_GPIO 4
#define INPUT 1
#define OUTPUT 3
#define INPUT_PULLUP 5
#define INPUT_PULLDOWN 9
#define OUTPUT_OPEN_DRAIN 19
#define GPIO_INPUT_TYPE INPUT
#define HIGH 1
#define LOW 0
#define RF_MODULE_CS 5
#define RF_MODULE_GDO0 12
#define RF_MODULE_GDO2 27
#define JSON_MSG_BUFFER 1024
#define F(value) value
#define CR "\n"
const int SCK = 18, MISO = 19, MOSI = 23, SS = 5;
class String : public std::string {
public:
  using std::string::string;
  String(const std::string &value) : std::string(value) {}
  String(int value) : std::string(std::to_string(value)) {}
  void toUpperCase() { std::transform(begin(), end(), begin(), [](unsigned char c) { return std::toupper(c); }); }
};
void convertToJson(const String &value, JsonVariant destination) {
  destination.set(static_cast<const std::string &>(value));
}
#include "config_GPIOInput.h"
#include "config_BLETracker.h"
BLETrackerConfig_s BLETrackerConfig[BLE_TRACKER_MAX] = {};
BLETrackerConfig_s getBLETrackerConfig(uint8_t slot) { assert(slot < BLE_TRACKER_MAX); return BLETrackerConfig[slot]; }
std::vector<std::string> discoveryCleanup;
String getUniqueId(const char *name, const char *) { return std::string("uid-") + name; }
void eraseTopic(const char *type, const char *id) { discoveryCleanup.push_back(std::string(type) + "/" + id); }
struct {
  template <typename... Args> void notice(const char *, Args...) {}
  template <typename... Args> void warning(const char *, Args...) {}
  template <typename... Args> void error(const char *, Args...) {}
  template <typename... Args> void trace(const char *, Args...) {}
} Log;
uint32_t fakeNow = 1000;
unsigned long millis() { return fakeNow; }
void yield() {}
int readings[40] = {}, modes[40] = {};
struct PinEvent { char operation; unsigned pin; int value; };
std::vector<PinEvent> events;
int digitalRead(unsigned pin) { assert(pin < 40); return readings[pin]; }
void digitalWrite(unsigned pin, int value) { assert(pin < 40); readings[pin] = value; events.push_back({'W', pin, value}); }
void pinMode(unsigned pin, int mode) { assert(pin < 40); modes[pin] = mode; events.push_back({'M', pin, mode}); }
const char *Gateway_Short_Name = "test", *mqtt_topic = "test/", *gateway_name = "mock";
struct { bool connected() { return true; } } client;
decltype(client) *mqtt = &client;
struct {
  std::map<std::string, bool> values;
  unsigned writes = 0;
  void begin(const char *, bool) {}
  void end() {}
  size_t putBool(const char *key, bool value) { values[key] = value; ++writes; return 1; }
  bool getBool(const char *key, bool fallback) { auto entry = values.find(key); return entry == values.end() ? fallback : entry->second; }
} preferences;
bool failQueue = false;
int queueLength = 0;
std::vector<std::string> publications, cleared;
bool enqueueJsonObject(JsonObject object) {
  if (failQueue) return false;
  std::string json; serializeJson(object, json); publications.push_back(json); return true;
}
bool pubMQTT(const char *topic, const char *payload, bool retained) {
  assert(!payload[0] && retained); cleared.emplace_back(topic); return true;
}
__FUNCTIONS__
__CLEANUP__
void reset() {
  std::memset(gpioInputChannels, 0, sizeof(gpioInputChannels));
  std::memset(gpioInputStates, 0, sizeof(gpioInputStates));
  std::memset(gpioOutputChannels, 0, sizeof(gpioOutputChannels));
  std::memset(gpioOutputStates, 0, sizeof(gpioOutputStates));
  std::memset(BLETrackerConfig, 0, sizeof(BLETrackerConfig)); discoveryCleanup.clear();
  std::memset(readings, 0, sizeof(readings)); std::memset(modes, 0, sizeof(modes));
  events.clear(); publications.clear(); cleared.clear(); preferences.values.clear(); preferences.writes = 0;
  fakeNow = 1000; failQueue = false;
}
void input(bool enabled = true, int pin = 4, int level = HIGH) {
  gpioInputChannels[0] = {enabled, static_cast<uint8_t>(pin), "Test input", GPIO_INPUT_MODE_INPUT,
                         static_cast<uint8_t>(level), 60, true, GPIO_INPUT_CLASS_GARAGE_DOOR};
}
void output(int startup = GPIO_OUTPUT_STARTUP_OFF, int level = HIGH, int mode = GPIO_OUTPUT_MODE_PUSH_PULL) {
  gpioOutputChannels[0] = {true, 16, "Test output", static_cast<uint8_t>(mode),
                          static_cast<uint8_t>(level), static_cast<uint8_t>(startup), true};
}
void command(const char *topic, const char *json) {
  StaticJsonDocument<512> document; assert(!deserializeJson(document, json));
  JsonObject object = document.as<JsonObject>(); XtoGPIOOutput(topic, object);
}
StaticJsonDocument<1024> lastDocument;
JsonObject lastPublication() {
  assert(!publications.empty()); lastDocument.clear();
  assert(!deserializeJson(lastDocument, publications.back())); return lastDocument.as<JsonObject>();
}
int main() {
  static_assert(sizeof(unsigned long) == sizeof(uint32_t), "Use a 32-bit unsigned-long host ABI");
  assert(!gpioInputPinValidationError(4, GPIO_INPUT_MODE_INPUT));
  assert(!gpioInputPinValidationError(13, GPIO_INPUT_MODE_PULLUP));
  assert(!gpioInputPinValidationError(14, GPIO_INPUT_MODE_PULLDOWN));
  assert(!gpioInputPinValidationError(34, GPIO_INPUT_MODE_INPUT));
  assert(gpioInputPinValidationError(34, GPIO_INPUT_MODE_PULLUP));
  assert(gpioInputPinValidationError(39, GPIO_INPUT_MODE_PULLDOWN));
  for (int pin : {-1, 0, 5, 6, 12, 18, 19, 23, 27, 40, 64})
    assert(gpioInputPinValidationError(pin, GPIO_INPUT_MODE_INPUT));
  assert(gpioInputPinValidationError(4, GPIO_INPUT_MODE_COUNT));
  assert(gpioOutputPinValidationError(34, GPIO_OUTPUT_MODE_PUSH_PULL));
  assert(gpioOutputPinValidationError(16, GPIO_OUTPUT_MODE_COUNT));
  assert(!gpioOutputPinValidationError(16, GPIO_OUTPUT_MODE_OPEN_DRAIN));

  reset(); input(); setupGPIOInput(); assert(modes[4] == INPUT);
  fakeNow += 60; MeasureGPIOInput(); assert(publications.empty());
  ++fakeNow; MeasureGPIOInput(); assert(!lastPublication()["active"].as<bool>());
  readings[4] = HIGH; ++fakeNow; MeasureGPIOInput();
  fakeNow += 30; readings[4] = LOW; MeasureGPIOInput(); // Bounce restarts debounce.
  fakeNow += 30; readings[4] = HIGH; MeasureGPIOInput();
  fakeNow += 61; failQueue = true; MeasureGPIOInput(); assert(gpioInputStates[0].publishPending);
  fakeNow += 999; failQueue = false; MeasureGPIOInput(); assert(publications.size() == 1);
  ++fakeNow; MeasureGPIOInput(); assert(lastPublication()["active"].as<bool>());
  assert(!gpioInputStates[0].publishPending);

  reset(); fakeNow = UINT32_MAX - 30; input(true, 4, LOW); setupGPIOInput();
  fakeNow = 30; MeasureGPIOInput(); assert(lastPublication()["active"].as<bool>());
  reset(); input(); gpioInputChannels[0].mode = GPIO_INPUT_MODE_PULLUP; setupGPIOInput(); assert(modes[4] == INPUT_PULLUP);
  reset(); input(); gpioInputChannels[0].mode = GPIO_INPUT_MODE_PULLDOWN; setupGPIOInput(); assert(modes[4] == INPUT_PULLDOWN);
  reset(); input(); gpioInputChannels[1] = gpioInputChannels[0]; setupGPIOInput(); assert(!gpioInputChannels[1].enabled);
  reset(); input(true, 16); setupGPIOInput(); events.clear(); output(); setupGPIOOutput();
  assert(!gpioOutputChannels[0].enabled && events.empty());

  for (int level : {LOW, HIGH}) for (int mode : {GPIO_OUTPUT_MODE_PUSH_PULL, GPIO_OUTPUT_MODE_OPEN_DRAIN}) {
    reset(); output(GPIO_OUTPUT_STARTUP_OFF, level, mode); setupGPIOOutput();
    assert(events.size() == 3 && events[0].operation == 'W' && events[1].operation == 'M');
    assert(!gpioOutputIsOn(0) && readings[16] == !level);
    assert(modes[16] == (mode == GPIO_OUTPUT_MODE_OPEN_DRAIN ? OUTPUT_OPEN_DRAIN : OUTPUT));
    command("/commands/MQTTtoGPIOOutput/1", "{\"state\":\"on\"}");
    assert(gpioOutputIsOn(0) && readings[16] == level && lastPublication()["active"].as<bool>());
    command("test/mock/commands/MQTTtoGPIOOutput/1", "{\"state\":\"TOGGLE\"}");
    assert(!gpioOutputIsOn(0) && readings[16] == !level);
    command("/commands/MQTTtoGPIOOutput", "{\"channel\":1,\"state\":true}"); assert(gpioOutputIsOn(0));
    command("/commands/MQTTtoGPIOOutput/1", "{\"state\":0}"); assert(!gpioOutputIsOn(0));
    const size_t writes = events.size();
    command("/commands/MQTTtoGPIOOutput/1", "{\"state\":\"invalid\"}");
    command("/commands/MQTTtoGPIOOutput", "{\"channel\":3,\"state\":true}");
    command("/unrelated", "{\"state\":true}"); assert(events.size() == writes);
    assert(preferences.writes == 0);
  }
  reset(); output(GPIO_OUTPUT_STARTUP_ON); setupGPIOOutput(); assert(gpioOutputIsOn(0));
  reset(); output(GPIO_OUTPUT_STARTUP_RESTORE); preferences.values["gpioOut1"] = true;
  setupGPIOOutput(); assert(gpioOutputIsOn(0));
  command("/commands/MQTTtoGPIOOutput/1", "{\"state\":false}"); assert(preferences.writes == 1);
  command("/commands/MQTTtoGPIOOutput/1", "{\"state\":false}"); assert(preferences.writes == 1);
  failQueue = true; command("/commands/MQTTtoGPIOOutput/1", "{\"state\":true}");
  assert(gpioOutputStates[0].publishPending); failQueue = false; fakeNow += 1000; MeasureGPIOInput();
  assert(!gpioOutputStates[0].publishPending && lastPublication()["active"].as<bool>());
  reset(); command("/commands/MQTTtoGPIOOutput/1", "{\"state\":true}"); assert(events.empty());
  reset(); forcePublishGPIOState(); assert(cleared.size() == 8); // Disabled slots and old inputs.
  reset(); cleanupMqttDiscovery(); assert(discoveryCleanup.size() == 16);
  assert(std::find(discoveryCleanup.begin(), discoveryCleanup.end(), "switch/uid-discovery") != discoveryCleanup.end());
  assert(std::find(discoveryCleanup.begin(), discoveryCleanup.end(), "binary_sensor/uid-GPIOInput-4") != discoveryCleanup.end());
  assert(std::find(discoveryCleanup.begin(), discoveryCleanup.end(), "sensor/mock-ble-tracker-4-rssi") != discoveryCleanup.end());
  reset(); input(); output(); BLETrackerConfig[0].enabled = true;
  std::strcpy(BLETrackerConfig[0].mac, "00:11:22:33:44:55"); cleanupMqttDiscovery();
  assert(discoveryCleanup.size() == 12);
  assert(std::find(discoveryCleanup.begin(), discoveryCleanup.end(), "binary_sensor/uid-GPIOInput") == discoveryCleanup.end());
  assert(std::find(discoveryCleanup.begin(), discoveryCleanup.end(), "switch/uid-GPIOOutput") == discoveryCleanup.end());
  assert(std::find(discoveryCleanup.begin(), discoveryCleanup.end(), "binary_sensor/mock-ble-tracker-1") == discoveryCleanup.end());
  std::puts("PASS: actual GPIO safe pins/modes, inversion, debounce/retry/rollover, duplicate rejection, commands/startup/restore, retained and HA discovery cleanup");
}
'''.replace("__FUNCTIONS__", functions).replace("__CLEANUP__", cleanup)

with tempfile.TemporaryDirectory(prefix="omg-gpio-behavior-") as directory:
    cpp = Path(directory) / "behavior.cpp"
    binary = Path(directory) / ("behavior.exe" if os.name == "nt" else "behavior")
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-missing-field-initializers", "-O2", "-I", str(ROOT / "main"),
                    "-I", str(ROOT / ".pio/libdeps/esp32dev-multi_receiver-wol-gpio-ble/ArduinoJson/src"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
