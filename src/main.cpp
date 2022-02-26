#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "./utils/MQTTConnector.h"
#include <WiFiManager.h>    

//########################################################## CONSTANTS #############################################################

String sensorTopic = "home/terrace/pump";
String ON = "ON";
String OFF = "OFF";
String DEACTIVATED = "DEACTIVATED";
String ACTIVATED = "ACTIVATED";
String REPORT = "REPORT";

//########################################################## FUNCTION DECLARATIONS #############################################################

void setupWifi();
void mqttCallback(char *, byte *, unsigned int);
void blink();
void checkPayload(String);

//########################################################## GLOBALS #############################################################

int pumpPin = D8; // water pump is connected to D8 pin
bool isActivated = true;
WiFiServer server(80);

//########################################################## CODE #############################################################

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(9600);
  setupWifi();
  // using mac address as device ID for topic
  MQTTBegin();
  MQTTSetCallback(mqttCallback);
  pinMode(pumpPin, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
}

/**
 * @brief begin wifi connection
 * 
 */
void setupWifi()
{
  WiFiManager wifi;
  
  // Uncomment and run it once, if you want to erase all the stored information
  //wifiManager.resetSettings();

  // fetches ssid and pass from eeprom and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "AutoConnectAP"
  // and goes into a blocking loop awaiting configuration
  wifi.autoConnect("Hercules");
  
  // if you get here you have connected to the WiFi
  Serial.println("Connected.");
  
  server.begin();
}

/**
 * @brief mqtt messages are received here
 * 
 * @param topic 
 * @param payload 
 * @param length 
 */
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  if (length > 0)
  {
    char payloadStr[length + 1];
    memset(payloadStr, 0, length + 1);
    strncpy(payloadStr, (char *)payload, length);
    checkPayload(payloadStr);
  }
}

/**
 * @brief decode MQTT payload and de/activate sensor accordingly
 * 
 * @param payload 
 */
void checkPayload(String payload)
{
  if (payload != nullptr && payload != "")
  {
    if (payload.equals(ON))
    {
      digitalWrite(LED_BUILTIN, LOW);
      digitalWrite(pumpPin, HIGH);
      isActivated = true;
    }
    else if (payload.equals(OFF))
    {
      isActivated = false;
      digitalWrite(pumpPin, LOW);
      digitalWrite(LED_BUILTIN, HIGH);
    }
    else if (payload.equals(REPORT)) {
      if (isActivated) {
        MQTTPublish(sensorTopic, ACTIVATED);
        } else {
           MQTTPublish(sensorTopic, DEACTIVATED);
           }
    }
  }
}

/**
 * @brief makes built in led blink TEST PURPOSES
 */
void blink()
{
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(300);
}

void loop()
{
  // put your main code here, to run repeatedly:
  MQTTLoop();
  MQTTSubscribe(sensorTopic);
}
