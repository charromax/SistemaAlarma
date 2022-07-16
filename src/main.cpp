#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "./utils/MQTTConnector.h"
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include "LittleFS.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

//########################################################## CONSTANTS ###########################################

char AP_SSID[] = "HerculesTotemAP";
const char null[5] = "null";
char CONFIG_FILE[] = "/config.json";
const String ALL_OK = "ALL_OK";
const String ON = "ON";
const String OFF = "OFF";
const String DEACTIVATED = "DEACTIVATED";
const String ACTIVATED = "ACTIVATED";
const String REPORT = "REPORT";
const String INTERVAL = "INTERVAL";
const String TOTEM_TYPE = "WATER_PUMP";
const String MANUAL = "MANUAL"; // operation modes
const String REMOTE = "REMOTE"; // operation modes
const long utcOffset = -10800;

//########################################################## FUNCTION DECLARATIONS #############################################################

void setupWifi();
void mqttCallback(char *, byte *, unsigned int);
void blink();
void checkPayload(String);
void turnOnBuiltInLED();
void turnOffBuiltInLED();
void saveConfigCallback();
void tryOpenConfigFile();
void saveNewConfig(const char *);
void performReset();
void checkResetButtonPress();
void checkManualPumpPressed();
void clearFilesystem();
void handleOnRequest();
void handleOffRequest();
void turnOn();
void turnOff();
void sendReport();
void handleIntervalRequest();
void runIntervalMode();
String buildResponse();
String buildPayload();

//########################################################## GLOBALS ###############################################

char sensorTopic[100];
int pumpPin = D1; // water pump
int manualPump = D2;
int resetButton = D7;
bool isActivated = true;
bool shouldSaveConfig = false; // flag for saving data
WiFiManager wifi;
bool shouldResetEsp = false;
bool newPayloadReceived = false;
String newPayload = "";
WiFiUDP clockServer;
NTPClient clockClient(clockServer, "south-america.pool.ntp.org", utcOffset);
int resetHoldingTime = 0;
bool pumpButtonPressed = false;
String currentMode = MANUAL;
bool currentState = false;

//########################################################## CODE ###################################################

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
  }
  else
  {
  }
}

void clearFilesystem()
{
  if (LittleFS.begin())
  {
    if (LittleFS.format())
    {
      Serial.println(F("Device memory wiped!"));
    }
    LittleFS.end();
  }
  else
  {
    Serial.println(F("Unable to format device"));
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
  if (customTopic.getValue() != sensorTopic)
  {
    Serial.println("topic value");
    Serial.println(customTopic.getValue());
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
    currentMode = REMOTE;
    Serial.println("current mode = REMOTE");
    newPayload = payloadStr;
    newPayloadReceived = true;
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
void checkPayload()
{
  if (newPayload != "" && newPayloadReceived && currentMode == REMOTE)
  {
    resetHoldingTime = 0;
    if (newPayload.equals(ON))
    {
      handleOnRequest();
    }
    else if (newPayload.equals(OFF))
    {
      handleOffRequest();
    }
    else if (newPayload.equals(INTERVAL))
    {
      handleIntervalRequest();
    }
    else if (newPayload.equals(REPORT))
    {
      sendReport();
    }
    newPayloadReceived = false;
    newPayload = "";
  }
}

void handleIntervalRequest()
{
  runIntervalMode();
  MQTTPublish(sensorTopic, buildResponse());
}

void handleOnRequest()
{
  turnOn();
  MQTTPublish(sensorTopic, buildResponse());
}

void turnOn()
{
  if (!isActivated)
  {
    Serial.println("turn on");
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(pumpPin, HIGH);
    isActivated = true;
  }
}

void turnOff()
{
  if (isActivated)
  {
    Serial.println("turn off");
    isActivated = false;
    digitalWrite(pumpPin, LOW);
    digitalWrite(LED_BUILTIN, HIGH);
  }
}

/**
 * @brief received OFF request from mqtt broker
 *
 */
void handleOffRequest()
{
  turnOff();
  MQTTPublish(sensorTopic, buildResponse());
}

/**
 * @brief received REPORT request from mqtt broker
 *
 */
void sendReport()
{
  MQTTPublish(sensorTopic, buildResponse());
}

String buildResponse()
{
  StaticJsonDocument<128> doc;
  String output;
  doc["type"] = TOTEM_TYPE;
  doc["is_active"] = isActivated;
  doc["is_power_on"] = isActivated;
  doc["payload"] = buildPayload();
  serializeJson(doc, output);
  Serial.println("RESPONSE: " + output);
  return output;
}

/**
 * @brief build specific payload
 *
 * @return String
 */
String buildPayload()
{
  StaticJsonDocument<64> doc;
  String payload;
  doc["is_working"] = true;
  doc["cycle"] = "MANUAL";
  serializeJson(doc, payload);
  payload.replace('"', '*');
  Serial.println("PAYLOAD: " + payload);
  return payload;
}

/**
 * @brief run for specified amount of time then cut off for andther specified time
 *
 */
void runIntervalMode()
{
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

void performReset()
{
  if (shouldResetEsp)
  {
    blink();
    shouldResetEsp = false;
    Serial.println("Terminating processes and resetting...");
    wifi.resetSettings();
    clearFilesystem();
    delay(500);
    setupWifi();
  }
}

void checkResetButtonPress()
{
  if (digitalRead(resetButton))
  {
    Serial.print('.');
    resetHoldingTime++;
    shouldResetEsp = false;
  }
  else
  {
    if (resetHoldingTime > 100)
    {
      resetHoldingTime = 0;
      shouldResetEsp = true;
    }
  }
}

void checkManualPumpPressed()
{
  if (currentMode == MANUAL)
  {
    if (digitalRead(manualPump))
    {
      Serial.println("Manual Pumping...");
      turnOn();
    }
    else
      turnOff();
  }
  else
  {
    if (digitalRead(manualPump))
    {
      currentMode = MANUAL;
      resetHoldingTime = 0;
      Serial.println("current mode = MANUAL");
    }
  }
}

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(9600);
  setupWifi();
  // using mac address as device ID for topic
  MQTTBegin();
  MQTTSetCallback(mqttCallback);
  pinMode(pumpPin, OUTPUT);
  pinMode(manualPump, INPUT);
  pinMode(resetButton, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  turnOffBuiltInLED();
}

void loop()
{
  // put your main code here, to run repeatedly:
  clockClient.update();
  checkResetButtonPress();
  performReset();
  checkManualPumpPressed();
  checkPayload();
  MQTTLoop();
  MQTTSubscribe(sensorTopic);
}
