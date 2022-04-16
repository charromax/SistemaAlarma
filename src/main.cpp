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
const String ON = "ON";
const String OFF = "OFF";
const String DEACTIVATED = "DEACTIVATED";
const String ACTIVATED = "ACTIVATED";
const String REPORT = "REPORT";
const String TOTEM_TYPE = "LED_CONTROL";
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
void checkResetButton();
void IRAM_ATTR resetFlagSetting();
void clearFilesystem();
void handleOnRequest();
void handleOffRequest();
void sendReport();
void runIntervalMode();
String buildResponse();
String buildPayload();
void setupLedStripControl();
void changeColor();
void fadeColorMode();
void redFade();
void greenFade();
void blueFade();
void rangeColorMode();

//########################################################## GLOBALS ###############################################

// Setting PWM frequency, channels and bit resolution
const int freq = 5000;
const int redChannel = 0;
const int greenChannel = 1;
const int blueChannel = 2;
// Bit resolution 2^8 = 256
const int resolution = 8;

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0;

char sensorTopic[100];
int redColorPin = D0;
int greenColorPin = D1;
int blueColorPin = D2;
int red = 0;   // 0-256
int green = 0; // 0-256
int blue = 0;  // 0-256
int SPEED = 1; // 1 - 10
String CHANNEL = "G";
bool rUP = true; // know when to increment or decrement color value1
bool gUP = true;
bool bUP = true;
int resetButton = D8;
String MODE = "manual";
bool isActivated = true;
bool shouldSaveConfig = false; // flag for saving data
WiFiManager wifi;
bool shouldResetEsp = false;
bool newPayloadReceived = false;
bool shouldChangeColor = false;
String newPayload = "";
int resetHoldingTime = 0;
WiFiUDP clockServer;
NTPClient clockClient(clockServer, "south-america.pool.ntp.org", utcOffset);

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
    Serial.println("MQTT payload arrived");
    char payloadStr[length + 1];
    memset(payloadStr, 0, length + 1);
    strncpy(payloadStr, (char *)payload, length);
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
  if (newPayload != "" && newPayloadReceived)
  {
    StaticJsonDocument<192> doc;
    DeserializationError error = deserializeJson(doc, newPayload);
    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }
    MODE = ((String)doc["mode"]);
    SPEED = doc["speed"];                     // 2
    CHANNEL = ((String)doc["range_channel"]); // "B"
    red = doc["red"];
    green = doc["green"]; // RGB values
    blue = doc["blue"];
    shouldChangeColor = true;
    newPayload = "";
    Serial.println("MQTT payload decoded");
  }
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

void setupLedStripControl()
{
  // configure LED PWM resolution/range and set pins to LOW
  analogWriteRange(resolution);
  changeColor();
}

/**
 * @brief changes color once to selected values
 *
 */
void changeColor()
{
  if (shouldChangeColor)
  {
    shouldChangeColor = false;
    Serial.println("Changing color...");
    analogWrite(redColorPin, red);
    analogWrite(greenColorPin, green);
    analogWrite(blueColorPin, blue);
  }
}

void fadeColorMode()
{
  if (MODE == "FADE")
  {
    Serial.println("fade mode");
    shouldChangeColor = true;
    redFade();
    greenFade();
    blueFade();
    changeColor();
  }
}

void rangeColorMode()
{
  if (MODE == "RANGE")
  {
    Serial.println("range mode channel = " + CHANNEL);
    shouldChangeColor = true;
    if (CHANNEL == "R")
    {
      greenFade();
      blueFade();
    }
    if (CHANNEL == "G")
    {
      redFade();
      blueFade();
    }
    if (CHANNEL == "B")
    {
      redFade();
      greenFade();
    }
    changeColor();
  }
}

void redFade()
{
  if (red > 255)
  {
    rUP = false;
  }
  if (red < 0)
  {
    rUP = true;
  }
  if (rUP)
    red = red + SPEED;
  else
    red = red - SPEED;
}

void greenFade()
{
  if (green > 255)
  {
    gUP = false;
  }
  if (green < 0)
  {
    gUP = true;
  }
  if (gUP)
    green = green + SPEED;
  else
    green = green - SPEED;
}

void blueFade()
{
  if (blue > 255)
  {
    bUP = false;
  }
  if (blue < 0)
  {
    bUP = true;
  }
  if (bUP)
    blue = blue + SPEED;
  else
    blue = blue - SPEED;
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

void checkResetButton()
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

void resetFlagSetting()
{
  if (digitalRead(resetButton) == HIGH)
  {
    resetHoldingTime++;
    shouldResetEsp = false;
    if (resetHoldingTime > 100)
    {
      resetHoldingTime = 0;
      shouldResetEsp = true;
    }
  }
  else
  {
    resetHoldingTime = 0;
  }
}

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(9600);
  setupWifi();
  MQTTBegin();
  MQTTSetCallback(mqttCallback);
  pinMode(resetButton, INPUT);
  setupLedStripControl();
  pinMode(LED_BUILTIN, OUTPUT);
  turnOffBuiltInLED();
}

void loop()
{
  // put your main code here, to run repeatedly:
  // clockClient.update();
  resetFlagSetting();
  checkResetButton();
  checkPayload();
  changeColor();
  fadeColorMode();
  rangeColorMode();
  MQTTLoop(sensorTopic);
  MQTTSubscribe(sensorTopic);
}
