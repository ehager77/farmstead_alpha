/*
  Offline-first data logger with VH400 Sensor from Vegetronix. (I2C)
  Reports current and saved substrate moisture & temperature data to Ubidots when connected.
*/
#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "FS.h"
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DHTesp.h>  //DHT11 Library for ESP
#include <Wire.h> //I2C library
#include <OneWire.h>
#include <RtcDS3231.h> //RTC library
#include "Ubidots.h"
#include <DallasTemperature.h>

WiFiServer server(80);
DNSServer dns;

// Connect to the Ubidots
const char* UBIDOTS_TOKEN = "YOUR_TOKEN";
Ubidots ubidots(UBIDOTS_TOKEN, UBI_HTTP);

//Declare real time clock object
RtcDS3231<TwoWire> rtcObject(Wire);
RtcDateTime now;

// GPIO where the DS18B20 is connected to
const int oneWireBus = 2;

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);

// Pass our oneWire reference to Dallas Temperature sensor
DallasTemperature soilTempSensor(&oneWire);

//  ****************  CREATE BUFFER TO STORE BINARY DATA ************************
#define INT_STR_SIZE 16
char buffer[INT_STR_SIZE];
char charBuf[50];

// Device ID & Type variables
String chipNumber = String(ESP.getChipId());
String deviceType = "Ground";
String deviceName = deviceType + "-" + chipNumber;
String deviceIP;

//Soil Moisture Variables
int analogPin = A0;
int analogValue;  // value read from the pot
float sensorVoltage;
float VWC;
int saturationPoint = 55;
int fieldCapacity = 35;
int permanentWiltingPoint = 18;
int depletion;
int percentSaturation;
int soilTempC;
int soilTempF;

// Sleep Settings
float millisecondsInSecond = 1000000;
float secondsInMinute = 60;
float sleepMinutes = 10;

void readVH400() {
  // This function returns Volumetric Water Content by converting the analogPin value to voltage
  // and then converting voltage to VWC using the piecewise regressions provided by the manufacturer
  // at http://www.vegetronix.com/Products/V...se-Curve.phtml

  // Read value and convert to voltage
  analogValue = analogRead(analogPin);
  sensorVoltage = analogValue * (3.0 / 1023.0);

  // Calculate VWC
  if (sensorVoltage <= 1.1) {
    VWC = 10 * sensorVoltage - 1;
  } else if (sensorVoltage > 1.1 && sensorVoltage <= 1.3) {
    VWC = 25 * sensorVoltage - 17.5;
  } else if (sensorVoltage > 1.3 && sensorVoltage <= 1.82) {
    VWC = 48.08 * sensorVoltage - 47.5;
  } else if (sensorVoltage > 1.82) {
    VWC = 26.32 * sensorVoltage - 7.89;
  }

  //  Calculate Depletion % below field capacity to trigger irrigation alerts
  depletion = ((VWC - fieldCapacity) / fieldCapacity) * 100;
  if (depletion > 0) {
    depletion = 0;
  } else if (depletion < 0) {
    depletion = depletion * -1;
  }

  percentSaturation = (VWC / saturationPoint) * 100;

  Serial.println("Analogue Value: ");
  Serial.println(analogValue);
  Serial.println("Voltage: ");
  Serial.println(sensorVoltage);
  Serial.println("VWC: ");
  Serial.println(VWC);
  Serial.println("% Saturation: ");
  Serial.println(percentSaturation);
  Serial.println("Field Capacity: ");
  Serial.println(fieldCapacity);
  Serial.println("Depletion from FC (%): ");
  Serial.println(depletion);
}

