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
#include <DHT.h>  //DHT11 Library for ESP
#include <Wire.h> //I2C library
#include <RtcDS3231.h> //RTC library
#include "Ubidots.h"

ESP8266WebServer server(80);
DNSServer dns;

// Connect to the Ubidots
const char* UBIDOTS_TOKEN = "BBFF-3z2OHVkOs8PsT5iHrdqldEq5zLOfv3";
Ubidots ubidots(UBIDOTS_TOKEN, UBI_HTTP);

//Declare real time clock object
RtcDS3231<TwoWire> rtcObject(Wire);
RtcDateTime now;

// Initialize DHT
// DHT Sensor
#define DHTPIN 14     // what pin we're connected to
#define DHTTYPE DHT11   // DHT11 
               
// Initialize DHT sensor.
DHT dht(DHTPIN, DHTTYPE);
double humidity, temperature;

// Device ID & Type variables
String chipNumber = String(ESP.getChipId());
String deviceType = "temp_rh";
String deviceName = deviceType + "-" + chipNumber;
String deviceIP;

// Sleep Settings
float millisecondsInSecond = 1000000;
float secondsInMinute = 60;
float sleepMinutes = 20;

//  ****************  CREATE BUFFER TO STORE BINARY DATA ************************
char charBuf[50];

void dhtStart() {
  pinMode(DHTPIN, INPUT);
  dht.begin();
  //dht.setup(DHTpin, DHTesp::DHT11); //for DHT11 Connect DHT sensor to GPIO 15
  //dht.setup(DHTpin, DHTesp::DHT22); //for DHT22 Connect DHT sensor to GPIO 17
  if (!dht.readTemperature()) {
    Serial.println("Could not find a valid sensor, check wiring!");
    while (1);
  } else {
    Serial.println("DHT11 Sensor Detected...");
  }
}

void getCurrentTime() {
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

  const size_t CAPACITY = JSON_OBJECT_SIZE(8) ;
  StaticJsonDocument<CAPACITY> obj;

  JsonObject sensorValues = obj.to<JsonObject>();
  sensorValues["air_temp_c"] = int(dht.readTemperature());
  sensorValues["air_humidity"] = int(dht.readHumidity());
  sensorValues["timestamp"] = int(now.Epoch32Time());

  char buffer[256];
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
  Serial.println("Network connection not made. Last values saved to local FS.  Going to sleep for 10 minutes...");
  ESP.deepSleep(millisecondsInSecond * secondsInMinute * sleepMinutes);
  delay(50);

}

void publishOfflineReadings() {
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
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, line);

    if (err) {
      Serial.print(F("deserializeJson() failed with code "));
      Serial.println(err.c_str());
    }

    int airTempC = doc["air_temp_c"];
    int airHumidity = doc["air_humidity"];
    long timestamp_seconds = doc["timestamp"];
    int timestamp_milliseconds = 0;

    ubidots.add("temperature_c", airTempC, NULL, timestamp_seconds, timestamp_milliseconds);
    ubidots.add("humidity", airHumidity, NULL, timestamp_seconds, timestamp_milliseconds);

    bool bufferSent = false;
    bufferSent = ubidots.send();

    if (!bufferSent) {
      // Do something if values were sent properly
      Serial.println("Error sending Offline Values!");
    }
    Serial.println("Offline Values sent by the device!");
    delay(50);
  }

  offlineValues.close();
  delay(3000);
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
    Serial.print("Offline Values not removed!");
  }
}

void publishOnlineReadings() {
  //Initialize Sensor
  dhtStart();
  //Get UNIX timestamp from RTC
  getCurrentTime();

  //  Publish Sensor Output
  int airTempC = int(dht.readTemperature());
  int airHumidity = int(dht.readHumidity());
  long timestamp_seconds = int(now.Epoch32Time());
  int timestamp_milliseconds = 0;

  ubidots.add("temperature_c", airTempC, NULL, timestamp_seconds, timestamp_milliseconds);
  ubidots.add("humidity", airHumidity, NULL, timestamp_seconds, timestamp_milliseconds);

  bool bufferSent = false;
  bufferSent = ubidots.send();

  if (!bufferSent) {
    // Do something if values were sent properly
    Serial.println("Error sending Online Values!");
  }

  Serial.println("Online Values successfully sent by the device!");
  Serial.println("Temp C: ");
  Serial.println(airTempC);
  Serial.println("Humidity: ");
  Serial.println(airHumidity);
  Serial.println("Timestamp: ");
  Serial.println(timestamp_seconds);
  delay(3000);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Waking up...");

  //   ***********************************************************************************

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //reset saved settings
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  wifiManager.setMinimumSignalQuality(60);

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(20);

  //set custom ip for portal
  wifiManager.setAPStaticIPConfig(IPAddress(WiFi.localIP()), IPAddress(10, 0, 1, 1), IPAddress(255, 255, 255, 0));

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

  // On connection, read from file system and publish offline readings to mqtt
  publishOfflineReadings();

  //Delete offline readings file after publishing
  removeFromFS();

  //  Publish Most Recent Readings & other sensor info directly when connected to WiFi.
  publishOnlineReadings();
  delay(2000);
  
  Serial.print("Going to sleep for 20 minutes...");
  ESP.deepSleep(millisecondsInSecond * secondsInMinute * sleepMinutes);
}

void loop() {
  //   if (!client.connected()) {
  //       reconnect();
  //   }
  //    client.loop();
}
