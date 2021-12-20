#ifndef ARDUINO_MQTTCONNECTOR_H
#define ARDUINO_MQTTCONNECTOR_H

#include <Arduino.h>

void    MQTTBegin();
void    MQTTLoop();
boolean MQTTPublish(String topic, String payload);
void    MQTTSetCallback(void (*callback)(char* topic, byte* payload, unsigned int length));
boolean MQTTSubscribe(String topic);
boolean MQTTIsConnected();

#endif /* ARDUINO_MQTTCONNECTOR_H */