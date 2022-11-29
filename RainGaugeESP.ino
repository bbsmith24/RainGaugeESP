/*
  Brian Smith
  November 2022
  Copyright (c) 2022 Brian B Smith. All rights reserved.
  IOT Rain Gauge
  uses MQTT to update OpenHAB (or any other MQTT server)
  push info to Weather Underground  

  based on ESP32_Credentials
  Brian B Smith
  November 2022
  brianbsmith.com
  info@brianbsmith.com
  
  Copyright (c) 2022 Brian B Smith. All rights reserved.

  This is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  This does nothing interesting on its own, except to get credentials and timezone offset (if needed) then connect to 
  WiFi and output the time every minute. Use as a basis for other applications that need WiFi
  
  'ESP32_Credentials' is based on "ESP32: Create a Wi-Fi Manager" tutorial by Rui Santos at Random Nerds Tutorials
  found here: https://randomnerdtutorials.com/esp32-wi-fi-manager-asyncwebserver/
  They have lots of useful tutorials here, well worth a visit!

  changes from original:
    just get wifi credentials, no web server page after connection
    refactored for clarity/ease of use (at least it's more clear to me...)
    added 'clear credentials' to allow for changing between wifi networks (reset, reboots to credentials page, reboots to connection)
    added 'define VERBOSE' for debug statements
    use defines for constant values
    allow undefined local ip, allows router to set ip from DNS
    get time from NTP server and set internal RTC
    added local time offset from GMT and daylight savings time offset to credentials page - hours now, could be enhanced with a combo box for timezone

*/
//#define VERBOSE                 // more output for debugging
#define POWER_STATE_REPORTING  // use INA260 for reporting solar v/mA and microcontroller load V/mA to monitor charging state
// wait between wifi and MQTT server connect attempts
#define RECONNECT_DELAY    5000
// wait between sensor updates
#define CHECK_STATE_DELAY 60000
#define FORMAT_LITTLEFS_IF_FAILED true
// for using alternate serial ports
//#define ALT_SERIAL
#define SERIALX Serial
//#define SERIALX Serial2
#define RXD2 16
#define TXD2 17

// for Sparkfun ESP32 Thing Plus
//#define RAINGAUGE_PIN 5  
// for Sparkfun ESP32 Thing
#define RAINGAUGE_PIN 14 

#include <Arduino.h>            // 
#include <WiFi.h>               // 
#include <ESPAsyncWebServer.h>  // https://github.com/me-no-dev/ESPAsyncWebServer
#include <AsyncTCP.h>           // https://github.com/me-no-dev/AsyncTCP
#include "FS.h"                 // 
#include "LITTLEFS.h"           // LittleFS_esp32 by loral from Arduino library manager
#include <time.h>               // for NTP time
#include <ESP32Time.h>          // for RTC time https://github.com/fbiego/ESP32Time
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>
#ifdef POWER_STATE_REPORTING
#include <Adafruit_INA260.h>  // voltage/current
#endif

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

//
// global variables for ESP32 credentials
// using #define instead of const saves some program space
#define WIFI_WAIT 10000    // interval between attempts to connect to wifi
// Search for parameter in HTTP POST request
#define PARAM_INPUT_1  "ssid"
#define PARAM_INPUT_2  "pass"
#define PARAM_INPUT_3  "ip"
#define PARAM_INPUT_4  "gateway"
#define PARAM_INPUT_5  "timezone"
#define PARAM_INPUT_6  "dst"
//Variables to save values from HTML form
String ssid;
String pass;
String ip;
String gateway;
String tz;
String dst;
// File paths to save input values permanently
#define ssidPath     "/ssid.txt"
#define passPath     "/pass.txt"
#define ipPath       "/ip.txt"
#define gatewayPath  "/gate.txt"
#define tzPath       "/tz.txt"
#define dstPath      "/dst.txt"

// ESP32 IP address (use DNS if blank)
IPAddress localIP;
// local Gateway IP address and subnet mask
IPAddress localGateway;
IPAddress subnet(255, 255, 0, 0);
//
// global variables for time
//
// NTP Server Details
#define ntpServer "pool.ntp.org"

long  gmtOffset_sec = 0;
int   daylightOffset_sec = 0;
ESP32Time rtc(0);

int dayNum;
int monthNum;
int yearNum;
int hourNum;
int minNum;
int secondNum;

int lastDayNum = -1;
int lastHourNum = -1;
int lastMinNum = -1;

char weekDay[10];
char dayMonth[4];
char monthName[5];
char year[6];
char hour[4];
char minute[4];
char second[4];
char localTimeStr[256];
char connectDateTime[256];
bool connectDateTimeSet = false;
bool rtcTimeSet = false;
// Timer variables
unsigned long previousMillis = 0;
unsigned long timeCheckInterval = 30000;
unsigned long lastTimeCheck = 0;

char wifiState[256];
bool wifiConnected = false;
//
// MQTT info
//
#define PARAM_INPUT_7   "mqtt_serverIP"
#define PARAM_INPUT_8   "mqtt_port"
#define PARAM_INPUT_9   "mqtt_user"
#define PARAM_INPUT_10  "mqtt_password"

#define mqtt_serverPath    "/mqttIP.txt"
#define mqtt_portPath      "/mqttPrt.txt"
#define mqtt_userPath      "/mqttUse.txt"
#define mqtt_passwordPath  "/mqttPas.txt"

void MQTT_Callback(char* topic, byte* payload, unsigned int length);
String mqtt_server = "0.0.0.0";
String mqtt_port  = "0";
String mqtt_user;
String mqtt_password;
IPAddress mqttserverIP(0, 0, 0, 0);
int  mqtt_portVal = 0;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
String mqttClientID = "MQTTRainGauge_";   // local name, must be unique on MQTT server so append a random string at the end
// subscribed (listening) topic(s)
#define MAX_SUBSCRIBE        4
String subscribed_topic[MAX_SUBSCRIBE] =  { "Rain_Gauge/WU_RainGaugeWUReport",      // switch weather underground reporting on/off
                                            "Rain_Gauge/MQTT_RainGaugeMQTTReport",    // switch MQTT reporting on/off
                                            "Rain_Gauge/RainGauge_ZeroValues",    // zero rain gauge values
                                            "Rain_Gauge/RainGauge_ResetCredentials"              // clear credentials and restart
                                          };

