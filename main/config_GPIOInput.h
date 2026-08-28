/*  
  Theengs OpenMQTTGateway - We Unite Sensors in One Open-Source Interface

   Act as a gateway between your 433mhz, infrared IR, BLE, LoRa signal and one interface like an MQTT broker 
   Send and receiving command by MQTT
 
   This files enables to set your parameter for the GPIOInput sensor
  
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
#ifndef config_GPIOInput_h
#define config_GPIOInput_h

extern void setupGPIOInput();
extern void GPIOInputtoX();
extern void MeasureGPIOInput();
extern void forcePublishGPIOState();
#ifndef GPIO_INPUT_MAX
#  define GPIO_INPUT_MAX 1
#endif
#ifndef GPIO_INPUT_NAME_SIZE
#  define GPIO_INPUT_NAME_SIZE 32
#endif

enum GPIOInputMode : uint8_t {
  GPIO_INPUT_MODE_INPUT = 0,
  GPIO_INPUT_MODE_PULLUP,
  GPIO_INPUT_MODE_PULLDOWN,
  GPIO_INPUT_MODE_COUNT
};

enum GPIOInputDeviceClass : uint8_t {
  GPIO_INPUT_CLASS_NONE = 0,
  GPIO_INPUT_CLASS_OPENING,
  GPIO_INPUT_CLASS_DOOR,
  GPIO_INPUT_CLASS_GARAGE_DOOR,
  GPIO_INPUT_CLASS_WINDOW,
  GPIO_INPUT_CLASS_MOTION,
  GPIO_INPUT_CLASS_OCCUPANCY,
  GPIO_INPUT_CLASS_MOISTURE,
  GPIO_INPUT_CLASS_SMOKE,
  GPIO_INPUT_CLASS_VIBRATION,
  GPIO_INPUT_CLASS_PROBLEM,
  GPIO_INPUT_CLASS_COUNT
};

#ifndef GPIO_INPUT_DEBOUNCE_MIN
#  define GPIO_INPUT_DEBOUNCE_MIN 10
#endif
#ifndef GPIO_INPUT_DEBOUNCE_MAX
#  define GPIO_INPUT_DEBOUNCE_MAX 5000
#endif

#ifndef GPIO_INPUT_RETAIN
#  define GPIO_INPUT_RETAIN true
#endif

struct GPIOInputChannelConfig_s {
  bool enabled;
  uint8_t pin;
  char name[GPIO_INPUT_NAME_SIZE];
  uint8_t mode;
  uint8_t activeLevel;
  uint16_t debounceMs;
  bool retainState;
  uint8_t deviceClass;
};

extern GPIOInputChannelConfig_s gpioInputChannels[GPIO_INPUT_MAX];
extern const char* gpioInputPinValidationError(int pin, uint8_t mode);
extern const char* gpioInputModeName(uint8_t mode);
extern const char* gpioInputDeviceClassName(uint8_t deviceClass);
extern String gpioInputTopic(uint8_t channel);
/*----------------------------USER PARAMETERS-----------------------------*/
/*-------------DEFINE YOUR MQTT PARAMETERS BELOW----------------*/
#define subjectGPIOInputtoMQTT "/GPIOInputtoMQTT"
#define GPIOInputDebounceDelay 60 //debounce time, increase if there are issues

/*-------------------PIN DEFINITIONS----------------------*/
#ifndef INPUT_GPIO
#  if defined(ESP8266) || defined(ESP32)
#    define INPUT_GPIO 13
#  else
#    define INPUT_GPIO 7
#  endif
#endif

#ifndef GPIO_INPUT_TYPE
#  define GPIO_INPUT_TYPE INPUT_PULLUP
#endif

#ifndef GPIO_INPUT_DEFAULT_MODE
#  if GPIO_INPUT_TYPE == INPUT_PULLUP
#    define GPIO_INPUT_DEFAULT_MODE GPIO_INPUT_MODE_PULLUP
#  elif defined(INPUT_PULLDOWN) && GPIO_INPUT_TYPE == INPUT_PULLDOWN
#    define GPIO_INPUT_DEFAULT_MODE GPIO_INPUT_MODE_PULLDOWN
#  else
#    define GPIO_INPUT_DEFAULT_MODE GPIO_INPUT_MODE_INPUT
#  endif
#endif

#ifndef GPIO_INPUT_ACTIVE_LEVEL
#  define GPIO_INPUT_ACTIVE_LEVEL HIGH
#endif

#define INPUT_GPIO_ON_VALUE  "HIGH"
#define INPUT_GPIO_OFF_VALUE "LOW"

#endif
