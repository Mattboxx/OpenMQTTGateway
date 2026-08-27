/*
  Theengs OpenMQTTGateway - MQTT outage Wake-on-LAN configuration

  This file is part of OpenMQTTGateway and is distributed under the terms of
  the GNU General Public License version 3 or later.
*/
#ifndef config_mqttWOL_h
#define config_mqttWOL_h

#ifndef MQTT_WOL_MAC
#  define MQTT_WOL_MAC ""
#endif
#ifndef MQTT_WOL_PORT
#  define MQTT_WOL_PORT 9
#endif
#ifndef MQTT_WOL_INITIAL_DELAY_MS
#  define MQTT_WOL_INITIAL_DELAY_MS (60UL * 1000UL)
#endif
#ifndef MQTT_WOL_REPEAT_INTERVAL_MS
#  define MQTT_WOL_REPEAT_INTERVAL_MS (20UL * 60UL * 1000UL)
#endif
#ifndef MQTT_WOL_MIN_FAILURES
#  define MQTT_WOL_MIN_FAILURES 3
#endif
#ifndef MQTT_WOL_DEFAULT_ENABLED
#  define MQTT_WOL_DEFAULT_ENABLED false
#endif
#ifndef MQTT_WOL_ON_TRANSPORT_ERROR
#  define MQTT_WOL_ON_TRANSPORT_ERROR true
#endif
#ifndef MQTT_WOL_ON_BROKER_ERROR
#  define MQTT_WOL_ON_BROKER_ERROR false
#endif
#ifndef MQTT_WOL_ON_AUTH_ERROR
#  define MQTT_WOL_ON_AUTH_ERROR false
#endif

#endif
