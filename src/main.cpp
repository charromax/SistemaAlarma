#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <SimpleTimer.h>
#include "./utils/MQTTConnector.h"

//########################################################## CONSTANTS #############################################################

String sensorTopic = "home/office/door";
String ALL_OK = "ALL_OK";
String ON = "ON";
String OFF = "OFF";
String ALARM = "ALARM";
String DEACTIVATED = "DEACTIVATED";

//########################################################## END CONSTANTS #############################################################

//########################################################## WIFI CREDENTIALS #############################################################

char ssid[] = "FUMANCHU";
char pass[] = "heyholetsgo";

//########################################################## END WIFI CREDENTIALS #############################################################

//########################################################## FUNCTION DECLARATIONS #############################################################

void setupWifi();
void mqttCallback(char *, byte *, unsigned int);
void getSensorValue();
void blink();
void checkPayload(String);

//########################################################## END FUNCTION DECLARATIONS #############################################################

int sensor = D0; // magnetic or otherwise triggereable sensor
bool isActivated = true;
bool sentDeactivationMessageAlready = false;
SimpleTimer timer;

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(9600);
  setupWifi();
  // using mac address as device ID for topic
  MQTTBegin(WiFi.macAddress());
  MQTTSetCallback(mqttCallback);
  pinMode(sensor, INPUT_PULLDOWN_16);
  pinMode(LED_BUILTIN, OUTPUT);
  timer.setInterval(1000L, getSensorValue);
}

/**
 * @brief begin wifi connection
 * 
 */
void setupWifi()
{
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Device Ready for Operation");
}

/**
 * @brief Get the Sensor Value from connected sensor
 * and publish to MQTT broker
 */
void getSensorValue()
{
  if (isActivated)
  {
    if (digitalRead(sensor) == LOW)
    {
      //door is closed ALL GOOD
      MQTTPublish(sensorTopic, ALL_OK);
    }
    if (digitalRead(sensor) == HIGH)
    {
      blink();
      //door is open ALARM ALARM
      MQTTPublish(sensorTopic, ALARM);
    }
  } else {
    if (!sentDeactivationMessageAlready){
      MQTTPublish(sensorTopic, DEACTIVATED);
      sentDeactivationMessageAlready = true;
    }
    
  }
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
      isActivated = true;
      sentDeactivationMessageAlready = false;
    }
    else if (payload.equals(OFF))
    {
      isActivated = false;
    }
  }
}

/**
 * @brief makes built in led blink TEST PURPOSES
 */
void blink()
{
  digitalWrite(LED_BUILTIN, HIGH);
  delay(300);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
}

void loop()
{
  // put your main code here, to run repeatedly:
  timer.run();
  MQTTLoop();
  MQTTSubscribe(sensorTopic);
}