// published (send to server) topic(s)
#ifdef POWER_STATE_REPORTING
#define MAX_PUBLISH 17
#else
#define MAX_PUBLISH 5
#endif
String published_topic[MAX_PUBLISH] = {  "Rain_Gauge/Date",
                                         "Rain_Gauge/pastQuarterHour",
                                         "Rain_Gauge/pastHour",
                                         "Rain_Gauge/past24Hours",
                                         "Rain_Gauge"
  #ifdef POWER_STATE_REPORTING  
                                        ,"RainGauge/ControllerVAve",
                                         "RainGauge/ControllerVMin", 
                                         "RainGauge/ControllerVMax", 
                                         "RainGauge/ControllerAAve", 
                                         "RainGauge/ControllerAMin", 
                                         "RainGauge/ControllerAMax", 
                                         "RainGauge/SolarVAve",
                                         "RainGauge/SolarVMin",
                                         "RainGauge/SolarVMax",
                                         "RainGauge/SolarAAve",
                                         "RainGauge/SolarAMin",
                                         "RainGauge/SolarAMax",
  #endif
                                      };

String published_payload[MAX_PUBLISH];
char payloadStr[512];
char mqttState[256];
bool mqtt_Report = true;
//
// Weather underground credentials
//
#define host "weatherstation.wunderground.com"
#define PARAM_INPUT_11  "wu_ID"
#define PARAM_INPUT_12  "wu_Key"
#define wu_IDPath  "/wuID.txt"
#define wu_KeyPath  "/wuKey.txt"

String wUndergroundID;
String wUndergroundKey;  
char wundergroundState[256];
bool wUnderground_Report = false;

#ifdef POWER_STATE_REPORTING
#define INA260_COUNT 2
#define INA260_INTERVAL 10000
Adafruit_INA260 sensors[INA260_COUNT];
int i2cAddresses[INA260_COUNT] = {0x40, 0x41};
String sensorDesc[INA260_COUNT] = {"Micro", "Solar" };
float measuredV_ave[INA260_COUNT];
float measuredV_min[INA260_COUNT];
float measuredV_max[INA260_COUNT];
float measuredA_ave[INA260_COUNT];
float measuredA_min[INA260_COUNT];
float measuredA_max[INA260_COUNT];
int ina260MeasureCount = 0;
unsigned long ina260LastMeasure = 0;
#endif

// rain information
#define MIN_PER_DAY 1440
#define LAST_X_DAYS 28
#define LAST_X_HOURS 24
#define INCHES_PER_CLICK 0.011F
//#define INTERVAL_MS           60000 //    1 minute interval
#define INTERVAL_MS            150000 //  2.5 minute interval
//#define INTERVAL_MS          300000 //    5 minute interval
//#define MAX_UPDATE_INTERVAL_MS 600000 //   10 minute max between updates
//#define MAX_UPDATE_INTERVAL_MS 900000 // 15 minute max between updates
#define MAX_UPDATE_INTERVAL_MS 600000 // 10 minute max between updates
// rain for current minute, stored for 
int rainByMinuteIdx = 0;                // current index of by minute rolling buffer
unsigned int rainByMinute[MIN_PER_DAY]; // circular buffer for every minute value for last 24 hours
// rain by hour for last X hours
int rainByHourIdx = 0;
unsigned int rainByHour[LAST_X_HOURS];  // circular buffer for every hour value for last 24 hours
// rain day day for last X days
int rainByDayIdx = 0;
unsigned int rainByDay[LAST_X_DAYS];    // circular buffer for daily total last 28 days
// rain counts for last minute, hour, day updated each minute
int rainForLastMinute = 0;
int rainForLastQuarter = 0;
int rainForLastHour = 0;
int rainForLastDay = 0;
int totalTicks = 0;
// rain inches current, last 15, last hour, last day 
float currentRainInches = 0.0;
float quarterHourRainInches = 0.0;
float hourRainInches = 0.0;
float dayRainInches = 0.0;
float priorQuarterHourRainInches = 0.0;
float priorHourRainInches = 0.0;
float priorDayRainInches = 0.0;

