"""Execute actual WOL parser, packet and outage functions with local mocks.

No networking or device mutations. Windows builds also place short strings
at the end of readable memory to check termination before the next read.
"""
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "main/main.ino").read_text(encoding="utf-8")
start = source.index("static bool mqttWOLTrackingDisconnect = false;")
end = source.index("\n#endif", source.index("static void mqttWOLDisconnected(", start))
functions = source[start:end]
config = re.search(r"struct MQTTWOLConfig_s \{.*?\n\};", source, re.S).group()
connection = (ROOT / ".pio/libdeps/esp32dev-multi_receiver-wol-gpio-ble/PicoMQTT/src/PicoMQTT/connection.h").read_text(encoding="utf-8")
enum = re.search(r"enum ConnectReturnCode : uint8_t \{.*?\n\};", connection, re.S).group()

harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <limits>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "config_mqttWOL.h"
using byte = uint8_t;
#define F(value) value
#define CR "\n"
namespace PicoMQTT { __ENUM__ }
__CONFIG__
MQTTWOLConfig_s mqttWOLConfig = {};
struct {
  template <typename... Args> void notice(const char *, Args...) {}
  template <typename... Args> void warning(const char *, Args...) {}
  template <typename... Args> void error(const char *, Args...) {}
  template <typename... Args> void trace(const char *, Args...) {}
} Log;
uint32_t fakeNow = 1000;
unsigned long millis() { return fakeNow; }
int failure_number_mqtt = 0;
const int WL_CONNECTED = 3;
struct { int value = WL_CONNECTED; int status() { return value; } } WiFi;
struct MQTT { bool online = false; bool connected() { return online; } } client;
MQTT *mqtt = &client;
const char *mqttConnectReturnCodeName(PicoMQTT::ConnectReturnCode) { return "mock"; }
struct IPAddress { IPAddress(int, int, int, int) {} };
unsigned beginCalls = 0, endCalls = 0, stopCalls = 0;
bool beginOK = true, endOK = true;
std::vector<byte> packet;
struct WiFiUDP {
  bool beginPacket(IPAddress, uint16_t port) { ++beginCalls; assert(port == 9); return beginOK; }
  size_t write(const byte *bytes, size_t size) { packet.assign(bytes, bytes + size); return size; }
  int endPacket() { ++endCalls; return endOK ? 1 : 0; }
  void stop() { ++stopCalls; }
};
__FUNCTIONS__
void reset() {
  mqttWOLConnected();
  mqttWOLConfig = {true, "00:11:22:33:44:55", 1000, 3, 5000, true, false, false};
  fakeNow = 1000; failure_number_mqtt = 3; client.online = false;
  WiFi.value = WL_CONNECTED; beginCalls = endCalls = stopCalls = 0;
  beginOK = endOK = true; packet.clear();
}
int main() {
  // Windows/ESP32 unsigned long is 32 bits: exercise the real wrap semantics.
  static_assert(sizeof(unsigned long) == sizeof(uint32_t), "Use a 32-bit unsigned-long host ABI");
  byte mac[6] = {};
  assert(mqttWOLParseMAC("aB:cD:ef:01:23:45", mac));
  const byte expected[] = {0xAB, 0xCD, 0xEF, 1, 0x23, 0x45};
  assert(std::memcmp(mac, expected, 6) == 0);
  assert(!mqttWOLParseMAC(nullptr, mac));
  assert(!mqttWOLParseMAC("00:11:22:33:44:55", nullptr));
  assert(!mqttWOLParseMAC("00-11-22-33-44-55", mac));
  assert(!mqttWOLParseMAC("00:11:22:33:44:55x", mac));
  assert(!mqttWOLParseMAC("00:11:22:33:44:gg", mac));

  reset(); mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED);
  fakeNow = 1999; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED); assert(beginCalls == 0);
  fakeNow = 2000; failure_number_mqtt = 2;
  mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED); assert(beginCalls == 0);
  failure_number_mqtt = 3; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED);
  assert(beginCalls == 1 && endCalls == 1 && stopCalls == 1 && packet.size() == 102);
  for (size_t i = 0; i < 6; ++i) assert(packet[i] == 0xFF);
  const byte destination[] = {0, 0x11, 0x22, 0x33, 0x44, 0x55};
  for (size_t i = 0; i < 16; ++i) assert(std::memcmp(packet.data() + 6 + 6*i, destination, 6) == 0);
  fakeNow = 6999; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED); assert(beginCalls == 1);
  fakeNow = 7000; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED); assert(beginCalls == 2);
  client.online = true; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED);
  assert(!mqttWOLTrackingDisconnect && !mqttWOLWakeSent && mqttWOLLastAttempt == 0);
  client.online = false; fakeNow = 8000; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED);
  fakeNow = 9000; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED); assert(beginCalls == 3);

  reset(); mqttWOLConfig.repeatIntervalMs = 0;
  fakeNow = UINT32_MAX - 999; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED);
  fakeNow = 0; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED); assert(beginCalls == 1);
  fakeNow = 1; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED);
  assert(beginCalls == 1); // A timestamp of zero is still a completed attempt.

  reset(); mqttWOLConfig.repeatIntervalMs = 0; endOK = false;
  mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED);
  fakeNow = 2000; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED);
  fakeNow = 2001; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED);
  assert(beginCalls == 1 && !mqttWOLWakeSent);
  mqttWOLConnected(); fakeNow = 3000; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED);
  fakeNow = 4000; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED); assert(beginCalls == 2);

  reset(); mqttWOLConfig.enabled = false; mqttWOLDisconnected(PicoMQTT::CRC_UNDEFINED);
  assert(!mqttWOLTrackingDisconnect && beginCalls == 0);
  reset(); mqttWOLDisconnected(PicoMQTT::CRC_BAD_USERNAME_OR_PASSWORD);
  assert(!mqttWOLTrackingDisconnect); mqttWOLConfig.onAuthError = true;
  assert(mqttWOLShouldTrigger(PicoMQTT::CRC_BAD_USERNAME_OR_PASSWORD));
  assert(mqttWOLShouldTrigger(PicoMQTT::CRC_NOT_AUTHORIZED));
  assert(!mqttWOLShouldTrigger(PicoMQTT::CRC_SERVER_UNAVAILABLE));
  mqttWOLConfig.onBrokerError = true;
  assert(mqttWOLShouldTrigger(PicoMQTT::CRC_SERVER_UNAVAILABLE));
  assert(!mqttWOLShouldTrigger(PicoMQTT::CRC_ACCEPTED));
  reset(); WiFi.value = 0; assert(!mqttWOLSend() && beginCalls == 0);
  reset(); beginOK = false; assert(!mqttWOLSend() && endCalls == 0);
  reset(); endOK = false; assert(!mqttWOLSend() && stopCalls == 1);

