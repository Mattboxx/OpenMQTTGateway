#pragma once
#include <Arduino.h>

enum class RuntimePhase : uint8_t { Loop, WiFi, Web, MQTT, Sensors, Queue, OTA, Restart };

#if defined(ESP32) && defined(OMG_RUNTIME_DIAGNOSTICS)
void runtimeDiagnosticsBegin();
void runtimeProgress(RuntimePhase phase);
void runtimePhase(RuntimePhase phase);
void runtimeWiFiEvent(uint8_t reason);
void runtimeRememberWiFiFailure();
String runtimeDiagnosticsJSON();
#else
inline void runtimeDiagnosticsBegin() {}
inline void runtimeProgress(RuntimePhase) {}
inline void runtimePhase(RuntimePhase) {}
inline void runtimeWiFiEvent(uint8_t) {}
inline void runtimeRememberWiFiFailure() {}
#endif
