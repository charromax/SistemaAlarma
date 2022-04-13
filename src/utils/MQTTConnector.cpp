#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "MQTTConnector.h"

#define MQTT_BROKER       "br5.maqiatto.com"
#define MQTT_BROKER_PORT  1883
#define MQTT_USERNAME     "charr0max"
#define MQTT_KEY          "Mg412115"

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

boolean mqttInitCompleted = false;
String clientId = "sensor" + String(ESP.getChipId());

void performConnect(String topic)
{
  uint16_t connectionDelay = 5000;
  while (!mqtt.connected())
  {
    Serial.printf("Trace   : Attempting MQTT connection...\n");
    if (mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_KEY))
    {
      Serial.printf("Trace   : Connected to Broker.\n");

      /* Subscription to your topic after connection was succeeded.*/
      MQTTSubscribe(topic);
    }
    else
    {
      Serial.printf("Error!  : MQTT Connect failed, rc = %d\n", mqtt.state());
      Serial.printf("Trace   : Trying again in %d msec.\n", connectionDelay);
      delay(connectionDelay);
    }
  }
}

boolean MQTTPublish(String topic, String payload)
{
  String topic_char = topic;
  char * payload_char = new char[payload.length() + 1];
  strcpy(payload_char, payload.c_str());
  boolean retval = false;
  if (mqtt.connected())
  {
    retval = mqtt.publish(topic_char.c_str(), payload_char);
  }
  return retval;
}

boolean MQTTSubscribe(String topic)
{
  boolean retval = false;
  if (mqtt.connected())
  {
    retval = mqtt.subscribe(topic.c_str());
  }
  return retval;
}

boolean MQTTIsConnected()
{
  return mqtt.connected();
}

void MQTTBegin()
{
  mqtt.setServer(MQTT_BROKER, MQTT_BROKER_PORT);
  mqttInitCompleted = true;
}

void MQTTSetCallback(void (*callback)(char* topic, byte* payload, unsigned int length)) {
  mqtt.setCallback(*callback);
}

void MQTTLoop(String topic)
{
  if(mqttInitCompleted)
  {
    if (!MQTTIsConnected())
    {
      performConnect(topic);
    }
    mqtt.loop();
  }
}