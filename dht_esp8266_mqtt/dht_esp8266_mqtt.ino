/*
  WiFi DHT11 humidity, temperature & pressure sensor (I2C)
  Reports current weather data  to Mosquitto MQTT running on my Google Compute Engine VM Instance
*/
#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "FS.h"
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DHTesp.h>  //DHT11 Library for ESP
#include <WifiLocation.h>
#include <Wire.h> //I2C library
#include <RtcDS3231.h> //RTC library

ESP8266WebServer server(80);
DNSServer dns;

#define DHTpin 14    //D5 of NodeMCU is GPIO14

// Setup Google GeoLocation API...
const char* location_api_key = "AIzaSyAgDLq8J7eQEyAXCCL4sR8BUUMhXE4uPSo";
location_t loc;
WifiLocation location(location_api_key);

// Connect to the WiFi
const char* mqtt_server = "YOUR_IP";
const int mqtt_port = 1883;

//Declare real time clock object
RtcDS3231<TwoWire> rtcObject(Wire);
RtcDateTime now;

// Initialize DHT
DHTesp dht;
double humidity, temperature;

// Ititialize PubSubClient
WiFiClient espClient;
PubSubClient client(espClient);

// Device ID & Type variables
String chipNumber = String(ESP.getChipId());
String deviceType = "air";
String deviceName = deviceType + "-" + chipNumber;
String deviceIP;

// Sleep Settings
float millisecondsInSecond = 1000000;
float secondsInMinute = 60;
float sleepMinutes = .5;

//  ****************  CREATE BUFFER TO STORE BINARY DATA ************************
char charBuf[50];