void readDS18B20() {
  // Start the DS18B20 Soil Temperature sensor
  soilTempSensor.begin();
  soilTempSensor.requestTemperatures();
  soilTempC = soilTempSensor.getTempCByIndex(0);
  soilTempF = soilTempSensor.getTempFByIndex(0);
  Serial.println("Soil Temperature is... ");
  Serial.println("Celsius: ");
  Serial.println(soilTempC);
  Serial.println("Farenheit: ");
  Serial.println(soilTempF);
  delay(3000);
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
  //  Read Soil Moisture Sensor
  readVH400();
  //  Read Soil Temp Sensor
  readDS18B20();
  //Get UNIX timestamp from RTC
  getCurrentTime();

  const size_t CAPACITY = JSON_OBJECT_SIZE(16) ;
  StaticJsonDocument<CAPACITY> obj;

  JsonObject sensorValues = obj.to<JsonObject>();
  sensorValues["substrate_vwc"] = VWC;
  sensorValues["saturation_%"] = percentSaturation;
  sensorValues["fc_depletion_%"] = depletion;
  sensorValues["substrate_temp_c"] = soilTempC;
  sensorValues["substrate_temp_f"] = soilTempF;
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
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, line);

    if (err) {
      Serial.print(F("deserializeJson() failed with code "));
      Serial.println(err.c_str());
    }

    int offl_VWC = doc["substrate_vwc"];
    int saturationLevel = doc["saturation_%"];
    int fcDepletion = doc["fc_depletion_%"];
    int offl_soilTempC = doc["substrate_temp_c"];
    int offl_soilTempF = doc["substrate_temp_f"];
    long timestamp_seconds = doc["timestamp"];
    int timestamp_milliseconds = 0;

    ubidots.add("substrate_vwc", offl_VWC, NULL, timestamp_seconds, timestamp_milliseconds);
    ubidots.add("saturation_%", saturationLevel, NULL, timestamp_seconds, timestamp_milliseconds);
    ubidots.add("fc_depletion_%", fcDepletion, NULL, timestamp_seconds, timestamp_milliseconds);
    ubidots.add("substrate_temp_c", offl_soilTempC, NULL, timestamp_seconds, timestamp_milliseconds);
    ubidots.add("substrate_temp_f", offl_soilTempF, NULL, timestamp_seconds, timestamp_milliseconds);

    bool bufferSent = false;
    bufferSent = ubidots.send();

    if (!bufferSent) {
      // Do something if values were sent properly
      Serial.println("Error sending Offline Values!");
      return;
    }
    Serial.println("Offline Values sent by the device!");
    delay(100);
  }

  offlineValues.close();
  delay(3000);
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
  //Read Sensors
  readVH400();
  readDS18B20();
  //Get UNIX timestamp from RTC
  getCurrentTime();

  long timestamp_seconds = int(now.Epoch32Time());
  int timestamp_milliseconds = 0;

  Serial.println("Current Time as UNIX timestamp: ");
  Serial.println(now.Epoch32Time());

  ubidots.add("saturation_point", saturationPoint, NULL, timestamp_seconds, timestamp_milliseconds);
  ubidots.add("field_capacity", fieldCapacity, NULL, timestamp_seconds, timestamp_milliseconds);
  ubidots.add("permanent_wilting_point", permanentWiltingPoint, NULL, timestamp_seconds, timestamp_milliseconds);
  ubidots.add("substrate_vwc", VWC, NULL, timestamp_seconds, timestamp_milliseconds);
  ubidots.add("saturation_%", percentSaturation, NULL, timestamp_seconds, timestamp_milliseconds);
  ubidots.add("fc_depletion_%", depletion, NULL, timestamp_seconds, timestamp_milliseconds);
  ubidots.add("substrate_temp_c", soilTempC, NULL, timestamp_seconds, timestamp_milliseconds);
  ubidots.add("substrate_temp_f", soilTempF, NULL, timestamp_seconds, timestamp_milliseconds);

  bool bufferSent = false;
  bufferSent = ubidots.send();
  delay(1000);

  if (!bufferSent) {
    // Do something if values were sent properly
    Serial.println("Error sending Online Values!");
    return;
  }
  Serial.println("Online Values sent by the device!");
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
    Serial.println("Failed to connect and hit timeout...");
    save2FS();

  } else if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to Network...");
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());

  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  // On connection, read from file system and publish offline readings to mqtt
  publishReadingsOffline();
  delay(2000);

  //Delete offline readings file after publishing
  //removeFromFS();

  //  Publish Most Recent Readings & other sensor info directly when connected to WiFi.
  publishReadingsOnline();
  delay(2000);

  Serial.print("Going to sleep for 10 minutes...");
  ESP.deepSleep(millisecondsInSecond * secondsInMinute * sleepMinutes);
}

void loop() {
  //   if (!client.connected()) {
  //       reconnect();
  //   }
  //    client.loop();
}
