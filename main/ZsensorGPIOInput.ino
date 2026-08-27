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

GPIOInputChannelConfig_s gpioInputChannels[GPIO_INPUT_MAX] = {{true, INPUT_GPIO, "GPIOInput", GPIO_INPUT_DEFAULT_MODE, GPIO_INPUT_ACTIVE_LEVEL, GPIOInputDebounceDelay, GPIO_INPUT_CLASS_NONE}};
GPIOInputChannelState_s gpioInputStates[GPIO_INPUT_MAX];

String gpioInputTopic(uint8_t channel) {
  if (channel == 0) return String(subjectGPIOInputtoMQTT);
  return String(subjectGPIOInputtoMQTT) + "/" + String(channel + 1);
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

void setupGPIOInput() {
  for (uint8_t channel = 0; channel < GPIO_INPUT_MAX; channel++) {
    GPIOInputChannelConfig_s& config = gpioInputChannels[channel];
    GPIOInputChannelState_s& state = gpioInputStates[channel];
    if (config.debounceMs < GPIO_INPUT_DEBOUNCE_MIN || config.debounceMs > GPIO_INPUT_DEBOUNCE_MAX) {
      config.mode = GPIO_INPUT_DEFAULT_MODE;
      config.activeLevel = GPIO_INPUT_ACTIVE_LEVEL;
      config.debounceMs = GPIOInputDebounceDelay;
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
    Log.notice(F("[GPIO] input initialized channel=%u name=%s pin=%u mode=%s active=%s debounce_ms=%u class=%s initial=%s mqtt_topic=%s" CR),
               channel + 1, config.name, config.pin, gpioInputModeName(config.mode),
               config.activeLevel == HIGH ? "HIGH" : "LOW", config.debounceMs,
               gpioInputDeviceClassName(config.deviceClass), initialReading == HIGH ? "HIGH" : "LOW",
               gpioInputTopic(channel).c_str());
  }
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
      GPIOdata["origin"] = gpioInputTopic(channel);
      const bool queued = enqueueJsonObject(GPIOdata);
      Log.notice(F("[GPIO] stable change channel=%u name=%s pin=%u previous=%s current=%s active=%T queued=%T mqtt_connected=%T queue=%d uptime_ms=%l" CR),
                 channel + 1, config.name, config.pin,
                 previousStableState == 3 ? "UNKNOWN" : (previousStableState == HIGH ? "HIGH" : "LOW"),
                 reading == HIGH ? "HIGH" : "LOW", reading == config.activeLevel,
                 queued, mqtt && mqtt->connected(), queueLength, millis());

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
    if (!gpioInputChannels[channel].enabled) continue;
    const int currentReading = digitalRead(gpioInputChannels[channel].pin);
    Log.notice(F("[GPIO] MQTT reconnect: scheduling state republish channel=%u pin=%u current=%s previous=%s" CR),
               channel + 1, gpioInputChannels[channel].pin, currentReading == HIGH ? "HIGH" : "LOW",
               gpioInputStates[channel].stableState == 3 ? "UNKNOWN" :
                   (gpioInputStates[channel].stableState == HIGH ? "HIGH" : "LOW"));
    gpioInputStates[channel].stableState = 3;
  }
}
#endif
