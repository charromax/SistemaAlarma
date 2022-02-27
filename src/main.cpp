#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <SimpleTimer.h>
#include "./utils/MQTTConnector.h"
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include "LittleFS.h"

//########################################################## CONSTANTS #############################################################

char sensorTopic[100];
char AP_SSID[] = "HerculesTotemAP";
char CONFIG_FILE[] = "/config.json";
String ALL_OK = "ALL_OK";
String ON = "ON";
String OFF = "OFF";
String ALARM = "ALARM";
String DEACTIVATED = "DEACTIVATED";

//########################################################## FUNCTION DECLARATIONS #############################################################

void setupWifi();
void mqttCallback(char *, byte *, unsigned int);
void getSensorValue();
void blink();
void checkPayload(String);
void turnOnBuiltInLED();
void turnOffBuiltInLED();
void saveConfigCallback();
void tryOpenConfigFile();
void saveNewConfig(const char *);

//########################################################## GLOBALS #############################################################

int sensor = D0; // magnetic or otherwise triggereable sensor
bool isActivated = true;
String currentState = ALL_OK;
SimpleTimer timer;
bool shouldSaveConfig = false; // flag for saving data

//########################################################## CODE #############################################################

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(9600);
  setupWifi();
  // using mac address as device ID for topic
  MQTTBegin();
  MQTTSetCallback(mqttCallback);
  pinMode(sensor, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  timer.setInterval(1000L, getSensorValue);
  turnOffBuiltInLED();
}

void tryOpenConfigFile()
{

  // read configuration from FS json
  Serial.println("mounting FS...");

  if (LittleFS.begin())
  {
    // clean FS, for testing
    // LittleFS.format();
    Serial.println("mounted file system");
    if (LittleFS.exists(CONFIG_FILE))
    {
      // file exists, reading and loading
      Serial.println("reading config file");
      File configFile = LittleFS.open(CONFIG_FILE, "r");
      if (configFile)
      {
        StaticJsonDocument<512> doc;
        // Deserialize the JSON document
        DeserializationError error = deserializeJson(doc, configFile);
        if (error)
        {
          Serial.println("failed to load json config");
        }
        else
        {
          Serial.println("parsed json");
          strcpy(sensorTopic, doc["topic"]);
        }
      }
    }
    LittleFS.end();
  }
  else
  {
    Serial.println("failed to mount FS");
  }
  // end read
}

void saveNewConfig(const char *newTopic)
{
  if (LittleFS.begin())
  {
    Serial.println("Attempting to save new topic config");
    Serial.println(newTopic);
    if (shouldSaveConfig)
    {
      LittleFS.remove(CONFIG_FILE);
      // Open file for writing
      File file = LittleFS.open(CONFIG_FILE, "w");
      if (!file)
      {
        Serial.println(F("Failed to create file"));
        return;
      }
      StaticJsonDocument<256> doc;

      // Set the values in the document
      doc["topic"] = newTopic;

      // Serialize JSON to file
      if (serializeJson(doc, file) == 0)
      {
        Serial.println(F("Failed to write to file"));
      }
      // Close the file
      file.close();
    }
    LittleFS.end();
  } else {
    Serial.println(F("Unable to begin filesystem manager"));
  }
}

/**
 * @brief begin wifi connection
 *
 */
void setupWifi()
{
  tryOpenConfigFile();
  WiFiManagerParameter customTopic("topic", "Topic:", sensorTopic, 100);
  WiFiManager wifi;
  wifi.resetSettings();
  wifi.addParameter(&customTopic);
  wifi.setSaveConfigCallback(saveConfigCallback);
  wifi.setMinimumSignalQuality(15);

  // fetches ssid and pass from eeprom and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "AutoConnectAP"
  // and goes into a blocking loop awaiting configuration
  turnOnBuiltInLED();
  wifi.autoConnect(AP_SSID, NULL);
  // if you get here you have connected to the WiFi
  Serial.println("Connected.");
  turnOffBuiltInLED();
  if (customTopic.getValue() != NULL && customTopic.getValueLength() > 5)
  {
    strcpy(sensorTopic, customTopic.getValue());
    saveNewConfig(customTopic.getValue());
  }
  else
  {
    Serial.println("Invalid topic input, resetting to defaults");
    wifi.resetSettings();
  }
}

/**
 * @brief Get the Sensor Value from connected sensor
 * and publish to MQTT broker
 */
void getSensorValue()
{
  if (isActivated)
  {
    digitalWrite(LED_BUILTIN, LOW);
    if (digitalRead(sensor) == HIGH && !currentState.equals(ALL_OK))

    {
      // door is closed ALL GOOD
      currentState = ALL_OK;
      MQTTPublish(sensorTopic, ALL_OK);
    }
    if (digitalRead(sensor) == LOW && !currentState.equals(ALARM))
    {
      blink();
      // door is open ALARM ALARM
      currentState = ALARM;
      MQTTPublish(sensorTopic, ALARM);
    }
  }
  else
  {
    if (!currentState.equals(DEACTIVATED))
    {
      MQTTPublish(sensorTopic, DEACTIVATED);
      currentState = DEACTIVATED;
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
 * @brief callback to save config data
 * in filesystem
 */
void saveConfigCallback()
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
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
      isActivated = true;
    }
    else if (payload.equals(OFF))
    {
      isActivated = false;
      digitalWrite(LED_BUILTIN, HIGH);
    }
  }
}

/**
 * @brief makes built in led blink TEST PURPOSES
 */
void blink()
{
  turnOnBuiltInLED();
  delay(300);
  turnOffBuiltInLED();
  delay(300);
}

void turnOnBuiltInLED()
{
  digitalWrite(LED_BUILTIN, LOW);
}

void turnOffBuiltInLED()
{
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop()
{
  // put your main code here, to run repeatedly:
  timer.run();
  MQTTLoop();
  MQTTSubscribe(sensorTopic);
}
