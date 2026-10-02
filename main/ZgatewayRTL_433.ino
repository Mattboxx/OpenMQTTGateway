/*
  OpenMQTTGateway  - ESP8266 or Arduino program for home automation

   Act as a gateway between your 433mhz, infrared IR, BLE, LoRa signal and one interface like an MQTT broker
   Send and receiving command by MQTT

  This gateway enables to:
 - receive MQTT data from a topic and send RF 433Mhz signal corresponding to the received MQTT data
 - publish MQTT data to a different topic related to received 433Mhz signal
 - leverage the rtl_433 device decoders on a ESP32 device

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
#ifdef ZgatewayRTL_433

#  include <ArduinoJson.h>
#  include <config_RF.h>
#  include <rtl_433_ESP.h>

#  include "ArduinoLog.h"
#  include "User_config.h"
#  ifdef ZmqttDiscovery
#    include "config_mqttDiscovery.h"
#    include "FixedDiscoveryCache.h"
#  endif

char messageBuffer[JSON_MSG_BUFFER];

#  ifdef ZmqttDiscovery
SemaphoreHandle_t semaphorecreateOrUpdateDeviceRTL_433;
StaticSemaphore_t semaphorecreateOrUpdateDeviceRTL_433Buffer;
FixedDiscoveryCache<RTL_433device, RTL433_DISCOVERY_CACHE_SIZE> RTL_433devices;
int newRTL_433Devices = 0;
uint32_t droppedRTL_433Devices = 0;
uint32_t evictedRTL_433Devices = 0;
uint32_t lastRTL_433DiscoveryWarning = 0;
bool hasRTL_433DiscoveryWarning = false;

void logRTL_433DiscoveryDrop(const char* reason, const char* id) {
  const uint32_t now = millis();
  if (hasRTL_433DiscoveryWarning && now - lastRTL_433DiscoveryWarning < 60000UL) return;
  hasRTL_433DiscoveryWarning = true;
  lastRTL_433DiscoveryWarning = now;
  Log.warning(F("[rtl_433] discovery ignored reason=%s id=%s total=%u" CR),
              reason, id ? id : "", droppedRTL_433Devices);
}

void createOrUpdateDeviceRTL_433(const char* id, const char* model, const char* type, uint8_t flags) {
  if (!semaphorecreateOrUpdateDeviceRTL_433) {
    ++droppedRTL_433Devices;
    return;
  }
  if (!id || !id[0] || !model || !type || strlen(id) >= uniqueIdSize ||
      strlen(model) >= modelNameSize || strlen(type) >= typeSize) {
    ++droppedRTL_433Devices;
    logRTL_433DiscoveryDrop("invalid_or_too_long", id);
    return;
  }
  if (xSemaphoreTake(semaphorecreateOrUpdateDeviceRTL_433, pdMS_TO_TICKS(QueueSemaphoreTimeOutTask)) == pdFALSE) {
    Log.error(F("[rtl_433] semaphorecreateOrUpdateDeviceRTL_433 Semaphore NOT taken" CR));
    ++droppedRTL_433Devices;
    return;
  }

  RTL_433device updated = {};
  strlcpy(updated.uniqueId, id, sizeof(updated.uniqueId));
  strlcpy(updated.modelName, model, sizeof(updated.modelName));
  strlcpy(updated.type, type, sizeof(updated.type));
  updated.isDisc = flags & device_flags_isDisc;
  const auto result = RTL_433devices.upsert(updated, millis());
  if (result == decltype(RTL_433devices)::Result::Inserted ||
      result == decltype(RTL_433devices)::Result::Evicted) {
    ++newRTL_433Devices;
    if (result == decltype(RTL_433devices)::Result::Evicted)
      ++evictedRTL_433Devices;
    DISCOVERY_TRACE_LOG(F("add %s" CR), id);
  } else if (result == decltype(RTL_433devices)::Result::Full ||
             result == decltype(RTL_433devices)::Result::Invalid) {
    ++droppedRTL_433Devices;
    logRTL_433DiscoveryDrop("cache_full", id);
  } else {
    DISCOVERY_TRACE_LOG(F("update %s" CR), id);
  }

  xSemaphoreGive(semaphorecreateOrUpdateDeviceRTL_433);
}

unsigned int getRTLDiscoveryCacheCount() { return RTL_433devices.size(); }
unsigned int getRTLDiscoveryCacheDropped() { return droppedRTL_433Devices; }
unsigned int getRTLDiscoveryCacheEvicted() { return evictedRTL_433Devices; }

// This function always should be called from the main core as it generates direct mqtt messages
// When overrideDiscovery=true, we publish discovery messages of known RTL_433devices (even if no new)
void launchRTL_433Discovery(bool overrideDiscovery) {
  if (!semaphorecreateOrUpdateDeviceRTL_433) return;
  if (xSemaphoreTake(semaphorecreateOrUpdateDeviceRTL_433, pdMS_TO_TICKS(QueueSemaphoreTimeOutLoop)) == pdFALSE) {
    Log.error(F("[rtl_433] semaphorecreateOrUpdateDeviceRTL_433 Semaphore NOT taken" CR));
    return;
  }
  const bool shouldLaunch = overrideDiscovery || newRTL_433Devices > 0;
  newRTL_433Devices = 0;
  xSemaphoreGive(semaphorecreateOrUpdateDeviceRTL_433);
  if (!shouldLaunch) return;

  // Copy one fixed-size record at a time. The previous vector copy allocated
  // from the heap whenever discovery ran and could throw std::bad_alloc.
  for (size_t slot = 0; slot < RTL_433devices.capacity(); ++slot) {
    RTL_433device localDevice = {};
    if (xSemaphoreTake(semaphorecreateOrUpdateDeviceRTL_433, pdMS_TO_TICKS(QueueSemaphoreTimeOutLoop)) == pdFALSE) {
      Log.error(F("[rtl_433] discovery snapshot semaphore timeout" CR));
      return;
    }
    const bool slotUsed = RTL_433devices.snapshot(slot, localDevice);
    xSemaphoreGive(semaphorecreateOrUpdateDeviceRTL_433);
    if (!slotUsed) continue;
    RTL_433device* pdevice = &localDevice;
    DISCOVERY_TRACE_LOG(F("Device id %s" CR), pdevice->uniqueId);
    // Do not launch discovery for the RTL_433devices already discovered (unless we have overrideDiscovery) or that are not unique by their MAC Address (Ibeacon, GAEN and Microsoft Cdp)
    if (overrideDiscovery || !isDiscovered(pdevice)) {
      size_t numRows = sizeof(parameters) / sizeof(parameters[0]);
      for (int i = 0; i < numRows; i++) {
        const char* parameter = parameters[i][0];
        const size_t idLength = strlen(pdevice->uniqueId);
        const size_t parameterLength = strlen(parameter);
        // Require "-key" as an exact suffix. The former subtraction could
        // underflow and read before uniqueId when an RF field was malformed.
        if (idLength > parameterLength &&
            pdevice->uniqueId[idLength - parameterLength - 1] == '-' &&
            strcmp(pdevice->uniqueId + idLength - parameterLength, parameter) == 0) {
          // Remove the key from the unique id to extract the device id
          String idWoKey = pdevice->uniqueId;
          idWoKey.remove(idWoKey.length() - (strlen(parameters[i][0]) + 1));
          DISCOVERY_TRACE_LOG(F("idWoKey %s" CR), idWoKey.c_str());
          String value_template = "";
          if (SYSConfig.ohdiscovery) {
            value_template = "{{ value_json." + String(parameters[i][0]) + " }}";
          } else {
            value_template = "{{ value_json." + String(parameters[i][0]) + " | is_defined }}";
          }
          String topic = subjectRTL_433toMQTT;
#    if valueAsATopic
          // Remove the key from the unique id to extract the device id
          String idWoKeyAndModel = idWoKey;
          if (strcmp(pdevice->type, "null")) {
            idWoKeyAndModel.remove(0, (strlen(pdevice->modelName) + strlen(pdevice->type) + 1)); // type is present
            topic = topic + "/" + String(pdevice->type) + "/" + String(pdevice->modelName);
          } else {
            idWoKeyAndModel.remove(0, (strlen(pdevice->modelName)));
            topic = topic + "/" + String(pdevice->modelName);
          }
          DISCOVERY_TRACE_LOG(F("idWoKeyAndModel %s" CR), idWoKeyAndModel.c_str());
          idWoKeyAndModel.replace("-", "/");
          topic = topic + idWoKeyAndModel;
#    endif
          if (strcmp(parameters[i][0], "battery_ok") == 0) {
            if (strcmp(pdevice->modelName, "Govee-Water") == 0 || strcmp(pdevice->modelName, "Govee-Contact") == 0 || strcmp(pdevice->modelName, "Archos-TBH") == 0 || strcmp(pdevice->modelName, "FineOffset-WH31L") == 0 || strcmp(pdevice->modelName, "Fineoffset-WH45") == 0 || strcmp(pdevice->modelName, "Fineoffset-WN34") == 0 || strcmp(pdevice->modelName, "Fineoffset-WS80") == 0 || strcmp(pdevice->modelName, "Fineoffset-WH0290") == 0 || strcmp(pdevice->modelName, "Fineoffset-WH51") == 0 || strcmp(pdevice->modelName, "Kedsum-TH") == 0 || strcmp(pdevice->modelName, "AVE") == 0 || strcmp(pdevice->modelName, "TPMS") == 0) {
              if (SYSConfig.ohdiscovery) {
                value_template = "{{ (value_json." + String(parameters[i][0]) + " * 100) | round(0)}}";
              } else {
                value_template = "{{ (float(value_json." + String(parameters[i][0]) + ") * 100) | round(0) | is_defined }}";
              }
              createDiscovery("sensor", //set Type
                              (char*)topic.c_str(), parameters[i][1], pdevice->uniqueId, //set state_topic,name,uniqueId
                              "", parameters[i][3], (char*)value_template.c_str(), //set availability_topic,device_class,value_template,
                              "", "", "%", //set,payload_on,payload_off,unit_of_meas,
                              0, //set  off_delay
                              "", "", false, "", //set,payload_available,payload_not available   ,is a gateway entity, command topic
                              (char*)idWoKey.c_str(), "", pdevice->modelName, (char*)idWoKey.c_str(), false, // device name, device manufacturer, device model, device ID, retain
                              stateClassMeasurement //State Class
              );
            } else {
              createDiscovery("binary_sensor", //set Type
                              (char*)topic.c_str(), parameters[i][1], pdevice->uniqueId, //set state_topic,name,uniqueId
                              "", parameters[i][3], (char*)value_template.c_str(), //set availability_topic,device_class,value_template,
                              "0", "1", "", //set,payload_on,payload_off,unit_of_meas,
                              0, //set  off_delay
                              "", "", false, "", //set,payload_available,payload_not available   ,is a gateway entity, command topic
                              (char*)idWoKey.c_str(), "", pdevice->modelName, (char*)idWoKey.c_str(), false, // device name, device manufacturer, device model, device ID, retain
                              "" //State Class
              );
            }
          } else if (strcmp(parameters[i][0], "tamper") == 0 || strcmp(parameters[i][0], "alarm") == 0 || strcmp(parameters[i][0], "motion") == 0) {
            createDiscovery("binary_sensor", //set Type
                            (char*)topic.c_str(), parameters[i][1], pdevice->uniqueId, //set state_topic,name,uniqueId
                            "", parameters[i][3], (char*)value_template.c_str(), //set availability_topic,device_class,value_template,
                            "1", "0", parameters[i][2], //set,payload_on,payload_off,unit_of_meas,
                            0, //set  off_delay
                            "", "", false, "", //set,payload_available,payload_not available   ,is a gateway entity, command topic
                            (char*)idWoKey.c_str(), "", pdevice->modelName, (char*)idWoKey.c_str(), false, // device name, device manufacturer, device model, device ID, retain
                            "" //State Class
            );
          } else if (strcmp(parameters[i][0], "state") == 0 && (strcmp(pdevice->modelName, "Nexa-Security") == 0 || strcmp(pdevice->modelName, "Brennenstuhl-RCS2044") == 0 || strcmp(pdevice->modelName, "Proove-Security") == 0 || strcmp(pdevice->modelName, "Waveman-Switch") == 0)) {
            createDiscovery("binary_sensor", //set Type
                            (char*)topic.c_str(), parameters[i][1], pdevice->uniqueId, //set state_topic,name,uniqueId
                            "", parameters[i][3], (char*)value_template.c_str(), //set availability_topic,device_class,value_template,
                            "ON", "OFF", parameters[i][2], //set,payload_on,payload_off,unit_of_meas,
                            0, //set  off_delay
                            "", "", false, "", //set,payload_available,payload_not available   ,is a gateway entity, command topic
                            (char*)idWoKey.c_str(), "", pdevice->modelName, (char*)idWoKey.c_str(), false, // device name, device manufacturer, device model, device ID, retain
                            "" //State Class
            );
          } else if (strcmp(parameters[i][0], "strike_count") == 0) {
            createDiscovery("sensor", //set Type
                            (char*)topic.c_str(), parameters[i][1], pdevice->uniqueId, //set state_topic,name,uniqueId
                            "", parameters[i][3], (char*)value_template.c_str(), //set availability_topic,device_class,value_template,
                            "1", "0", parameters[i][2], //set,payload_on,payload_off,unit_of_meas,
                            0, //set  off_delay
                            "", "", false, "", //set,payload_available,payload_not available   ,is a gateway entity, command topic
                            (char*)idWoKey.c_str(), "", pdevice->modelName, (char*)idWoKey.c_str(), false, // device name, device manufacturer, device model, device ID, retain
                            stateClassTotalIncreasing //State Class
            );
          } else if (strcmp(parameters[i][0], "event") == 0 && strcmp(pdevice->modelName, "Govee-Water") == 0) { //the entity will detect Water Leak Event and go back to Off state after 60seconds
            createDiscovery("binary_sensor", //set Type
                            (char*)topic.c_str(), parameters[i][1], pdevice->uniqueId, //set state_topic,name,uniqueId
                            "", parameters[i][3], (char*)value_template.c_str(), //set availability_topic,device_class,value_template,
                            "Water Leak", "", parameters[i][2], //set,payload_on,payload_off,unit_of_meas,
                            60, //set  off_delay
                            "", "", false, "", //set,payload_available,payload_not available   ,is a gateway entity, command topic
                            (char*)idWoKey.c_str(), "Govee", pdevice->modelName, (char*)idWoKey.c_str(), false, // device name, device manufacturer, device model, device ID, retain
                            stateClassMeasurement //State Class
            );
          } else if (strcmp(pdevice->modelName, "Interlogix-Security") != 0) {
            createDiscovery("sensor", //set Type
                            (char*)topic.c_str(), parameters[i][1], pdevice->uniqueId, //set state_topic,name,uniqueId
                            "", parameters[i][3], (char*)value_template.c_str(), //set availability_topic,device_class,value_template,
                            "", "", parameters[i][2], //set,payload_on,payload_off,unit_of_meas,
                            0, //set  off_delay
                            "", "", false, "", //set,payload_available,payload_not available   ,is a gateway entity, command topic
                            (char*)idWoKey.c_str(), "", pdevice->modelName, (char*)idWoKey.c_str(), false, // device name, device manufacturer, device model, device ID, retain
                            stateClassMeasurement //State Class
            );
          }
          pdevice->isDisc = true;
          if (xSemaphoreTake(semaphorecreateOrUpdateDeviceRTL_433, pdMS_TO_TICKS(QueueSemaphoreTimeOutLoop)) == pdTRUE) {
            RTL_433devices.markDiscovered(pdevice->uniqueId);
            xSemaphoreGive(semaphorecreateOrUpdateDeviceRTL_433);
          } else {
            Log.error(F("[rtl_433] discovery completion semaphore timeout" CR));
          }
          break;
        }
      }
      if (!pdevice->isDisc) {
        DISCOVERY_TRACE_LOG(F("Device id %s was not discovered" CR), pdevice->uniqueId); // Remove from production release ?
      }
    } else {
      DISCOVERY_TRACE_LOG(F("Device already discovered or that doesn't require discovery %s" CR), pdevice->uniqueId);
    }
  }
}

void storeRTL_433Discovery(JsonObject& RFrtl_433_ESPdata, const char* model, const char* type, const char* uniqueid) {
  //Sanitize model name
  String modelSanitized = model;
  modelSanitized.replace(" ", "_");
  modelSanitized.replace("/", "_");
  modelSanitized.replace(".", "_");
  modelSanitized.replace("&", "");

  //Sensors translation matrix for sensors that requires statistics by using stateClassMeasurement
  size_t numRows = sizeof(parameters) / sizeof(parameters[0]);

  for (int i = 0; i < numRows; i++) {
    if (RFrtl_433_ESPdata.containsKey(parameters[i][0])) {
      String key_id = String(uniqueid) + "-" + String(parameters[i][0]);
      createOrUpdateDeviceRTL_433((char*)key_id.c_str(), (char*)modelSanitized.c_str(), (char*)type, device_flags_init);
    }
  }
}
#  endif

void rtl_433_Callback(char* message) {
  DynamicJsonDocument jsonBuffer2(JSON_MSG_BUFFER);
  JsonObject RFrtl_433_ESPdata = jsonBuffer2.to<JsonObject>();
  auto error = deserializeJson(jsonBuffer2, message);
  if (error) {
    Log.error(F("[rtl_433] deserializeJson() failed: %s" CR), error.c_str());
    return;
  }

  unsigned long MQTTvalue = (int)RFrtl_433_ESPdata["id"] + round((float)RFrtl_433_ESPdata["temperature_C"]);
  String topic = subjectRTL_433toMQTT;
  String model = RFrtl_433_ESPdata["model"];
  String type = RFrtl_433_ESPdata["type"];
  String uniqueid;

  const char naming_keys[5][8] = {"type", "model", "subtype", "channel", "id"}; // from rtl_433_mqtt_hass.py
  size_t numRows = sizeof(naming_keys) / sizeof(naming_keys[0]);
  for (int i = 0; i < numRows; i++) {
    if (RFrtl_433_ESPdata.containsKey(naming_keys[i])) {
      if (uniqueid == 0) {
        uniqueid = RFrtl_433_ESPdata[naming_keys[i]].as<String>(); // Start of the unique id with the first key
      } else {
        uniqueid = uniqueid + "/" + RFrtl_433_ESPdata[naming_keys[i]].as<String>(); // Following keys
      }
    }
  }

#  if valueAsATopic
  topic = topic + "/" + uniqueid;
#  endif

  uniqueid.replace("/", "-");

  DISCOVERY_TRACE_LOG(F("uniqueid: %s" CR), uniqueid.c_str());
  if (!isAduplicateSignal(MQTTvalue)) {
#  ifdef ZmqttDiscovery
    if (SYSConfig.discovery)
      storeRTL_433Discovery(RFrtl_433_ESPdata, (char*)model.c_str(), (char*)type.c_str(), (char*)uniqueid.c_str());
#  endif
    RFrtl_433_ESPdata["origin"] = (char*)topic.c_str();
    enqueueJsonObject(RFrtl_433_ESPdata);
    storeSignalValue(MQTTvalue);
  }
#  ifdef MEMORY_DEBUG
  Log.trace(F("Post rtl_433_Callback: %d" CR), ESP.getFreeHeap());
#  endif
}

void setupRTL_433() {
  rtl_433.setCallback(rtl_433_Callback, messageBuffer, JSON_MSG_BUFFER);
#  ifdef ZmqttDiscovery
  semaphorecreateOrUpdateDeviceRTL_433 = xSemaphoreCreateMutexStatic(&semaphorecreateOrUpdateDeviceRTL_433Buffer);
  if (!semaphorecreateOrUpdateDeviceRTL_433)
    Log.error(F("[rtl_433] discovery cache mutex unavailable; discovery disabled" CR));
#  endif
  Log.trace(F("ZgatewayRTL_433 command topic: %s%s%s" CR), mqtt_topic, gateway_name, subjectMQTTtoRFset);
  Log.notice(F("ZgatewayRTL_433 setup done " CR));
}

void RTL_433Loop() {
  rtl_433.loop();
}

extern void enableRTLreceive() {
  Log.notice(F("Enable RTL_433 Receiver: %FMhz" CR), RFConfig.frequency);
  rtl_433.initReceiver(RF_MODULE_RECEIVER_GPIO, RFConfig.frequency);
  rtl_433.enableReceiver();
}

extern void disableRTLreceive() {
  Log.trace(F("disableRTLreceive" CR));
  rtl_433.disableReceiver();
}

extern int getRTLrssiThreshold() {
  return rtl_433.rssiThreshold;
}

extern int getRTLAverageRSSI() {
  return rtl_433.averageRssi;
}

extern int getRTLCurrentRSSI() {
  return rtl_433.currentRssi;
}

extern int getRTLMessageCount() {
  return rtl_433.messageCount;
}

#  if defined(RF_SX1276) || defined(RF_SX1278)
extern int getOOKThresh() {
  return rtl_433.OokFixedThreshold;
}
#  endif

#endif