bool neverUpdated = true;
unsigned long lastUpdate = 0;
volatile bool clicked = false;  // true after rain gauge triggers hall sensor
//
// connect to wifi. if credentials not present/invalid, boot to access point and present page to get them
//
void setup()
{
  #ifdef VERBOSE
  // Serial port for debugging purposes
  #ifndef ALT_SERIAL
  SERIALX.begin(115200);
  #else
  SERIALX.begin(115200, SERIAL_8N1, RXD2, TXD2);
  #endif
  #endif

  #ifdef VERBOSE
  SERIALX.println("");
  SERIALX.println("BBS Nov 2022");
  SERIALX.println("IOT Rain gauge");
  SERIALX.println("=======================");
  delay(1000);
  #endif
  // set CPU freq to 80MHz, disable bluetooth  to save power
  #ifdef VERBOSE
  int freq = getCpuFrequencyMhz();
  SERIALX.printf("Default Freq\n");
  SERIALX.printf("CPU Freq = %dMhz\n", freq);
  freq = getXtalFrequencyMhz();
  SERIALX.printf("XTAL Freq = %dMhz\n", freq);
  freq = getApbFrequency();
  SERIALX.printf("APB Freq = %dHz\n", freq);
  #endif
  setCpuFrequencyMhz(80);
  #ifdef VERBOSE
  SERIALX.printf("Updated Freq\n");
  freq = getCpuFrequencyMhz();
  SERIALX.printf("CPU Freq = %dMhz\n", freq);
  freq = getXtalFrequencyMhz();
  SERIALX.printf("XTAL Freq = %dMhz\n", freq);
  freq = getApbFrequency();
  SERIALX.printf("APB Freq = %dHz\n", freq);
  #endif
  btStop();
  #ifdef VERBOSE
  SERIALX.printf("Bluetooth disabled\n");
  #endif  

  sprintf(wifiState, "Not connected");
  sprintf(mqttState, "No attempts");
  sprintf(wundergroundState, "No attempts");

  // initialize LITTLEFS for reading credentials
  LITTLEFS_Init();

  #ifdef VERBOSE
  // list files in LITTLEFS
  LITTLEFS_ListDir(LITTLEFS, "/", 10);
  #endif
  // uncomment to clear saved credentials 
  //ClearCredentials();

  // Load values saved in LITTLEFS if any
  LoadCredentials();
  //
  // try to initalize wifi with stored credentials
  // if getting credentials from config page, reboot after to connect with new credentials
  //
  if(!WiFi_Init()) 
  {
    #ifdef VERBOSE
    SERIALX.println("No credentials stored - get from locally hosted page");
    #endif
    GetCredentials();
  }
  
  // zero rain counters
  ZeroRainCounts();
  
  // set up the rising edge interrupt for the rain sensor
  pinMode(RAINGAUGE_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RAINGAUGE_PIN), RainGaugeTrigger, FALLING);  

  #ifdef POWER_STATE_REPORTING
  // if enabled, set up charge state reporting
  ina260LastMeasure = millis();
  // set up voltage/current sensors on I2C bus
  for(int senseIdx = 0; senseIdx < INA260_COUNT; senseIdx++)
  {
    sensors[senseIdx] = Adafruit_INA260();
    if (!sensors[senseIdx].begin(i2cAddresses[senseIdx])) 
    {
      SERIALX.print("Couldn't find INA260 at 0x");
      SERIALX.println(i2cAddresses[senseIdx], HEX);
      while (1);
    }
    SERIALX.print("INA260 connected 0x");
    SERIALX.print(i2cAddresses[senseIdx], HEX);
    SERIALX.print(" ");
    SERIALX.println(sensorDesc[senseIdx]);
    measuredV_ave[senseIdx] = 0.0F;
    measuredV_min[senseIdx] = 1000.0F;
    measuredV_max[senseIdx] = 0.0F;
    measuredA_ave[senseIdx] = 0.0F;
    measuredA_min[senseIdx]= 1000.0F;;
    measuredA_max[senseIdx] = 0.0F;
    ina260MeasureCount = 0;    
  }
  #endif
  // generate a randomized MQTT client name
  // YOU CANNOT DUPLICATE CLIENT NAMES!!!! (and expect them both to work anyway...)
  randomSeed(analogRead(0));
  mqttClientID += String(random(0xffff), HEX);

  #ifdef VERBOSE
  SERIALX.print("MQTT Server: ");
  SERIALX.print(mqtt_server);
  SERIALX.print(" ");
  SERIALX.println(mqttserverIP.toString());

  SERIALX.print("Port: ");
  SERIALX.println(mqtt_portVal);
  SERIALX.print("User: ");
  SERIALX.println(mqtt_user);
  SERIALX.print("Password: ");
  SERIALX.println(mqtt_password);
  #endif
  mqttClient.setServer(mqttserverIP, mqtt_portVal);
  mqttClient.setCallback(MQTT_Callback);

  // get time
  UpdateLocalTime();
}
//
// main loop
// check for rain, update MQTT and Weather Underground on change or every 15 minutes is no changes
//
void loop() 
{
  // only continue if connected to wifi
  if(!wifiConnected)
  {
    return;
  }
  // let mqtt and client stuff happen to keep watchdog on leash
  mqttClient.loop();

  // Read solar panel and battery power state (if enabled by #define POWER_STATE_REPORTING)
  ReadPowerState();

  // on 30 second intervals
  unsigned long now = millis();
  if(now - lastTimeCheck > timeCheckInterval)
  {
    // get time
    UpdateLocalTime();
    timeCheckInterval = 30000;
    if((secondNum % 30) != 0)
    {
      timeCheckInterval += ((30 - (secondNum % 30)) * 1000);
    }
    #ifdef VERBOSE
    SERIALX.printf("Time check interval %d\n", timeCheckInterval);
    #endif
    lastTimeCheck = now;
  }
  // Update minute, hour, day indices in cicular buffers 
  UpdateBufferIndices();
  
  // if there was a click, update values and reset clicked to false
  RainGaugeClicked();

  // never updated listeners, or time to update?
  if((neverUpdated == true) ||
     ((now - lastUpdate) >= INTERVAL_MS))
  {
    // update rain values to be reported to MQTT and WU
    UpdateRainValues();

    // only update on max update time exceeded or rain amount changed
    if((neverUpdated) ||
       (quarterHourRainInches != priorQuarterHourRainInches) ||
       (hourRainInches != priorHourRainInches) ||
       (dayRainInches != priorDayRainInches) ||
       ((now - lastUpdate) >= MAX_UPDATE_INTERVAL_MS))
    {
      
      // average power values since last report
      AveragePowerValues();
      
      #ifdef VERBOSE
      SERIALX.println("Ready to send updates");    
      #endif
      // send report to Weather Underground if enabled
      WU_Report();     

      sprintf(payloadStr,"Connected %s | WiFi: %s | MQTT: %s (%s) | Weather Underground: %s (%s)",connectDateTime, 
                                                                                                  wifiState, 
                                                                                                  mqttState,
                                                                                                  (mqtt_Report ? "Reporting" : "Not reporting"),
                                                                                                  wundergroundState,
                                                                                                  (wUnderground_Report ? "Reporting" : "Not reporting"));

      // send report to MQTT Host if enabled
      MQTT_Report();

      // update last reported values, newver updated, and last update time
      priorQuarterHourRainInches = quarterHourRainInches;
      priorHourRainInches = hourRainInches;
      priorDayRainInches = dayRainInches;
      neverUpdated = false;
      lastUpdate = now;
      ZeroPowerValues();      
      #ifdef VERBOSE
      SERIALX.printf("time %d next update check at %d\n", millis(), (lastUpdate + INTERVAL_MS));    
      #endif
    }
  }
}
//
// zero rain counter variables
//
void ZeroRainCounts()
{
  #ifdef VERBOSE
  SERIALX.println("ZeroRainCounts()");
  #endif
  for(int idx = 0; idx < MIN_PER_DAY; idx++)
  {
    rainByMinute[idx] = 0;
  }
  for(int idx = 0; idx < LAST_X_HOURS; idx++)
  {
    rainByHour[idx] = 0;
  }
  for(int idx = 0; idx < LAST_X_DAYS; idx++)
  {
    rainByDay[idx] = 0;
  }
  #ifdef VERBOSE
  SERIALX.println("Rain counts zeroed");
  #endif
  delay(1000);
}
//
// read solar panel and battery charge start, if enabled
//
void ReadPowerState()
{  
  #ifdef POWER_STATE_REPORTING
  float ina260Val = 0.0F;
  if(millis() - ina260LastMeasure > INA260_INTERVAL)
  {
    for(int senseIdx = 0; senseIdx < INA260_COUNT; senseIdx++)
    {
      ina260Val = sensors[senseIdx].readBusVoltage()/1000.0F;
      ina260Val = ina260Val >= 0.0F ? ina260Val : 0.0F;
      #ifdef VERBOSE
      SERIALX.print(sensorDesc[senseIdx]);
      SERIALX.print(" ");
      SERIALX.print(ina260Val);
      SERIALX.print("V ");
      #endif
      measuredV_ave[senseIdx] += ina260Val;
      measuredV_min[senseIdx] = ina260Val < measuredV_min[senseIdx] ? ina260Val : measuredV_min[senseIdx];
      measuredV_max[senseIdx] = ina260Val > measuredV_max[senseIdx] ? ina260Val : measuredV_max[senseIdx];
      ina260Val = sensors[senseIdx].readCurrent();
      ina260Val = ina260Val >= 0.0F ? ina260Val : 0.0F;
      #ifdef VERBOSE
      SERIALX.print(ina260Val);
      SERIALX.print("mA ");
      #endif
      measuredA_ave[senseIdx] += ina260Val;
      measuredA_min[senseIdx] = ina260Val < measuredA_min[senseIdx] ? ina260Val : measuredA_min[senseIdx];
      measuredA_max[senseIdx] = ina260Val > measuredA_max[senseIdx] ? ina260Val : measuredA_max[senseIdx];
    }
    ina260MeasureCount++;
    SERIALX.print(" measurements ");
    SERIALX.println(ina260MeasureCount);
    ina260LastMeasure = millis();
  }
  #endif   
}
//
// update minute, day, hour indices in respective circular buffers
//
void UpdateBufferIndices()
{
  // check and update minute/hour/day circular buffer indices
  // on change update last, increment index, zero circular buffer entry
  bool indexChanged = false;
  if(lastMinNum != minNum)
  {
    lastMinNum = minNum;
    rainByMinuteIdx = rainByMinuteIdx + 1 < MIN_PER_DAY ? rainByMinuteIdx + 1 : 0;
    rainByMinute[rainByMinuteIdx] = 0;
    indexChanged = true;
  }
  if(lastHourNum != hourNum)
  {
    lastHourNum = hourNum;
    // get next rain by hour index, reset rain clicks
    rainByHourIdx = rainByHourIdx + 1 < LAST_X_HOURS ? rainByHourIdx + 1 : 0;
    rainByHour[rainByHourIdx] = 0;
    indexChanged = true;
  }
  if(lastDayNum != dayNum)
  {
    lastDayNum = dayNum;
    // get next rain by day index, reset rain clicks
    // replace with check for hour change, update rainByHourIdx
    rainByDayIdx = rainByDayIdx + 1 < LAST_X_DAYS ? rainByDayIdx + 1 : 0;
    rainByDay[rainByDayIdx] = 0;
    indexChanged = true;
  }
  #ifdef VERBOSE
  if(indexChanged)
  {
    SERIALX.printf("%s\tIndices: Minute %d\tHour %d\tDay %d\n", localTimeStr, rainByMinuteIdx, rainByHourIdx, rainByDayIdx);
  }
  #endif
}
//
// rain gauge clicked
//
void RainGaugeClicked()
{
  if(clicked)
  {
    clicked = false;
    #ifdef VERBOSE
    SERIALX.println(">>>>>click<<<<<");
    #endif
    rainByMinute[rainByMinuteIdx]++;
    rainByHour[rainByHourIdx]++;
    rainByDay[rainByDayIdx]++;
    totalTicks++;
  }
}
//
// update rain values to be reported to MQTT and WU
//
void UpdateRainValues()
{
  // update count for last minute
  rainForLastMinute = rainByMinute[rainByMinuteIdx];
  // update total counts for last 24 hours, 1 hour, 15 minutes (most recent 15 and 60 readings in circular buffer)
  rainForLastQuarter = 0;
  rainForLastHour = 0;
  rainForLastDay = 0;
  int minIdx = rainByMinuteIdx;
  for(int minCount = 0; minCount < MIN_PER_DAY; minCount++)
  {
    if(minCount < 15)
    {
      rainForLastQuarter += rainByMinute[minIdx];
    }
    if(minCount < 60)
    {
      rainForLastHour += rainByMinute[minIdx];
    }
    rainForLastDay += rainByMinute[minIdx];
    minIdx = (minIdx - 1) >= 0 ? (minIdx - 1) : MIN_PER_DAY - 1;
  }
  // update values for web page, output to serial for debug
  currentRainInches = (float)rainForLastMinute * INCHES_PER_CLICK;
  quarterHourRainInches = (float)rainForLastQuarter * INCHES_PER_CLICK;
  hourRainInches = (float)rainForLastHour * INCHES_PER_CLICK;
  dayRainInches = (float)rainForLastDay * INCHES_PER_CLICK;
}
//
// report to Weather Underground if enabled
//
void WU_Report()
{
  if(wUnderground_Report == true)
  {
    #ifdef VERBOSE
    SERIALX.println(">>>>>Update Weather Underground<<<<<");
    #endif
    //
    // Weather Underground update
    //
    // Set up the generic use-every-time part of the URL
    String url = "/weatherstation/updateweatherstation.php";
    url += "?ID=";
    url += wUndergroundID;
    url += "&PASSWORD=";
    url += wUndergroundKey;
    url += "&dateutc=now&action=updateraw";
    // Now let's add in the data that we've collected from our sensors
    // Start with rain in last hour/day
    url += "&rainin=";
    url += hourRainInches;
    url += "&dailyrainin=";
    url += dayRainInches;
    // Connnect to Weather Underground. If the connection fails, return from
    //  loop and start over again.
    #ifdef VERBOSE      
    SERIALX.println(">>>>>Update Weather Underground<<<<<");    
    #endif
    if (!wifiClient.connect(host, 80))
    {
      #ifdef VERBOSE      
      SERIALX.println("Weather Underground connection failed");
      #endif
      sprintf(wundergroundState, "Not connected");
      return;
    }
    else
    {
      #ifdef VERBOSE      
      SERIALX.println("Weather Underground connected");
      #endif
      sprintf(wundergroundState, "Connected");
    }
    // Issue the GET command to Weather Underground to post the data we've 
    //  collected.
    wifiClient.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");

    // Give Weather Underground five seconds to reply.
    unsigned long timeout = millis();
    while (wifiClient.available() == 0) 
    {
      if (millis() - timeout > 5000) 
      {
        #ifdef VERBOSE      
        SERIALX.println(">>> WU Update - client Timeout !");
        #endif
        wifiClient.stop();
        sprintf(wundergroundState, "%s %s", wundergroundState, " Weather Underground client timeout");          
        return;
      }
    }
    // Read the response from Weather Underground and print it to the console.
    char wundergroundResponse[256];
    while(wifiClient.available()) 
    {
      String line = wifiClient.readStringUntil('\r');
      SERIALX.println(line);
      sprintf(wundergroundResponse, "%s", line);          
    }
    sprintf(wundergroundState, "%s %s", wundergroundState, wundergroundResponse);          
    #ifdef VERBOSE      
    SERIALX.println(wundergroundResponse);
    #endif      
  }
  else
  {
    #ifdef VERBOSE
    SERIALX.println(">>>>>Weather Underground reporting OFF<<<<<");
    #endif
  }
}
//
//
// report to MQTT Host
void MQTT_Report()
{    
  if(mqtt_Report)
  {
    #ifdef VERBOSE
    SERIALX.println(">>>>>MQTT update ON<<<<<");    
    SERIALX.println(payloadStr);
    #endif
    //
    // MQTT update
    //    
    published_payload[0] = String(localTimeStr);
    published_payload[1] = String(quarterHourRainInches, 4);
    published_payload[2] = String(hourRainInches, 4);
    published_payload[3] = String(dayRainInches, 4);
    published_payload[4] = String(payloadStr);
    #ifdef POWER_STATE_REPORTING
    published_payload[ 5] = String(measuredV_ave[0]);
    published_payload[ 6] = String(measuredV_min[0]); 
    published_payload[ 7] = String(measuredV_max[0]);
    published_payload[ 8] = String(measuredA_ave[0]);
    published_payload[ 9] = String(measuredA_min[0]);
    published_payload[10] = String(measuredA_max[0]);
    published_payload[11] = String(measuredV_ave[1]);
    published_payload[12] = String(measuredV_min[1]); 
    published_payload[13] = String(measuredV_max[1]);
    published_payload[14] = String(measuredA_ave[1]);
    published_payload[15] = String(measuredA_min[1]);
    published_payload[16] = String(measuredA_max[1]);
    #endif      
    MQTT_PublishTopics();    
  }
  else
  {
    SERIALX.println(">>>>>MQTT update OFF<<<<<");    
  }
}
//
// average solar and battery power values if enabled
//
void AveragePowerValues()
{      
  #ifdef POWER_STATE_REPORTING
  // average volt/amp reading since last report
  for(int senseIdx = 0; senseIdx < INA260_COUNT; senseIdx++)
  {
    measuredV_ave[senseIdx] /= (float)ina260MeasureCount;
    measuredA_ave[senseIdx] /= (float)ina260MeasureCount;
  }
  #ifdef VERBOSE
  for(int senseIdx = 0; senseIdx < INA260_COUNT; senseIdx++)
  {
    SERIALX.printf("\t%s\tV ave %0.1f min %0.1f max %0.1f\t mA ave %0.1f min %0.1f max %0.1f\n", sensorDesc[senseIdx].c_str(),
                                                                                                 measuredV_ave[senseIdx], 
                                                                                                 measuredV_min[senseIdx], 
                                                                                                 measuredV_max[senseIdx], 
                                                                                                 measuredA_ave[senseIdx], 
                                                                                                 measuredA_min[senseIdx], 
                                                                                                 measuredA_max[senseIdx]);
  }
  #endif    
  #endif    
}      
//
//
//
void ZeroPowerValues()
{
  #ifdef POWER_STATE_REPORTING
  for(int senseIdx = 0; senseIdx < INA260_COUNT; senseIdx++)
  {
    measuredV_ave[senseIdx] = 0.0F;
    measuredV_min[senseIdx] = 1000.0F;
    measuredV_max[senseIdx] = 0.0F;
    measuredA_ave[senseIdx] = 0.0F;
    measuredA_min[senseIdx]= 1000.0F;;
    measuredA_max[senseIdx] = 0.0F;
    ina260MeasureCount = 0;    
  }
  #endif    
}
//
// trigger on hall sensor low as magnet passes by
//
ICACHE_RAM_ATTR void RainGaugeTrigger()
{
  if(digitalRead(RAINGAUGE_PIN) == LOW)
  {
    clicked = true;
  }
}
//
// ================================ begin MQTT functions ========================================
//
//****************************************************************************
//
// (re)connect to MQTT server
//
//****************************************************************************
bool MQTT_Reconnect() 
{
  // Loop until we're reconnected
  int attemptCount = 0;
  bool mqttConnect = true;
  while (!mqttClient.connected() && (attemptCount < 100)) 
  {
    sprintf(mqttState, "Not connected");
    mqttConnect = false;
    #ifdef VERBOSE
    SERIALX.printf("MQTT connect attempt %d ", (attemptCount + 1));
    SERIALX.print(" Server IP: >");
    SERIALX.print(mqttserverIP.toString());
    SERIALX.print("< Port: >");
    SERIALX.print(mqtt_portVal);
    SERIALX.print("< mqttClientID: >");
    SERIALX.print(mqttClientID);
    SERIALX.print("< mqtt_user: >");
    SERIALX.print(mqtt_user);
    SERIALX.print("< mqtt_password: >");
    SERIALX.print(mqtt_password);
    SERIALX.println("<");
    #endif
    // connected, subscribe and publish
    if (mqttClient.connect(mqttClientID.c_str(), mqtt_user.c_str(), mqtt_password.c_str())) 
    {
      mqttConnect = true;
      sprintf(mqttState, "Connected");
      break;
    }
    // Wait before retrying
    delay(RECONNECT_DELAY);
    attemptCount++;
  }
  if(!mqttConnect)
  {
    SERIALX.print("MQTT not connected!");
    SERIALX.print(" Server IP: ");
    SERIALX.print(mqttserverIP.toString());
    SERIALX.print(" Port: ");
    SERIALX.print(mqtt_portVal);
    SERIALX.print(" mqttClientID: ");
    SERIALX.print(mqttClientID.c_str());
    SERIALX.print(" mqtt_user: ");
    SERIALX.print(mqtt_user.c_str());
    SERIALX.print(" mqtt_password: ");
    SERIALX.println(mqtt_password.c_str());
    return mqttConnect;
  }
  delay(RECONNECT_DELAY);
  SERIALX.println("MQTT connected!");
  // we're connected
  // set up listeners for server updates  
  MQTT_SubscribeTopics();
  return mqttConnect;

}
//****************************************************************************
//
// handle subscribed MQTT message received
// determine which message was received and what needs to be done
//
//****************************************************************************
void MQTT_Callback(char* topic, byte* payload, unsigned int length) 
{
  String topicStr;
  String payloadStr;
  topicStr = topic;
  for (int i = 0; i < length; i++) 
  {
    payloadStr += (char)payload[i];
  }
  #ifdef VERBOSE
  SERIALX.print("MQTT_Callback ");
  SERIALX.print(topic);
  SERIALX.print(" payload  ");
  SERIALX.println(payloadStr);
  #endif
  if(topicStr == "Rain_Gauge/WU_RainGaugeWUReport")
  {
    if(payloadStr == "\"ON\"")
    {
      wUnderground_Report = true;
    }
    else
    {
      wUnderground_Report = false;
    }
    #ifdef VERBOSE
    SERIALX.printf("Weather Underground reporting %s - rain counts zeroed\n", wUnderground_Report ? "ON" : "OFF");
    #endif
    // in either case, zero the rain stats
    ZeroRainCounts();
  }
  if(topicStr == "Rain_Gauge/MQTT_RainGaugeMQTTReport")
  {
    if(payloadStr == "\"ON\"")
    {
      mqtt_Report = true;
    }
    else
    {
      mqtt_Report = false;
    }
    #ifdef VERBOSE
    SERIALX.printf("MQTT reporting %s\n", mqtt_Report ? "ON" : "OFF");
    #endif
  }
  else if((topicStr == "Rain_Gauge/RainGauge_ResetCredentials") && (payloadStr == "ON"))
  {
    #ifdef VERBOSE
    SERIALX.println("Reset - clear credentials, restart");
    #endif
    ClearCredentials();
  }
  else if((topicStr == "Rain_Gauge/RainGauge_ZeroValues") && (payloadStr == "1"))
  {
    #ifdef VERBOSE
    SERIALX.println("Zero values");
    #endif
    ZeroRainCounts();
  }
}
//
// subscribed MQTT topics (get updates from MQTT server)
//
void MQTT_SubscribeTopics()
{  
  bool subscribed = false;
  for(int idx = 0; idx < MAX_SUBSCRIBE; idx++)
  {
    subscribed = mqttClient.subscribe(subscribed_topic[idx].c_str());
    #ifdef VERBOSE
    SERIALX.printf("subscribing topic %s %s\n", subscribed_topic[idx].c_str(), (subscribed ? "success!" : "failed"));
    #endif
  }
}
//
// published MQTT topics (send updates to MQTT server)
// if multiple topics could be published, it might be good to either pass in the specific name
// or make separate publish functions
//
bool MQTT_PublishTopics()
{
  bool connected = mqttClient.connected();
  if (!connected)
  {
    connected = MQTT_Reconnect();
  }
  //
  if(!connected)
  {
    SERIALX.println("MQTT not connected in MQTT_PublishTopics()");
    return connected;
  }
  int pubSubResult = 0;
  for(int idx = 0; idx < MAX_PUBLISH; idx++)
  {
    pubSubResult = mqttClient.publish(published_topic[idx].c_str(), published_payload[idx].c_str());
    #ifdef VERBOSE
    SERIALX.print((pubSubResult == 0 ? "FAIL TO PUBLISH Topic: " : "PUBLISHED Topic: "));
    SERIALX.print(published_topic[idx]);
    SERIALX.print(" ");
    SERIALX.println(published_payload[idx]);
    #endif
  }
  return connected;
}
//
// ================================ end MQTT functions ========================================
//
// ================================ begin LITTLEFS functions ================================
//
// Initialize LITTLEFS
//
void LITTLEFS_Init() 
{
  if (!LITTLEFS.begin(true)) 
  {
    #ifdef VERBOSE
    SERIALX.println("An error has occurred while mounting LITTLEFS");
    #endif
    return;
  }
  #ifdef VERBOSE
  SERIALX.println("LITTLEFS mounted successfully");
  #endif
}
//
// Read File from LITTLEFS
//
String LITTLEFS_ReadFile(fs::FS &fs, const char * path)
{
  #ifdef VERBOSE
  SERIALX.printf("Reading file: %s - ", path);
  #endif
  File file = fs.open(path);
  if(!file || file.isDirectory())
  {
    #ifdef VERBOSE
    SERIALX.println("- failed to open file for reading");
    #endif
    return String();
  }
  
  String fileContent;
  while(file.available())
  {
    fileContent = file.readStringUntil('\n');
    #ifdef VERBOSE
    //SERIALX.println(fileContent);
    #endif
  }
    #ifdef VERBOSE
    SERIALX.println("- read file");
    #endif
  return fileContent;
}
//
// Write file to LITTLEFS
//
void LITTLEFS_WriteFile(fs::FS &fs, const char * path, const char * message)
{
  #ifdef VERBOSE
  SERIALX.printf("Writing >>%s<< to file: %s ", message, path);
  #endif
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    #ifdef VERBOSE
    SERIALX.println("- failed to open file for writing");
    #endif
    return;
  }
  if(file.print(message))
  {
    #ifdef VERBOSE
    SERIALX.println("- file written");
    #endif
  }
   else 
   {
    #ifdef VERBOSE
    SERIALX.println("- file write failed");
    #endif
  }
}
//
// list LITTLEFS files
//
void LITTLEFS_ListDir(fs::FS &fs, const char * dirname, uint8_t levels)
{
    SERIALX.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root)
    {
        SERIALX.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory())
    {
        SERIALX.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file)
    {
        if(file.isDirectory())
        {
            SERIALX.print("  DIR : ");
            SERIALX.println(file.name());
            if(levels)
            {
                LITTLEFS_ListDir(fs, file.name(), levels -1);
            }
        }
         else 
         {
            SERIALX.print("  FILE: ");
            SERIALX.print(file.name());
            SERIALX.print("\tSIZE: ");
            SERIALX.println(file.size());
        }
        file = root.openNextFile();
    }
}
//
// delete named file from LITTLEFS
//
void LITTLEFS_DeleteFile(fs::FS &fs, const char * path)
{
  #ifdef VERBOSE
  SERIALX.printf("Deleting file: %s ", path);
  #endif
  if(fs.remove(path))
  {
    #ifdef VERBOSE
    SERIALX.println("- file deleted");
    #endif
  }
  else 
  {
    #ifdef VERBOSE
    SERIALX.println("- delete failed");
    #endif
  }
}
// ================================ end LITTLEFS functions ================================
// ================================ begin WiFi initialize/credentials functions ================================
//
// Initialize WiFi
//
bool WiFi_Init() 
{
  // cant connect if there's no WiFi SSID defined
  if(ssid=="")
  {
    #ifdef VERBOSE
    SERIALX.println("Undefined SSID");
    #endif    
    wifiConnected = false;
    return false;
  }

  WiFi.mode(WIFI_STA);

  // for (optional) user defined gateway and local IP  
  localIP.fromString(ip.c_str());
  localGateway.fromString(gateway.c_str());
  if((ip != "") || (gateway != ""))
  {
    #ifdef VERBOSE
    SERIALX.printf("Configure wifi localIP %s gateway %s\n", ip, gateway);
    #endif
    if (!WiFi.config(localIP, localGateway, subnet))
    {
      #ifdef VERBOSE
      SERIALX.println("STA Failed to configure");
      #endif
      wifiConnected = false;
      return false;
    }
  }
  else
  {
    #ifdef VERBOSE  
    SERIALX.println("Connect to wifi with DNS assigned IP");
    #endif
  }
  // set up and connect to wifi
  WiFi.begin(ssid.c_str(), pass.c_str());
  #ifdef VERBOSE
  SERIALX.printf("Connecting to WiFi SSID: %s PWD: %s...", ssid.c_str(), pass.c_str());
  #endif
  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  int retryCount = 0;
  previousMillis = millis();
  wifiConnected = true;
  while((WiFi.status() != WL_CONNECTED) && (retryCount < 10))
  {
    currentMillis = millis();
    if (currentMillis - previousMillis >= WIFI_WAIT) 
    {
      #ifdef VERBOSE
      SERIALX.printf("Failed to connect on try %d of 10.", retryCount+1);
      #endif
      wifiConnected = false;
      retryCount++;
      previousMillis = currentMillis;
    }
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if(!wifiConnected)
  {
      SERIALX.printf("Failed to connect after 10 attempts - reset credentials");
      ClearCredentials();
  }
  sprintf(wifiState, "Connected %s ",WiFi.localIP().toString().c_str());

  #ifdef VERBOSE
  SERIALX.println(wifiState);
  #endif
  return wifiConnected;
}
//
// load wifi credentials from LITTLEFS
//
void LoadCredentials()
{
  ssid = LITTLEFS_ReadFile(LITTLEFS, ssidPath);
  ssid.trim();
  pass = LITTLEFS_ReadFile(LITTLEFS, passPath);
  pass.trim();
  ip = LITTLEFS_ReadFile(LITTLEFS, ipPath);
  ip.trim();
  gateway = LITTLEFS_ReadFile (LITTLEFS, gatewayPath);
  gateway.trim();
  tz = LITTLEFS_ReadFile (LITTLEFS, tzPath);
  tz.trim();
  dst = LITTLEFS_ReadFile (LITTLEFS, dstPath);
  dst.trim();
  
  gmtOffset_sec = atoi(tz.c_str()) * 3600; // convert hours to seconds
  daylightOffset_sec = atoi(dst.c_str()) * 3600; // convert hours to seconds

  mqtt_server = LITTLEFS_ReadFile(LITTLEFS, mqtt_serverPath);
  mqtt_server.trim();
  mqttserverIP.fromString(mqtt_server);
  mqtt_port = LITTLEFS_ReadFile(LITTLEFS, mqtt_portPath);
  mqtt_port.trim();
  mqtt_portVal = atoi(mqtt_port.c_str());
  mqtt_user = LITTLEFS_ReadFile(LITTLEFS, mqtt_userPath);
  mqtt_user.trim();
  mqtt_password = LITTLEFS_ReadFile(LITTLEFS, mqtt_passwordPath);
  mqtt_password.trim();


  wUndergroundID = LITTLEFS_ReadFile(LITTLEFS, wu_IDPath);
  wUndergroundID.trim();
  wUndergroundKey = LITTLEFS_ReadFile(LITTLEFS, wu_KeyPath);
  wUndergroundKey.trim();
  
  LITTLEFS_ReadFile(LITTLEFS, "/wifimanager.html");
  #ifdef VERBOSE
  SERIALX.print("SSID: ");
  SERIALX.println(ssid);
  SERIALX.print("PASSWORD: ");
  SERIALX.println(pass);
  SERIALX.print("Fixed IP (optional): ");
  SERIALX.println(ip);
  SERIALX.print("Fixed gateway (optional): ");
  SERIALX.println(gateway);
  SERIALX.print("Timezone offset: ");
  SERIALX.print(tz);
  SERIALX.print(" ");
  SERIALX.println(gmtOffset_sec);
  SERIALX.print("DST offset: ");
  SERIALX.print(dst);
  SERIALX.print(" ");
  SERIALX.println(daylightOffset_sec);

  SERIALX.println("MQTT credentials");
  SERIALX.print("Server: ");
  SERIALX.println(mqtt_server);
  SERIALX.print("Port: ");
  SERIALX.println(mqtt_port);
  SERIALX.print("User: ");
  SERIALX.println(mqtt_user);
  SERIALX.print("Password: ");
  SERIALX.println(mqtt_password);

  SERIALX.println("Weather Underground credentials");
  SERIALX.print("ID : ");
  SERIALX.println(wUndergroundID);
  SERIALX.print("Key: ");
  SERIALX.println(wUndergroundKey);

  #endif
}
//
// get new credentials from user from web page in access point mode
//
void GetCredentials()
{
  disableCore0WDT();
  disableCore1WDT();
  // Connect to Wi-Fi network with SSID and password
  #ifdef VERBOSE
  SERIALX.print("Setting AP (Access Point) ");
  #endif
  // NULL sets an open Access Point
  WiFi.softAP("ESP-WIFI-MANAGER", NULL);

  IPAddress IP = WiFi.softAPIP();
  #ifdef VERBOSE
  SERIALX.print(" address: ");
  SERIALX.println(IP); 
  #endif
  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    #ifdef VERBOSE
    SERIALX.println("Request from webserver, send page");
    #endif  
    request->send(LITTLEFS, "/wifimanager.html", "text/html");
  });
    
  server.serveStatic("/", LITTLEFS, "/");
  //
  // display web page and get credentials from user
  //  
  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) 
  {
    int params = request->params();
    for(int i=0;i<params;i++)
    {
      yield();
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost())
      {
        // HTTP POST ssid value
        if (p->name() == PARAM_INPUT_1) 
        {
          ssid = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_1, ssid);
          #endif
        }
        // HTTP POST password value
        if (p->name() == PARAM_INPUT_2) 
        {
          pass = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_2, pass);
          #endif
        }
        // HTTP POST local ip value
        if (p->name() == PARAM_INPUT_3) 
        {
          ip = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_3, ip);
          #endif
        }
        // HTTP POST gateway ip value
        if (p->name() == PARAM_INPUT_4) 
        {
          gateway = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_4, gateway);
          #endif
        }
        // HTTP POST time zone offset value
        if (p->name() == PARAM_INPUT_5) 
        {
          tz = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_5, tz);
          #endif
        }
        // HTTP POST dst offset value
        if (p->name() == PARAM_INPUT_6) 
        {
          dst = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_6, dst);
          #endif
        }
        // HTTP POST mqtt server ip value
        if (p->name() == PARAM_INPUT_7) 
        {
          mqtt_server = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_7, mqtt_server);
          #endif
        }
        // HTTP POST mqtt port value
        if (p->name() == PARAM_INPUT_8) 
        {
          mqtt_port = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_8, mqtt_port);
          #endif
        }
        // HTTP POST mqtt username value
        if (p->name() == PARAM_INPUT_9) 
        {
          mqtt_user = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_9, mqtt_user);
          #endif
        }
        // HTTP POST mqtt password value
        if (p->name() == PARAM_INPUT_10) 
        {
          mqtt_password = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_10, mqtt_password);
          #endif
        }
        // Weather Underground credentials
        // HTTP POST Weather Underground ID
        if (p->name() == PARAM_INPUT_11) 
        {
          wUndergroundID = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_11, wUndergroundID);
          #endif
        }
        // HTTP POST Weather Underground Key
        if (p->name() == PARAM_INPUT_12) 
        {
          wUndergroundKey = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_12, wUndergroundKey);
          #endif
        }
      }
    } 
    request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
    #ifdef VERBOSE
    SERIALX.println("Store credentials in LITTLEFS and reboot");
    #endif
    SaveCredentials();
  });
  server.begin();
}
void SaveCredentials()
{
  LITTLEFS_WriteFile(LITTLEFS, ssidPath, ssid.c_str());
  LITTLEFS_WriteFile(LITTLEFS, passPath, pass.c_str());
  LITTLEFS_WriteFile(LITTLEFS, ipPath, ip.c_str());
  LITTLEFS_WriteFile(LITTLEFS, gatewayPath, gateway.c_str());
  LITTLEFS_WriteFile(LITTLEFS, tzPath, tz.c_str());
  LITTLEFS_WriteFile(LITTLEFS, dstPath, dst.c_str());
  LITTLEFS_WriteFile(LITTLEFS, mqtt_serverPath, mqtt_server.c_str());
  LITTLEFS_WriteFile(LITTLEFS, mqtt_portPath, mqtt_port.c_str());
  LITTLEFS_WriteFile(LITTLEFS, mqtt_userPath, mqtt_user.c_str());
  LITTLEFS_WriteFile(LITTLEFS, mqtt_passwordPath, mqtt_password.c_str());
  LITTLEFS_WriteFile(LITTLEFS, wu_IDPath, wUndergroundID.c_str());
  LITTLEFS_WriteFile(LITTLEFS, wu_KeyPath, wUndergroundKey.c_str());
  #ifdef VERBOSE
  SERIALX.print("SSID set to: ");
  SERIALX.println(ssid);
  SERIALX.print("Password set to: ");
  SERIALX.println(pass);
  SERIALX.print("IP Address set to: ");
  SERIALX.println(ip);
  SERIALX.print("Time zone offset set to: ");
  SERIALX.println(tz);
  SERIALX.print("DST offset set to: ");
  SERIALX.println(dst);
  SERIALX.print("Gateway set to: ");
  SERIALX.println(gateway);
  SERIALX.print("MQTT server IP: ");
  SERIALX.println(mqtt_server);
  SERIALX.print("MQTT port: ");
  SERIALX.println(mqtt_port);
  SERIALX.print("MQTT username: ");
  SERIALX.println(mqtt_user);
  SERIALX.print("MQTT password: ");
  SERIALX.println(mqtt_password);
  SERIALX.print("Weather Underground ID: ");
  SERIALX.println(wUndergroundID);
  SERIALX.print("Weather Underground Key: ");
  SERIALX.println(wUndergroundKey);
  #endif
  ESP.restart();
}
//
// clear credentials and restart
// allows user to change wifi SSIDs easily
//
void ClearCredentials()
{
  #ifdef VERBOSE
  SERIALX.println("Clear WiFi credentials");
  #endif
  LITTLEFS_DeleteFile(LITTLEFS, ssidPath);
  LITTLEFS_DeleteFile(LITTLEFS, passPath);
  LITTLEFS_DeleteFile(LITTLEFS, ipPath);
  LITTLEFS_DeleteFile(LITTLEFS, gatewayPath);
  LITTLEFS_DeleteFile(LITTLEFS, tzPath);
  LITTLEFS_DeleteFile(LITTLEFS, dstPath);
  // MQTT
  LITTLEFS_DeleteFile(LITTLEFS, mqtt_serverPath);
  LITTLEFS_DeleteFile(LITTLEFS, mqtt_portPath);
  LITTLEFS_DeleteFile(LITTLEFS, mqtt_userPath);
  LITTLEFS_DeleteFile(LITTLEFS, mqtt_passwordPath);
  // Weather Underground
  LITTLEFS_DeleteFile(LITTLEFS, wu_IDPath);
  LITTLEFS_DeleteFile(LITTLEFS, wu_KeyPath);
  
  #ifdef VERBOSE
  SERIALX.println("Restart...");
  #endif
  delay(WIFI_WAIT);
  ESP.restart();
}
// ================================ end WiFi initialize/credentials functions ================================
// ================================ begin NTP/RTC time functions ================================
//
// get local time (initially set via NTP server)
//
void UpdateLocalTime()
{
  if(!wifiConnected)
  {
    return;
  }
  // if not set from NTP, get time and set RTC
  if(!rtcTimeSet)
  {
    #ifdef VERBOSE
    SERIALX.print("Time from NTP server ");
    #endif
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo))
    {
      // Init timeserver
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      getLocalTime(&timeinfo);
    }
    //GET DATE
    strftime(dayMonth, sizeof(dayMonth), "%d", &timeinfo);
    strftime(monthName, sizeof(monthName), "%b", &timeinfo);
    strftime(year, sizeof(year), "%Y", &timeinfo);
    dayNum = atoi(dayMonth);
    monthNum = timeinfo.tm_mon+1;
    yearNum = atoi(year);

    //GET TIME
    strftime(hour, sizeof(hour), "%H", &timeinfo);
    strftime(minute, sizeof(minute), "%M", &timeinfo);
    strftime(second, sizeof(second), "%S", &timeinfo);
    hourNum = atoi(hour);
    minNum = atoi(minute);
    secondNum = atoi(second);

    //rtc.setTime(secondNum, minNum, hourNum, dayNum, monthNum, yearNum);
    rtc.setTimeStruct(timeinfo);
    rtcTimeSet = true;

    lastMinNum = minNum;
    lastHourNum = hourNum;
    lastDayNum = dayNum;
  }
  // use RTC for time
  else
  {
    #ifdef VERBOSE
    SERIALX.print("Time from local RTC ");
    #endif
    dayNum = rtc.getDay();
    monthNum = rtc.getMonth() + 1;
    yearNum = rtc.getYear();
    hourNum = rtc.getHour();
    minNum = rtc.getMinute();
    secondNum = rtc.getSecond();
  }
  // set last time values to current
  if(lastMinNum == -1)
  {
    lastMinNum = minNum;
    lastHourNum = hourNum;
    lastDayNum = dayNum;
  }
  sprintf(localTimeStr, "%02d/%02d/%04d %02d:%02d:%02d", monthNum, dayNum, yearNum, hourNum, minNum, secondNum);
  if(!connectDateTimeSet)
  {
    strcpy(connectDateTime, localTimeStr);
    connectDateTimeSet = true;
  }
  #ifdef VERBOSE
  SERIALX.println(localTimeStr);
  #endif
}
// ================================ end NTP/RTC time functions ================================