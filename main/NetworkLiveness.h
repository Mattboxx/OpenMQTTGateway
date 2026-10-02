#pragma once

#include <Arduino.h>

struct NetworkLivenessSnapshot {
  bool started;
  uint32_t target;
  uint32_t replies;
  uint32_t timeouts;
  uint32_t recoveries;
  uint32_t lastResultAgeMs;
  uint8_t consecutiveFailures;
};

#if defined(ESP32) && defined(WIFI_GATEWAY_LIVENESS)
void networkLivenessBegin();
void networkLivenessLoop();
NetworkLivenessSnapshot networkLivenessSnapshot();
#else
inline void networkLivenessBegin() {}
inline void networkLivenessLoop() {}
inline NetworkLivenessSnapshot networkLivenessSnapshot() { return {}; }
#endif
