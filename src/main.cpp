#include <Arduino.h>
#include "LittleFS.h"
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

#define MQTT_BROKER "br5.maqiatto.com"
#define MQTT_BROKER_PORT 1883
#define MQTT_USERNAME "charr0max"
#define MQTT_KEY "Mg412115"
// ########################################################## CONSTANTS ###########################################

char AP_SSID[] = "HerculesTotemAP";
const char null[5] = "null";
char CONFIG_FILE[] = "/config.json";

// ########################################################## FUNCTION DECLARATIONS ##############################

void setupWifi();
void reset();
void mqttCallback(char *, byte *, unsigned int);
void blink();
void checkPayload();
void turnOnBuiltInLED();
void turnOffBuiltInLED();
void saveConfigCallback();
void tryOpenConfigFile();
void saveNewConfig(const char *);
void checkResetButton();
void setDeviceState(int);
void IRAM_ATTR resetCallback();
void clearFilesystem();
void publishDeviceState();
void reconnect();

// ########################################################## GLOBALS ###############################################

char sensorTopic[100];
bool outlet1State = false;
bool outlet2State = false;
bool outlet3State = false;
bool outlet4State = false;
int outlet1 = D5;
int outlet2 = D6;
int outlet3 = D7;
int outlet4 = D8;
int resetButton = D1;
bool isActivated = true;
bool shouldSaveConfig = false; // flag for saving data
WiFiClient espClient;
WiFiManager wifi;
bool shouldResetEsp = false;
String currentPayload = "";
PubSubClient client(espClient);

// ########################################################## CODE ###################################################

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
    Serial.println("No file exists");
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
  wifi.autoConnect(AP_SSID);
  // if you get here you have connected to the WiFi
  Serial.println("Connected.");
  turnOffBuiltInLED();
  if (customTopic.getValue() != nullptr && customTopic.getValueLength() > 0 && customTopic.getValue() != sensorTopic)
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
    Serial.println("received message ");
    char payloadStr[length + 1];
    memset(payloadStr, 0, length + 1);
    strncpy(payloadStr, (char *)payload, length);
    currentPayload = payloadStr;
  }
}

void reconnect()
{
  // reconnect code from PubSubClient example
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "sensor" + String(ESP.getChipId());
    // Attempt to connect
    if (client.connect(clientId.c_str(), MQTT_USERNAME, MQTT_KEY))
    {
      Serial.println("connected");
      // Once connected, publish an announcement...
      publishDeviceState();
      // ... and resubscribe
      client.subscribe(sensorTopic);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
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
  if (currentPayload != nullptr && currentPayload != "")
  {
    blink();
    StaticJsonDocument<96> doc;
    DeserializationError error = deserializeJson(doc, currentPayload);
    if (error)
    {
      currentPayload = "";
      return;
    }
    int outletNumber = doc["outletNumber"]; // 1
    bool refresh = doc["refresh"];
    if (refresh)
      publishDeviceState();
    else
      setDeviceState(outletNumber);
    currentPayload = "";
  }
}

void setDeviceState(int outletNumber)
{
  switch (outletNumber)
  {
  case 1:
    if (outlet1State)
    {
      // turn off dev 1
      digitalWrite(outlet1, HIGH);
      outlet1State = false;
    }
    else
    {
      // turno on dev 1
      digitalWrite(outlet1, LOW);
      outlet1State = true;
    }
    break;
  case 2:
    if (outlet2State)
    {
      // turn off dev 2
      digitalWrite(outlet2, HIGH);
      outlet2State = false;
    }
    else
    {
      // turno on dev 2
      digitalWrite(outlet2, LOW);
      outlet2State = true;
    }
    break;
  case 3:
    if (outlet3State)
    {
      // turn off dev 3
      digitalWrite(outlet3, HIGH);
      outlet3State = false;
    }
    else
    {
      // turno on dev 3
      digitalWrite(outlet3, LOW);
      outlet3State = true;
    }
    break;
  case 4:
    if (outlet4State)
    {
      // turn off dev 4
      digitalWrite(outlet4, HIGH);
      outlet4State = false;
    }
    else
    {
      // turno on dev 4
      digitalWrite(outlet4, LOW);
      outlet4State = true;
    }
    break;

  default:
    break;
  }
  publishDeviceState();
}

void publishDeviceState()
{
  String output;
  StaticJsonDocument<256> doc;
  String outlet = "outletNumber";
  String state = "outletState";
  JsonArray devices = doc.createNestedArray("devices");

  JsonObject devices_0 = devices.createNestedObject();
  devices_0[outlet] = 1;
  devices_0[state] = outlet1State;

  JsonObject devices_1 = devices.createNestedObject();
  devices_1[outlet] = 2;
  devices_1[state] = outlet2State;

  JsonObject devices_2 = devices.createNestedObject();
  devices_2[outlet] = 3;
  devices_2[state] = outlet3State;

  JsonObject devices_3 = devices.createNestedObject();
  devices_3[outlet] = 4;
  devices_3[state] = outlet4State;

  serializeJson(doc, output);
  if (!output.isEmpty())
  {
    client.publish(sensorTopic, output.c_str());
    output = "";
    blink();
  }
}

/**
 * @brief makes built in led blink TEST PURPOSES
 */
void blink()
{
  turnOnBuiltInLED();
  delay(200);
  turnOffBuiltInLED();
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
  delay(3000); // delay applied for deboucing and to force long press for reset
  bool isStillPressed = !digitalRead(resetButton);
  if (shouldResetEsp && isStillPressed)
  {
    reset();
  }
}

void reset()
{
  blink();
  Serial.println("Terminating processes and resetting...");
  wifi.resetSettings();
  clearFilesystem();
  delay(500);
  setupWifi();
}

void IRAM_ATTR resetCallback()
{
  Serial.println("Reset activated!");
  shouldResetEsp = true;
}

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(9600);
  // network setup
  setupWifi();
  client.setServer(MQTT_BROKER, MQTT_BROKER_PORT);
  client.setCallback(mqttCallback);
  // hardware setup
  pinMode(outlet1, OUTPUT);
  pinMode(outlet2, OUTPUT);
  pinMode(outlet3, OUTPUT);
  pinMode(outlet4, OUTPUT);
  pinMode(resetButton, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(resetButton), resetCallback, FALLING);

  digitalWrite(outlet1, HIGH);
  digitalWrite(outlet2, HIGH);
  digitalWrite(outlet3, HIGH);
  digitalWrite(outlet4, HIGH);
  turnOffBuiltInLED();
  delay(2000);
}

void loop()
{
  // put your main code here, to run repeatedly:

  if (!client.connected())
  {
    reconnect();
  }
  client.loop();
  if (currentPayload != "")
    checkPayload();
  checkResetButton();
}