boolean reconnect() {// **********************************************************
  const char* clientID = deviceName.c_str();
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print (F("Contacting MQTT server..."));
    // Attempt to connect
    if (client.connect(clientID)) {     //assign a "client name".  Each device must have a unique name
      Serial.println (F("Connected to broker."));
      Serial.println(clientID);

      // ... SUBSCRIBE TO TOPICS
      // chipNumber.toCharArray(charBuf, 50);
      // deviceType.toCharArray(charBuf, 50);
      // String locationIdStr = "device/" + chipNumber + "/status";
      // const char*locationIdTopic = locationIdStr.c_str();
      // client.subscribe(locationIdTopic, 1);

      return client.connected();
    } else {
      Serial.print (F("Failed to connect to broker... "));
      // Wait 3 seconds before retrying
      delay(500);
      return 0;
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<512> doc;
  deserializeJson(doc, payload, length);

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void dhtStart() {
  dht.setup(DHTpin, DHTesp::DHT11); //for DHT11 Connect DHT sensor to GPIO 15
  //dht.setup(DHTpin, DHTesp::DHT22); //for DHT22 Connect DHT sensor to GPIO 17
  if (!dht.getTemperature()) {
    Serial.println("Could not find a valid sensor, check wiring!");
    while (1);
  } else {
    Serial.println("DHT11 Sensor Detected...");
  }
}

void getCurrentTime(){
  //--------RTC SETUP ------------
  // if you are using ESP-01 then uncomment the line below to reset the pins to
  // the available pins for SDA, SCL
  // Wire.begin(0, 2); // due to limited pins, use pin 0 and 2 for SDA, SCL

  rtcObject.Begin();

  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);

  if (!rtcObject.IsDateTimeValid())
  {
    if (rtcObject.LastError() != 0)
    {
      // we have a communications error
      // see https://www.arduino.cc/en/Reference/WireEndTransmission for
      // what the number means
      Serial.print("RTC communications error = ");
      Serial.println(rtcObject.LastError());
    }
    else
    {
      // Common Causes:
      //    1) first time you ran and the device wasn't running yet
      //    2) the battery on the device is low or even missing

      Serial.println("RTC lost confidence in the DateTime!");

      // following line sets the RTC to the date & time this sketch was compiled
      // it will also reset the valid flag internally unless the Rtc device is
      // having an issue

      rtcObject.SetDateTime(compiled);
    }
  }

  if (!rtcObject.GetIsRunning())
  {
    Serial.println("RTC was not actively running, starting now");
    rtcObject.SetIsRunning(true);
  }

  now = rtcObject.GetDateTime();
  if (now < compiled)
  {
    Serial.println("RTC is older than compile time!  Updating DateTime...");
    rtcObject.SetDateTime(compiled);
    Serial.print("Compiled: ");
    Serial.println(__DATE__);
    Serial.println(__TIME__);
  }
  else if (now > compiled)
  {
    Serial.println("RTC is newer than compile time. (this is expected)");
  }
  else if (now == compiled)
  {
    Serial.println("RTC is the same as compile time! (not expected but all is fine)");
  }

  // never assume the Rtc was last configured by you, so
  // just clear them to your needed state
  rtcObject.Enable32kHzPin(false);
  rtcObject.SetSquareWavePin(DS3231SquareWavePin_ModeNone);
}

#define countof(a) (sizeof(a) / sizeof(a[0]))

void printDateTime(const RtcDateTime& dt)
{
    char datestring[20];

    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
            dt.Month(),
            dt.Day(),
            dt.Year(),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
    Serial.print(datestring);
}

void save2FS() {
//  Initialize Sensor
  dhtStart();
  //Get UNIX timestamp from RTC
  getCurrentTime();
  
  const size_t CAPACITY = JSON_OBJECT_SIZE(16) ;
  StaticJsonDocument<CAPACITY> obj;

  JsonObject sensorValues = obj.to<JsonObject>();
  sensorValues["air_temp_c"] = int(dht.getTemperature());
  sensorValues["air_temp_f"] = int(dht.getTemperatureF());
  sensorValues["air_humidity"] = int(dht.getHumidity());
  sensorValues["timestamp"] = int(now.Epoch32Time());

  char buffer[384];
  Serial.println("Object to be appended...");
  serializeJson(obj, buffer);
  Serial.println(buffer);

  bool spiffs = SPIFFS.begin();
  if (spiffs) {
    Serial.println("File system mounted.");
  } else {
    Serial.println("File sytem mount failed.");
    return;
  }

  File fileToAppend = SPIFFS.open("/offline-values.txt", "a");

  if (!fileToAppend) {
    Serial.print("Error opening file for appending...");
    return;
  }

  if (fileToAppend.println(buffer)) {
    Serial.println("File content was appended ...");

    int fileSize = fileToAppend.size();

    if (fileSize > 0) {
      Serial.println("File write success! ");
      Serial.println("File Byte Size: ");
      Serial.print(fileSize);
      Serial.println();
    } else if (fileSize > 500000) {
      Serial.println("File size is too large.");
    } else {
      Serial.println("File append failed");
    }

    fileToAppend.close();
  }

  //reset and try again, or maybe put it to deep sleep
  Serial.println();
  Serial.println("Network connection not made. Last values saved to local FS.  Going to sleep for 15 minutes...");
  ESP.deepSleep(millisecondsInSecond * secondsInMinute * sleepMinutes);
  delay(50);

}

void publishReadingsOffline() {
  bool spiffs = SPIFFS.begin();

  if (spiffs) {
    Serial.println("File system mounted.");
  } else {
    Serial.println("File sytem mount failed.");
  }

  File offlineValues = SPIFFS.open("/offline-values.txt", "r");

  if (!offlineValues) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.println("File Content:");

  while (offlineValues.available()) {

    String line = offlineValues.readStringUntil('\n');
    StaticJsonDocument<384> doc;
    deserializeJson(doc, line);

    char output[384];
    serializeJson(doc, output);

    String readingsTopicStr = "device/" + chipNumber + "/readings/offline";
    const char*readingsTopic = readingsTopicStr.c_str();

    Serial.println("Payload: ");
    Serial.println(output);

    if (!client.publish(readingsTopic, output)) {
      Serial.println("Error publishing offline readings...");
    }
    
    Serial.println("Offline Readings published successfully!");
    delay(2000);
  }

  offlineValues.close();
  delay(2000);
  
  //Delete offline readings file after publishing
  removeFromFS();
}

void removeFromFS() {
  Serial.println("Deleting stored readings...");

  bool spiffs = SPIFFS.begin();
  if (spiffs) {
    Serial.println("File system mounted.");
  } else {
    Serial.println("File sytem mount failed.");
  }

  if (SPIFFS.remove("/offline-values.txt")) {
    Serial.println("Offline values deleted successfully!");
  } else {
    Serial.println("Offline Values not removed!");
  }
}

void publishReadingsOnline() {
  //Initialize Sensor
  dhtStart();
  //Get UNIX timestamp from RTC  
  getCurrentTime();
  
  //  Publish Sensor Output Object
  const size_t CAPACITY = JSON_OBJECT_SIZE(16);
  StaticJsonDocument<CAPACITY> sensorValues;

  sensorValues["air_temp_c"] = int(dht.getTemperature());
  sensorValues["air_temp_f"] = int(dht.getTemperatureF());
  sensorValues["air_humidity"] = int(dht.getHumidity());
  sensorValues["timestamp"] = long(now.Epoch32Time());

  char buffer[384];
  Serial.println("Object to be published...");
  serializeJson(sensorValues, buffer);
  Serial.println(buffer);

  String readingsTopicStr = "device/" + chipNumber + "/readings/online";
  const char*readingsTopic = readingsTopicStr.c_str();

  Serial.println("Online topic: ");
  Serial.println(readingsTopic);

  if (!client.publish(readingsTopic, buffer)) {
    Serial.print("Error publishing online readings...");;
  }
  delay(1000);

  Serial.print("Online Readings published successfully!");

}

void publishLocation() {
  const size_t CAPACITY = JSON_OBJECT_SIZE(8) ;
  StaticJsonDocument<CAPACITY> doc;

  JsonObject obj = doc.to<JsonObject>();

  JsonObject location = obj.createNestedObject("location");
  location["latitude"] = loc.lat;
  location["longitude"] = loc.lon;

  char buffer[384];
  Serial.println("Location Object: ");
  serializeJson(location, buffer);
  Serial.println(buffer);

  String locationTopicStr = "device/" + chipNumber + "/location";
  const char*locationTopic = locationTopicStr.c_str();

  if (!client.publish(locationTopic, buffer)) {
    Serial.print("Error publishing location...");;
  }
  delay(1000);

  Serial.print("Device Location published successfully!");
}

void publishDeviceInfo() {
  const size_t CAPACITY = JSON_OBJECT_SIZE(6) ;
  StaticJsonDocument<CAPACITY> deviceInfo;

  deviceInfo["device_id"] = chipNumber;
  deviceInfo["device_type"] = deviceType;
  deviceInfo["local_IP"] = WiFi.localIP().toString();

  char buffer[384];
  Serial.println("Device Info Object: ");
  serializeJson(deviceInfo, buffer);
  Serial.println(buffer);

  String deviceInfoTopicStr = "device/" + chipNumber + "/info";
  const char*deviceInfoTopic = deviceInfoTopicStr.c_str();

  if (!client.publish(deviceInfoTopic, buffer)) {
    Serial.print("Error publishing device info...");;
  }
  delay(1000);

  Serial.print("Device Info published successfully!");
}

void setup() {
  Serial.begin(115200);
  Serial.println("Waking up...");

  //   ***********************************************************************************

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //reset saved settings
  wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(20);

  //set custom ip for portal
  wifiManager.setAPStaticIPConfig(IPAddress(10, 0, 1, 1), IPAddress(10, 0, 1, 1), IPAddress(255, 255, 255, 0));

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  //wifiManager.autoConnect();

  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    save2FS();

  } else if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to Network...");
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());

  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  // Geolocate Device
  Serial.println("Fetching sensor's geolocation...");
  loc = location.getGeoFromWiFi();
  Serial.println(location.getSurroundingWiFiJson());
  Serial.println("Accuracy (m): " + String(loc.accuracy));

  //  Establish Google Cloud VM MQTT Connection
  {
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    if (!client.connected()) {
      reconnect();
    }
  }

  //  Get ESP Chip id and publish as deviceId
  Serial.print("Hi! My name is " + deviceType + "-" + chipNumber);
  Serial.println();
  chipNumber.toCharArray(charBuf, 50);
  String deviceIdTopicStr = "device/";
  const char*deviceIdTopic = deviceIdTopicStr.c_str();
  if (!client.publish(deviceIdTopic, charBuf)) {
    Serial.println("Error publishing device id...");
  }

  //  Publish Most Recent Readings & other sensor info directly when connected to WiFi.
  publishReadingsOnline();
  publishLocation();
  publishDeviceInfo();
  delay(1000);
  // On connection, read from file system and publish offline readings to mqtt
  publishReadingsOffline();
  delay(3000);

  Serial.print("Going to sleep for 15 minutes...");
  ESP.deepSleep(millisecondsInSecond * secondsInMinute * sleepMinutes);
}

void loop() {
  //   if (!client.connected()) {
  //       reconnect();
  //   }
  //    client.loop();
}
