#include "../../main/CheckedMessageQueue.h"
#include <ArduinoJson.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
  CheckedMessageQueue<2> queue;
  StaticJsonDocument<1024> original;
  original["origin"] = "/SYStoMQTT";
  original["name"] = "garage \"open\"\nUTF-8: \xc3\xa8";
  original["uptime"] = 4294967295UL;
  original["presence"] = true;
  const size_t bytes = measureJson(original);
  assert(queue.tryPush(bytes, [&](char* data, size_t capacity) {
    return serializeJson(original, data, capacity) == bytes;
  }));
  assert(strlen(queue.front()) == bytes);
  DynamicJsonDocument decoded(2048);
  assert(!deserializeJson(decoded, queue.front()));
  // A const queue view must select ArduinoJson's copying overload. Firmware
  // releases the payload before publishing, so no JSON string may alias it.
  const char* name = decoded["name"];
  const uintptr_t address = reinterpret_cast<uintptr_t>(name);
  const uintptr_t payload = reinterpret_cast<uintptr_t>(queue.front());
  assert(address < payload || address > payload + bytes);
  queue.pop();
  assert(strcmp(decoded["origin"], "/SYStoMQTT") == 0);
  assert(strcmp(name, original["name"]) == 0);
  assert(decoded["uptime"].as<unsigned long>() == 4294967295UL);
  assert(decoded["presence"].as<bool>());
  puts("PASS: ArduinoJson measured serialization, escaping, copying decode and payload release");
}
