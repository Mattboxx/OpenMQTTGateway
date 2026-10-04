#pragma once
#include <Arduino.h>

enum class RuntimePhase : uint8_t { Loop, WiFi, Web, MQTT, Sensors, Queue, OTA, Restart };
enum class OTATransport : uint8_t { WebFile = 1, URL = 2, Network = 3 };
enum class OTAStage : uint8_t { Starting = 1, Receiving, Writing, Validating, Complete, Failed, Interrupted };

#if defined(ESP32) && defined(OMG_RUNTIME_DIAGNOSTICS)
void runtimeDiagnosticsBegin();
void runtimeProgress(RuntimePhase phase);
void runtimePhase(RuntimePhase phase);
void runtimeWiFiEvent(uint8_t reason);
void runtimeRememberWiFiFailure();
void runtimeOTAStart(OTATransport transport, uint32_t expected = 0);
void runtimeOTAStep(OTAStage stage);
void runtimeOTAAccepted(uint32_t total, uint32_t expected = 0);
void runtimeOTAFinish(bool success, int error = 0, int detail = 0);
String runtimeDiagnosticsJSON();
#else
inline void runtimeDiagnosticsBegin() {}
inline void runtimeProgress(RuntimePhase) {}
inline void runtimePhase(RuntimePhase) {}
inline void runtimeWiFiEvent(uint8_t) {}
inline void runtimeRememberWiFiFailure() {}
inline void runtimeOTAStart(OTATransport, uint32_t = 0) {}
inline void runtimeOTAStep(OTAStage) {}
inline void runtimeOTAAccepted(uint32_t, uint32_t = 0) {}
inline void runtimeOTAFinish(bool, int = 0, int = 0) {}
#endif