#ifdef _WIN32
  SYSTEM_INFO info; GetSystemInfo(&info); const size_t page = info.dwPageSize;
  char *memory = static_cast<char *>(VirtualAlloc(nullptr, 2*page, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  assert(memory); DWORD previous = 0;
  assert(VirtualProtect(memory + page, page, PAGE_NOACCESS, &previous));
  const char valid[] = "00:11:22:33:44:55";
  for (size_t length = 0; length < sizeof(valid); ++length) {
    char *candidate = memory + page - length - 1;
    std::memcpy(candidate, valid, length); candidate[length] = '\0';
    assert(mqttWOLParseMAC(candidate, mac) == (length == 17));
  }
  assert(VirtualFree(memory, 0, MEM_RELEASE));
#endif
  std::puts("PASS: actual WOL parser, short strings, packet bytes, triggers, failure/delay thresholds, repeats, reset and clock rollover");
}
'''.replace("__ENUM__", enum).replace("__CONFIG__", config).replace("__FUNCTIONS__", functions)

with tempfile.TemporaryDirectory(prefix="omg-wol-behavior-") as directory:
    cpp = Path(directory) / "behavior.cpp"
    binary = Path(directory) / ("behavior.exe" if os.name == "nt" else "behavior")
    cpp.write_text(harness, encoding="utf-8")
    # Preserve the firmware's existing signed failure counter comparison.
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare", "-O2",
                    "-I", str(ROOT / "main"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
