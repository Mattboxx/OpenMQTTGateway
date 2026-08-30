/*  
  Theengs OpenMQTTGateway - We Unite Sensors in One Open-Source Interface
   Act as a gateway between your 433mhz, infrared IR, BLE, LoRa signal and one interface like an MQTT broker 
   Send and receiving command by MQTT
 
    GPIO Input derived from HC SR-501 reading Addon and https://www.arduino.cc/en/Tutorial/Debounce

    This reads a high (open) or low (closed) through a circuit (switch, float sensor, etc.) connected to ground.

    Copyright: (c)Florian ROBERT
    
    Contributors:
    - 1technophile
    - QuagmireMan
  
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

#ifdef ZsensorGPIOInput
#  if defined(TRIGGER_GPIO)
unsigned long resetTime = 0;
#  endif
struct GPIOInputChannelState_s {
  unsigned long lastDebounceTime;
  int stableState;
  int previousReading;
};

GPIOInputChannelConfig_s gpioInputChannels[GPIO_INPUT_MAX] = {{true, INPUT_GPIO, "GPIOInput", GPIO_INPUT_DEFAULT_MODE, GPIO_INPUT_ACTIVE_LEVEL, GPIOInputDebounceDelay, GPIO_INPUT_RETAIN, GPIO_INPUT_CLASS_NONE}};
GPIOInputChannelState_s gpioInputStates[GPIO_INPUT_MAX];
GPIOOutputChannelConfig_s gpioOutputChannels[GPIO_OUTPUT_MAX] = {};

struct GPIOOutputChannelState_s {
  bool initialized;
  bool logicalOn;
};

GPIOOutputChannelState_s gpioOutputStates[GPIO_OUTPUT_MAX];

String gpioInputTopic(uint8_t channel) {
  if (channel == 0) return String(subjectGPIOInputtoMQTT);
  return String(subjectGPIOInputtoMQTT) + "/" + String(channel + 1);
}

String gpioOutputTopic(uint8_t channel) {
  if (channel == 0) return String(subjectGPIOOutputtoMQTT);
  return String(subjectGPIOOutputtoMQTT) + "/" + String(channel + 1);
}

String gpioOutputCommandTopic(uint8_t channel) {
  return String(subjectMQTTtoGPIOOutput) + "/" + String(channel + 1);
}

const char* gpioInputModeName(uint8_t mode) {
  switch (mode) {
    case GPIO_INPUT_MODE_PULLUP:
      return "PULLUP";
    case GPIO_INPUT_MODE_PULLDOWN:
      return "PULLDOWN";
    case GPIO_INPUT_MODE_INPUT:
    default:
      return "INPUT";
  }
}

const char* gpioInputDeviceClassName(uint8_t deviceClass) {
  static const char* const deviceClasses[GPIO_INPUT_CLASS_COUNT] = {
      "", "opening", "door", "garage_door", "window", "motion",
      "occupancy", "moisture", "smoke", "vibration", "problem"};
  return deviceClass < GPIO_INPUT_CLASS_COUNT ? deviceClasses[deviceClass] : "";
}

const char* gpioOutputModeName(uint8_t mode) {
  return mode == GPIO_OUTPUT_MODE_OPEN_DRAIN ? "OPEN_DRAIN" : "PUSH_PULL";
}

const char* gpioOutputStartupName(uint8_t startupState) {
  switch (startupState) {
    case GPIO_OUTPUT_STARTUP_ON:
      return "ON";
    case GPIO_OUTPUT_STARTUP_RESTORE:
      return "RESTORE";
    case GPIO_OUTPUT_STARTUP_OFF:
    default:
      return "OFF";
  }
}

static uint8_t gpioInputArduinoMode(uint8_t mode) {
  if (mode == GPIO_INPUT_MODE_PULLUP) return INPUT_PULLUP;
#  if defined(ESP32)
  if (mode == GPIO_INPUT_MODE_PULLDOWN) return INPUT_PULLDOWN;
#  elif defined(ESP8266)
  if (mode == GPIO_INPUT_MODE_PULLDOWN) return INPUT_PULLDOWN_16;
#  endif
  return INPUT;
}

const char* gpioInputPinValidationError(int pin, uint8_t mode) {
#  if !defined(GPIO_INPUT_RUNTIME_CONFIG)
  (void)pin;
  (void)mode;
  return nullptr;
#  else
  if (mode >= GPIO_INPUT_MODE_COUNT)
    return "input mode is not supported";
#    if defined(GPIO_INPUT_ALLOWED_MASK)
  if (pin < 0 || pin >= 64 || !(GPIO_INPUT_ALLOWED_MASK & (1ULL << pin)))
    return "GPIO is reserved or unsafe for this hardware preset";
#    endif
#  if defined(ESP32)
  if (pin < 0 || pin > 39 || pin == 20 || pin == 24 || (pin >= 28 && pin <= 31))
    return "GPIO is not available on a classic ESP32";
  if (pin >= 6 && pin <= 11)
    return "GPIO is reserved for ESP32 flash memory";
  if (mode != GPIO_INPUT_MODE_INPUT && pin >= 34)
    return "GPIO 34-39 do not provide internal pull resistors";
#  elif defined(ESP8266)
  if (pin < 0 || pin > 16)
    return "GPIO is not available on ESP8266";
  if (pin >= 6 && pin <= 11)
    return "GPIO is reserved for ESP8266 flash memory";
  if (mode == GPIO_INPUT_MODE_PULLDOWN && pin != 16)
    return "ESP8266 internal pull-down is available only on GPIO 16";
#  elif defined(ARDUINO)
  if (pin < 0)
    return "GPIO must be zero or greater";
#  endif

#  if defined(ZradioCC1101)
#    ifdef RF_MODULE_CS
  if (pin == RF_MODULE_CS) return "GPIO is used by the CC1101 chip-select line";
#    endif
#    ifdef RF_MODULE_GDO0
  if (pin == RF_MODULE_GDO0) return "GPIO is used by the CC1101 GDO0 line";
#    endif
#    ifdef RF_MODULE_GDO2
  if (pin == RF_MODULE_GDO2) return "GPIO is used by the CC1101 GDO2 line";
#    endif
#    if defined(ESP32)
  if (pin == SCK || pin == MISO || pin == MOSI || pin == SS)
    return "GPIO is used by the CC1101 SPI bus";
#    endif
#  endif
#  if defined(ZgatewayRF) || defined(ZgatewayRF2) || defined(ZgatewayPilight) || defined(ZactuatorSomfy)
#    ifdef RF_RECEIVER_GPIO
  if (pin == RF_RECEIVER_GPIO) return "GPIO is used by the RF receiver";
#    endif
#    ifdef RF_EMITTER_GPIO
  if (pin == RF_EMITTER_GPIO) return "GPIO is used by the RF transmitter";
#    endif
#  endif
#  ifdef ACTUATOR_ONOFF_GPIO
  if (pin == ACTUATOR_ONOFF_GPIO) return "GPIO is used by the ON/OFF actuator";
#  endif
#  ifdef LED_PIN
  if (pin == LED_PIN) return "GPIO is used by the status LED";
#  endif
  return nullptr;
#  endif
}

const char* gpioOutputPinValidationError(int pin, uint8_t mode) {
  if (mode >= GPIO_OUTPUT_MODE_COUNT)
    return "output mode is not supported";
#  if defined(GPIO_OUTPUT_ALLOWED_MASK)
  if (pin < 0 || pin >= 64 || !(GPIO_OUTPUT_ALLOWED_MASK & (1ULL << pin)))
    return "GPIO is reserved or unsafe for output on this hardware preset";
#  endif
#  if defined(ESP32)
  if (pin < 0 || pin > 33)
    return "GPIO is not output-capable on a classic ESP32";
#  elif defined(ESP8266)
  if (pin < 0 || pin > 16)
    return "GPIO is not available on ESP8266";
#  endif
  // Reuse the board/module reservation checks used by inputs. OUTPUT pins are
  // a strict subset of the safe INPUT list for this preset.
  const char* commonError = gpioInputPinValidationError(pin, GPIO_INPUT_MODE_INPUT);
  if (commonError) return commonError;
  return nullptr;
}

static uint8_t gpioOutputArduinoMode(uint8_t mode) {
#  if defined(OUTPUT_OPEN_DRAIN)
  if (mode == GPIO_OUTPUT_MODE_OPEN_DRAIN) return OUTPUT_OPEN_DRAIN;
#  endif
  return OUTPUT;
}

bool gpioOutputIsOn(uint8_t channel) {
  return channel < GPIO_OUTPUT_MAX && gpioOutputStates[channel].initialized && gpioOutputStates[channel].logicalOn;
}

static void gpioOutputWrite(uint8_t channel, bool logicalOn, bool persist) {
  GPIOOutputChannelConfig_s& config = gpioOutputChannels[channel];
  const uint8_t electricalLevel = logicalOn ? config.activeLevel : !config.activeLevel;
  digitalWrite(config.pin, electricalLevel);
  gpioOutputStates[channel].logicalOn = logicalOn;
  gpioOutputStates[channel].initialized = true;

#  if defined(ESP32)
  if (persist && config.startupState == GPIO_OUTPUT_STARTUP_RESTORE) {
    char key[12];
    snprintf(key, sizeof(key), "gpioOut%u", channel + 1);
    preferences.begin(Gateway_Short_Name, false);
    const size_t saved = preferences.putBool(key, logicalOn);
    preferences.end();
    Log.trace(F("[GPIO] output state persisted channel=%u state=%s result=%u" CR),
              channel + 1, logicalOn ? "ON" : "OFF", saved);
  }
#  else
  (void)persist;
#  endif
}

static bool publishGPIOOutputState(uint8_t channel, const char* reason) {
  if (channel >= GPIO_OUTPUT_MAX || !gpioOutputChannels[channel].enabled || !gpioOutputStates[channel].initialized)
    return false;
  GPIOOutputChannelConfig_s& config = gpioOutputChannels[channel];
  const bool logicalOn = gpioOutputStates[channel].logicalOn;
  StaticJsonDocument<JSON_MSG_BUFFER> outputBuffer;
  JsonObject output = outputBuffer.to<JsonObject>();
  output["state"] = logicalOn ? "ON" : "OFF";
  output["active"] = logicalOn;
  output["gpio"] = digitalRead(config.pin) == HIGH ? "HIGH" : "LOW";
  output["pin"] = config.pin;
  output["name"] = config.name;
  output["mode"] = gpioOutputModeName(config.mode);
  output["retain"] = config.retainState;
  output["origin"] = gpioOutputTopic(channel);
  const bool queued = enqueueJsonObject(output);
  Log.notice(F("[GPIO] output state channel=%u name=%s pin=%u state=%s electrical=%s reason=%s retain=%T queued=%T mqtt_connected=%T" CR),
             channel + 1, config.name, config.pin, logicalOn ? "ON" : "OFF",
             digitalRead(config.pin) == HIGH ? "HIGH" : "LOW", reason,
             config.retainState, queued, mqtt && mqtt->connected());
  return queued;
}

void setupGPIOInput() {
  for (uint8_t channel = 0; channel < GPIO_INPUT_MAX; channel++) {
    GPIOInputChannelConfig_s& config = gpioInputChannels[channel];
    GPIOInputChannelState_s& state = gpioInputStates[channel];
    if (config.debounceMs < GPIO_INPUT_DEBOUNCE_MIN || config.debounceMs > GPIO_INPUT_DEBOUNCE_MAX) {
      config.mode = GPIO_INPUT_DEFAULT_MODE;
      config.activeLevel = GPIO_INPUT_ACTIVE_LEVEL;
      config.debounceMs = GPIOInputDebounceDelay;
      config.retainState = GPIO_INPUT_RETAIN;
      config.deviceClass = GPIO_INPUT_CLASS_NONE;
    }
    if (config.mode >= GPIO_INPUT_MODE_COUNT) config.mode = GPIO_INPUT_DEFAULT_MODE;
    if (config.activeLevel != LOW && config.activeLevel != HIGH) config.activeLevel = GPIO_INPUT_ACTIVE_LEVEL;
    if (config.deviceClass >= GPIO_INPUT_CLASS_COUNT) config.deviceClass = GPIO_INPUT_CLASS_NONE;
    state.lastDebounceTime = millis();
    state.stableState = 3;
    state.previousReading = 3;
    if (!config.enabled) continue;

    const char* validationError = gpioInputPinValidationError(config.pin, config.mode);
    if (validationError) {
      Log.error(F("[GPIO] disabling channel=%u pin=%u reason=%s" CR), channel + 1, config.pin, validationError);
      config.enabled = false;
      continue;
    }
    for (uint8_t previous = 0; previous < channel; previous++) {
      if (gpioInputChannels[previous].enabled && gpioInputChannels[previous].pin == config.pin) {
        Log.error(F("[GPIO] disabling duplicate channel=%u pin=%u already_used_by=%u" CR),
                  channel + 1, config.pin, previous + 1);
        config.enabled = false;
        break;
      }
    }
    if (!config.enabled) continue;
    if (!config.name[0]) snprintf(config.name, sizeof(config.name), "GPIO Input %u", channel + 1);

    pinMode(config.pin, gpioInputArduinoMode(config.mode));
    const int initialReading = digitalRead(config.pin);
    state.previousReading = initialReading;
    Log.notice(F("[GPIO] input initialized channel=%u name=%s pin=%u mode=%s active=%s debounce_ms=%u retain=%T class=%s initial=%s mqtt_topic=%s" CR),
               channel + 1, config.name, config.pin, gpioInputModeName(config.mode),
               config.activeLevel == HIGH ? "HIGH" : "LOW", config.debounceMs,
               config.retainState,
               gpioInputDeviceClassName(config.deviceClass), initialReading == HIGH ? "HIGH" : "LOW",
               gpioInputTopic(channel).c_str());
  }
}

void setupGPIOOutput() {
  for (uint8_t channel = 0; channel < GPIO_OUTPUT_MAX; channel++) {
    GPIOOutputChannelConfig_s& config = gpioOutputChannels[channel];
    gpioOutputStates[channel] = {false, false};
    const bool neverConfigured = !config.name[0];
    if (neverConfigured) {
      snprintf(config.name, sizeof(config.name), "GPIO Output %u", channel + 1);
      config.mode = GPIO_OUTPUT_MODE_PUSH_PULL;
      config.activeLevel = HIGH;
      config.startupState = GPIO_OUTPUT_STARTUP_OFF;
      config.retainState = true;
    }
    if (config.mode >= GPIO_OUTPUT_MODE_COUNT) config.mode = GPIO_OUTPUT_MODE_PUSH_PULL;
    if (config.activeLevel != LOW && config.activeLevel != HIGH) config.activeLevel = HIGH;
    if (config.startupState >= GPIO_OUTPUT_STARTUP_COUNT) config.startupState = GPIO_OUTPUT_STARTUP_OFF;

    // Give never-configured disabled slots a useful, safe pin in the WebUI.
    if (!config.enabled && gpioOutputPinValidationError(config.pin, config.mode)) {
      const uint8_t defaults[] = {16, 17};
      if (channel < sizeof(defaults)) config.pin = defaults[channel];
    }
    if (!config.enabled) continue;

    const char* validationError = gpioOutputPinValidationError(config.pin, config.mode);
    if (validationError) {
      Log.error(F("[GPIO] disabling output channel=%u pin=%u reason=%s" CR), channel + 1, config.pin, validationError);
      config.enabled = false;
      continue;
    }
    bool conflict = false;
    for (uint8_t input = 0; input < GPIO_INPUT_MAX; input++) {
      if (gpioInputChannels[input].enabled && gpioInputChannels[input].pin == config.pin) {
        Log.error(F("[GPIO] disabling output channel=%u pin=%u conflict=input-%u" CR), channel + 1, config.pin, input + 1);
        conflict = true;
        break;
      }
    }
    for (uint8_t previous = 0; !conflict && previous < channel; previous++) {
      if (gpioOutputChannels[previous].enabled && gpioOutputChannels[previous].pin == config.pin) {
        Log.error(F("[GPIO] disabling output channel=%u pin=%u conflict=output-%u" CR), channel + 1, config.pin, previous + 1);
        conflict = true;
      }
    }
    if (conflict) {
      config.enabled = false;
      continue;
    }

    bool initialOn = config.startupState == GPIO_OUTPUT_STARTUP_ON;
#  if defined(ESP32)
    if (config.startupState == GPIO_OUTPUT_STARTUP_RESTORE) {
      char key[12];
      snprintf(key, sizeof(key), "gpioOut%u", channel + 1);
      preferences.begin(Gateway_Short_Name, true);
      initialOn = preferences.getBool(key, false);
      preferences.end();
    }
#  endif
    // Set the output latch before changing the pin direction to avoid a brief
    // active pulse during boot or configuration changes.
    digitalWrite(config.pin, initialOn ? config.activeLevel : !config.activeLevel);
    pinMode(config.pin, gpioOutputArduinoMode(config.mode));
    gpioOutputWrite(channel, initialOn, false);
    Log.notice(F("[GPIO] output initialized channel=%u name=%s pin=%u mode=%s active=%s startup=%s state=%s electrical=%s retain=%T mqtt_state=%s mqtt_command=%s" CR),
               channel + 1, config.name, config.pin, gpioOutputModeName(config.mode),
               config.activeLevel == HIGH ? "HIGH" : "LOW", gpioOutputStartupName(config.startupState),
               initialOn ? "ON" : "OFF", digitalRead(config.pin) == HIGH ? "HIGH" : "LOW",
               config.retainState, gpioOutputTopic(channel).c_str(), gpioOutputCommandTopic(channel).c_str());
  }
}

void XtoGPIOOutput(const char* topicOri, JsonObject& data) {
  int selectedChannel = -1;
  for (uint8_t channel = 0; channel < GPIO_OUTPUT_MAX; channel++) {
    String commandSuffix = gpioOutputCommandTopic(channel);
    String commandFull = String(mqtt_topic) + String(gateway_name) + commandSuffix;
    if (strcmp(topicOri, commandSuffix.c_str()) == 0 || strcmp(topicOri, commandFull.c_str()) == 0) {
      selectedChannel = channel;
      break;
    }
  }
  if (selectedChannel < 0) {
    String baseFull = String(mqtt_topic) + String(gateway_name) + String(subjectMQTTtoGPIOOutput);
    if (strcmp(topicOri, subjectMQTTtoGPIOOutput) != 0 && strcmp(topicOri, baseFull.c_str()) != 0) return;
    const int requestedChannel = data["channel"] | 0;
    if (requestedChannel < 1 || requestedChannel > GPIO_OUTPUT_MAX) {
      Log.warning(F("[GPIO] output command rejected reason=invalid-channel channel=%d" CR), requestedChannel);
      return;
    }
    selectedChannel = requestedChannel - 1;
  }

  GPIOOutputChannelConfig_s& config = gpioOutputChannels[selectedChannel];
  if (!config.enabled || !gpioOutputStates[selectedChannel].initialized) {
    Log.warning(F("[GPIO] output command rejected channel=%u reason=disabled" CR), selectedChannel + 1);
    return;
  }

  bool requestedOn = false;
  bool valid = false;
  if (data["state"].is<bool>()) {
    requestedOn = data["state"].as<bool>();
    valid = true;
  } else if (data["state"].is<const char*>()) {
    String state = data["state"].as<const char*>();
    state.toUpperCase();
    if (state == "ON" || state == "1" || state == "TRUE") {
      requestedOn = true;
      valid = true;
    } else if (state == "OFF" || state == "0" || state == "FALSE") {
      requestedOn = false;
      valid = true;
    } else if (state == "TOGGLE") {
      requestedOn = !gpioOutputStates[selectedChannel].logicalOn;
      valid = true;
    }
  } else if (data["state"].is<int>()) {
    requestedOn = data["state"].as<int>() != 0;
    valid = true;
  }
  if (!valid) {
    Log.warning(F("[GPIO] output command rejected channel=%u reason=invalid-state" CR), selectedChannel + 1);
    return;
  }

  const bool previous = gpioOutputStates[selectedChannel].logicalOn;
  gpioOutputWrite(selectedChannel, requestedOn, previous != requestedOn);
  Log.notice(F("[GPIO] output command channel=%u name=%s pin=%u previous=%s requested=%s electrical=%s changed=%T" CR),
             selectedChannel + 1, config.name, config.pin, previous ? "ON" : "OFF", requestedOn ? "ON" : "OFF",
             digitalRead(config.pin) == HIGH ? "HIGH" : "LOW", previous != requestedOn);
  publishGPIOOutputState(selectedChannel, "command");
}

void MeasureGPIOInput() {
  for (uint8_t channel = 0; channel < GPIO_INPUT_MAX; channel++) {
    GPIOInputChannelConfig_s& config = gpioInputChannels[channel];
    GPIOInputChannelState_s& state = gpioInputStates[channel];
    if (!config.enabled) continue;
    int reading = digitalRead(config.pin);

    if (reading != state.previousReading) {
      state.lastDebounceTime = millis();
      Log.trace(F("[GPIO] raw edge channel=%u pin=%u previous=%d current=%d debounce_started_ms=%l" CR),
                channel + 1, config.pin, state.previousReading, reading, state.lastDebounceTime);
    }

    if ((millis() - state.lastDebounceTime) > config.debounceMs) {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:
    yield();
#  if defined(TRIGGER_GPIO) && !defined(ESPWifiManualSetup)
      if (channel == 0 && config.pin == TRIGGER_GPIO) {
    if (reading == LOW) {
      if (resetTime == 0) {
        resetTime = millis();
      } else if ((millis() - resetTime) > 3000) {
        Log.warning(F("[GPIO] reset contact held pin=%u duration_ms=%l" CR), config.pin, millis() - resetTime);
        gatewayState = GatewayState::WAITING_ONBOARDING;
// Switching off the relay during reset or failsafe operations
#    ifdef ZactuatorONOFF
        uint8_t level = digitalRead(ACTUATOR_ONOFF_GPIO);
        if (level == ACTUATOR_ON) {
          ActuatorTrigger();
        }
#    endif
        Log.warning(F("[GPIO] erasing configuration and restarting after long press" CR));
        eraseConfig();
      }
    } else {
      resetTime = 0;
    }
      }
#  endif
    // if the Input state has changed:
      if (reading != state.stableState) {
      const int previousStableState = state.stableState;
      StaticJsonDocument<JSON_MSG_BUFFER> GPIOdataBuffer;
      JsonObject GPIOdata = GPIOdataBuffer.to<JsonObject>();
      if (reading == HIGH) {
        GPIOdata["gpio"] = "HIGH";
      }
      if (reading == LOW) {
        GPIOdata["gpio"] = "LOW";
      }
      GPIOdata["pin"] = config.pin;
      GPIOdata["name"] = config.name;
      GPIOdata["active"] = reading == config.activeLevel;
      GPIOdata["mode"] = gpioInputModeName(config.mode);
      GPIOdata["retain"] = config.retainState;
      GPIOdata["origin"] = gpioInputTopic(channel);
      const bool queued = enqueueJsonObject(GPIOdata);
      Log.notice(F("[GPIO] stable change channel=%u name=%s pin=%u previous=%s current=%s active=%T retain=%T queued=%T mqtt_connected=%T queue=%d uptime_ms=%l" CR),
                 channel + 1, config.name, config.pin,
                 previousStableState == 3 ? "UNKNOWN" : (previousStableState == HIGH ? "HIGH" : "LOW"),
                 reading == HIGH ? "HIGH" : "LOW", reading == config.activeLevel,
                 config.retainState, queued, mqtt && mqtt->connected(), queueLength, millis());

#  if defined(ZactuatorONOFF) && defined(ACTUATOR_TRIGGER)
      //Trigger the actuator if we are not at startup
      if (channel == 0 && state.stableState != 3) {
#    if defined(ACTUATOR_BUTTON_TRIGGER_LEVEL)
        if (state.stableState == ACTUATOR_BUTTON_TRIGGER_LEVEL)
          ActuatorTrigger(); // Button press trigger
#    else
        ActuatorTrigger(); // Switch trigger
#    endif
      }
#  endif
        state.stableState = reading;
      }
    }

    state.previousReading = reading;
  }
}

void forcePublishGPIOState() {
  for (uint8_t channel = 0; channel < GPIO_INPUT_MAX; channel++) {
    if (!gpioInputChannels[channel].enabled || !gpioInputChannels[channel].retainState) {
      String fullTopic = String(mqtt_topic) + String(gateway_name) + gpioInputTopic(channel);
      const bool cleared = pubMQTT(fullTopic.c_str(), "", true);
      Log.notice(F("[GPIO] retained state cleanup channel=%u enabled=%T retain=%T topic=%s cleared=%T" CR),
                 channel + 1, gpioInputChannels[channel].enabled,
                 gpioInputChannels[channel].retainState, fullTopic.c_str(), cleared);
    }
    if (!gpioInputChannels[channel].enabled) continue;
    const int currentReading = digitalRead(gpioInputChannels[channel].pin);
    Log.notice(F("[GPIO] MQTT reconnect: scheduling state republish channel=%u pin=%u current=%s previous=%s retain=%T" CR),
               channel + 1, gpioInputChannels[channel].pin, currentReading == HIGH ? "HIGH" : "LOW",
               gpioInputStates[channel].stableState == 3 ? "UNKNOWN" :
                   (gpioInputStates[channel].stableState == HIGH ? "HIGH" : "LOW"),
               gpioInputChannels[channel].retainState);
    gpioInputStates[channel].stableState = 3;
  }
  // v1.8.1-wol-gpio.38 exposed four input slots. Clear the two legacy retained
  // state topics after this preset moves to two inputs.
  for (uint8_t channel = GPIO_INPUT_MAX; channel < 4; channel++) {
    String legacyTopic = String(mqtt_topic) + String(gateway_name) + String(subjectGPIOInputtoMQTT) + "/" + String(channel + 1);
    pubMQTT(legacyTopic.c_str(), "", true);
  }
  for (uint8_t channel = 0; channel < GPIO_OUTPUT_MAX; channel++) {
    String fullStateTopic = String(mqtt_topic) + String(gateway_name) + gpioOutputTopic(channel);
    String fullCommandTopic = String(mqtt_topic) + String(gateway_name) + gpioOutputCommandTopic(channel);
    if (!gpioOutputChannels[channel].enabled) {
      pubMQTT(fullStateTopic.c_str(), "", true);
      pubMQTT(fullCommandTopic.c_str(), "", true);
      continue;
    }
    publishGPIOOutputState(channel, "mqtt-reconnect");
  }
}
#endif
