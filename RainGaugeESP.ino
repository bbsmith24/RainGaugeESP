/*********
  Brian Smith
  10/2022

  IOT Rain Gauge
  Serves a web page, connects to MQTT server, push data to weather underground
  Serves a web page for ssid/password if not previously stored to SPIFFS

*********/

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
//#include "SPIFFS.h"
#include <time.h>        // for NTP time
#include <ESP32Time.h>   // for RTC time
#include <ESP32Ping.h>
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>

void MQTT_Callback(char* topic, byte* payload, unsigned int length);

//#define VERBOSE
#define WEATHER_UNDERGROUND

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Search for parameter in HTTP POST request
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
const char* PARAM_INPUT_3 = "ip";
const char* PARAM_INPUT_4 = "gateway";

// Variables to save values from HTML form
String ssid;
String pass;
String ip;
String gateway;

// File paths to save input values permanently
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* ipPath = "/ip.txt";
const char* gatewayPath = "/gateway.txt";

// hardcoded
IPAddress localGateway(192, 168, 1, 1);
IPAddress localIP(192, 168, 1, 200); 
IPAddress subnet(255, 255, 255, 0);
IPAddress dnsServer(8,8,8,8);
// Timer variables
unsigned long previousMillis = 0;
const long interval = 10000;  // interval to wait for Wi-Fi connection (milliseconds)

// NTP Server Details
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -18000;
const int   daylightOffset_sec = 3600;
ESP32Time rtc(0/*-18000*/);

int dayNum;
int monthNum;
int yearNum;
int hourNum;
int minNum;
int secondNum;
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

#define MIN_PER_DAY 1440
#define LAST_X_DAYS 28
#define LAST_X_HOURS 24
#define INCHES_PER_CLICK 0.011F
//#define INTERVAL_MS           60000 //    1 minute interval
#define INTERVAL_MS            150000 //  2.5 minute interval
//#define INTERVAL_MS          300000 //    5 minute interval
//#define MAX_UPDATE_INTERVAL_MS 600000 //   10 minute max between updates
#define MAX_UPDATE_INTERVAL_MS 900000 // 15 minute max between updates
// for devkit c
#define RAINGAUGE_PIN 14 
// for Sparkfun ESP32 Thing Plus
//#define LED_PIN 13
//#define RAINGAUGE_PIN 5  
// for Sparkfun ESP32 Thing
//#define RAINGAUGE_PIN 5  
#define LED_PIN 5
#define MAX_SUBSCRIBE        10
#define MAX_PUBLISH          10
// wait between wifi and MQTT server connect attempts
#define RECONNECT_DELAY    5000
// wait between sensor updates
#define CHECK_STATE_DELAY 60000

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
unsigned long lastForceUpdate = 0;
unsigned long lastUpdate = 0;
//int ledState = 1;
unsigned long currentTime = 0;
volatile bool clicked = false;
// mqtt setup
IPAddress mqttServer(192, 168, 1, 76);
//char mqtt_server[40];
char mqtt_port[6]  = "8080";
char mqtt_user[40];
char mqtt_password[40];
char mqtt_api_token[32] = "YOUR_API_TOKEN";
byte mqtt_ip[4] = {192, 168, 1, 76};
int  mqtt_portVal = 1883;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
String mqttClientID = "MQTTRainGauge_";   // local name, must be unique on MQTT server so append a random string at the end
// subscribed (listening) topic(s)
int subscribed_count = 1;
int published_count = 5;
String subscribed_topic[MAX_SUBSCRIBE];
String published_topic[MAX_PUBLISH];
String published_payload[MAX_PUBLISH];
char payloadStr[512];
// Weather underground credentials
String WUndergroundID = "KMINOVI53";
String WUndergroundKey = "fQnoqk2e";  
const char* host = "weatherstation.wunderground.com";

char wifiState[256];
char mqttState[256];
char wundergroundState[256];

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
// scan for wifi access points
//
void ScanWiFi()
{
#ifdef VERBOSE
  Serial.println("WiFi scan start");

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0) 
  {
      Serial.println("no networks found");
  }
  else 
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN)?" ":"*");
      delay(10);
    }
  }
#endif  
}
//
// get local time (initially set via NTP server)
//
void UpdateLocalTime()
{
  #ifdef VERBOSE
  Serial.print("Update local time ");
  #endif
  // if not set from NTP, get time and set RTC
  if(!rtcTimeSet)
  {
    digitalWrite(LED_PIN, HIGH);
    #ifdef VERBOSE
    Serial.print("from NTP server ");    
    #endif
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      // Init timeserver
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      getLocalTime(&timeinfo);
    }
    //GET DATE
    strftime(weekDay, sizeof(weekDay), "%a", &timeinfo);
    strftime(dayMonth, sizeof(dayMonth), "%d", &timeinfo);
    strftime(monthName, sizeof(monthName), "%b", &timeinfo);
    strftime(year, sizeof(year), "%Y", &timeinfo);

    //GET TIME
    strftime(hour, sizeof(hour), "%H", &timeinfo);
    strftime(minute, sizeof(minute), "%M", &timeinfo);
    strftime(second, sizeof(second), "%S", &timeinfo);

    dayNum = atoi(dayMonth);
    monthNum = timeinfo.tm_mon+1;
    yearNum = atoi(year);
    hourNum = atoi(hour);
    minNum = atoi(minute);
    secondNum = atoi(second);

    //rtc.setTime(secondNum, minNum, hourNum, dayNum, monthNum, yearNum);
    rtc.setTimeStruct(timeinfo);
    rtcTimeSet = true;
    digitalWrite(LED_PIN, LOW);
  }
  // use RTC for time
  else
  {
    #ifdef VERBOSE
    Serial.print("from embedded RTC ");  
    #endif 
    dayNum = rtc.getDay();
    monthNum = rtc.getMonth() + 1;
    yearNum = rtc.getYear();
    hourNum = rtc.getHour();
    minNum = rtc.getMinute();
    secondNum = rtc.getSecond();
  }
  sprintf(localTimeStr, "%d/%d/%d %02d:%02d:%02d", monthNum, dayNum, yearNum, hourNum, minNum, secondNum);
  #ifdef VERBOSE
  Serial.println(localTimeStr);
  Serial.flush();
  #endif
  if(!connectDateTimeSet)
  {
    strcpy(connectDateTime, localTimeStr);
    connectDateTimeSet = true;
  }
}
////
//// Initialize SPIFFS
////
//void initSPIFFS() 
//{
//  if (!SPIFFS.begin(true)) 
//  {
//#ifdef VERBOSE
//    Serial.println("An error has occurred while mounting SPIFFS");
//#endif
//  }
//#ifdef VERBOSE  
//  Serial.println("SPIFFS mounted successfully");
//#endif  
//}
////
//// Read File from SPIFFS
////
//String readFile(fs::FS &fs, const char * path)
//{
//#ifdef VERBOSE
//  Serial.printf("Reading file: %s\r\n", path);
//#endif
//
//  File file = fs.open(path);
//  if(!file || file.isDirectory())
//  {
//#ifdef VERBOSE
//    Serial.println("- failed to open file for reading");
//#endif
//    return String();
//  }
//  
//  String fileContent;
//  while(file.available()){
//    fileContent = file.readStringUntil('\n');
//    break;     
//  }
//  return fileContent;
//}
////
//// Write file to SPIFFS
////
//void writeFile(fs::FS &fs, const char * path, const char * message)
//{
//#ifdef VERBOSE
//  Serial.printf("Writing file: %s\r\n", path);
//#endif
//
//  File file = fs.open(path, FILE_WRITE);
//  if(!file)
//  {
//#ifdef VERBOSE
//    Serial.println("- failed to open file for writing");
//#endif
//    return;
//  }
//#ifdef VERBOSE
//  if(file.print(message))
//  {
//    Serial.println("- file written");
//  }
//  else 
//  {
//    Serial.println("- file write failed");
//  }
//#endif
//}
//
// Initialize WiFi
//
bool initWiFi() 
{
  if(ssid=="" || ip=="")
  {
#ifdef VERBOSE
    Serial.println("Undefined SSID or IP address.");
#endif
    return false;
  }

  WiFi.mode(WIFI_STA);

#ifdef VERBOSE
  Serial.printf("Connecting to WiFi %s ", ssid.c_str());
#endif

  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while(WiFi.status() != WL_CONNECTED) 
  {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) 
    {
#ifdef VERBOSE
      Serial.print(".");
#endif
      return false;
    }
  }

#ifdef VERBOSE
  Serial.println("Connected!");
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("Local: ");
  Serial.println(WiFi.localIP());
  Serial.print("Subnet: ");
  Serial.println(WiFi.subnetMask());
#endif
  sprintf(wifiState, "IP %s", WiFi.localIP().toString().c_str()); 
  // Init and get the time
  UpdateLocalTime();
  return true;
}
// 
// Replaces placeholder with sensor values
//
String processor(const String& var)
{
  if(var == "QUARTERHOUR")
  {
    return String(currentRainInches);
  }
  else if(var == "LASTHOUR")
  {
    return String(hourRainInches);
  }
  else if(var == "DAY")
  {
    return String(dayRainInches);
  }
  else if(var == "DATETIME")
  {
    return String(localTimeStr);
  }
  else
  {
    return "ERROR";
  }
  return String();
}
//
//
//
void setup() 
{
  // Serial port for debugging purposes
  Serial.begin(115200);
  delay(1000);
  
  sprintf(wifiState, "not connected");
  sprintf(mqttState, "not connected");
  sprintf(wundergroundState, "no attempts to update");

  /*****************************************************/
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
#ifdef VERBOSE
  Serial.println("");
  Serial.println("BBS Sept 2022");
  Serial.println("Rain gauge website host");
  Serial.println("=======================");
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
  // set up the rising edge interrupt for the rain sensor
  pinMode(RAINGAUGE_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RAINGAUGE_PIN), RainGaugeTrigger, FALLING);  
  /*****************************************************/

  // MQTT client name
  // YOU CANNOT DUPLICATE CLIENT NAMES!!!! (and expect them both to work anyway...)
  randomSeed(analogRead(0));
  mqttClientID += String(random(0xffff), HEX);
  sprintf(mqtt_user,"openhabian");
  sprintf(mqtt_password,"SJnu12HMo");

  // subscribed and published MQTT topics
  published_topic[0] =   "Rain_Gauge/Date";
  published_topic[1] =   "Rain_Gauge/pastQuarterHour";
  published_topic[2] =   "Rain_Gauge/pastHour";
  published_topic[3] =   "Rain_Gauge/past24Hours";
  published_topic[4] =   "Rain_Gauge";

  subscribed_topic[0] = "MQTT_Sensors/TestMQTTValue";
  
  published_count = 5;
  subscribed_count = 1;
  
#ifdef VERBOSE
  Serial.print("MQTT Server: ");
  Serial.println(mqttServer);
  Serial.print("MQTT Port: ");
  Serial.println(mqtt_portVal);
#endif
  mqttClient.setServer(mqttServer, mqtt_portVal);
  mqttClient.setCallback(MQTT_Callback);
  
  //initSPIFFS();
  
  // Load values saved in SPIFFS
  ssid = "FamilyRoom";     //readFile(SPIFFS, ssidPath);
  pass = "ZoeyDora48375";  //readFile(SPIFFS, passPath);
  ip = "192.168..200";     //readFile(SPIFFS, ipPath);
  gateway = "192.168.1.1"; //readFile (SPIFFS, gatewayPath);

  #ifdef VERBOSE
  int freq = getCpuFrequencyMhz();
  Serial.printf("Default Freq\n");
  Serial.printf("CPU Freq = %dMhz\n", freq);
  freq = getXtalFrequencyMhz();
  Serial.printf("XTAL Freq = %dMhz\n", freq);
  freq = getApbFrequency();
  Serial.printf("APB Freq = %dHz\n", freq);
  #endif
  
  //setCpuFrequencyMhz(80);
  setCpuFrequencyMhz(80);
  #ifdef VERBOSE
  Serial.printf("Updated Freq\n");
  freq = getCpuFrequencyMhz();
  Serial.printf("CPU Freq = %dMhz\n", freq);
  freq = getXtalFrequencyMhz();
  Serial.printf("XTAL Freq = %dMhz\n", freq);
  freq = getApbFrequency();
  Serial.printf("APB Freq = %dHz\n", freq);
  #endif
  btStop();
  #ifdef VERBOSE
  Serial.printf("Bluetooth disabled\n");
  #endif  
  // wifi connected
  if(initWiFi()) 
  {
//   // Web Server Root URL
//    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
//    {
//      request->send(SPIFFS, "/index.html", "text/html");
//    });    
//    server.on("/quarterHour", HTTP_GET, [](AsyncWebServerRequest *request){
//       request->send_P(200, "text/plain", String(quarterHourRainInches).c_str());
//    });
//    server.on("/day", HTTP_GET, [](AsyncWebServerRequest *request){
//       request->send_P(200, "text/plain", String(dayRainInches).c_str());
//    });
//    server.on("/lastHour", HTTP_GET, [](AsyncWebServerRequest *request){
//       request->send_P(200, "text/plain", String(hourRainInches).c_str());
//    });
//    server.on("/dateTime", HTTP_GET, [](AsyncWebServerRequest *request){
//       UpdateLocalTime();
//       request->send_P(200, "text/plain", String(localTimeStr).c_str());
//    });
  // Start server
    server.begin();
//
//    //rtc.offset = gmtOffset_sec;
  }
 //  // wifi not connected  
//  else 
//  {
//    while(true)
//    {
//    // Connect to Wi-Fi network with SSID and password
//#ifdef VERBOSE
//    Serial.println("Setting AP (Access Point)");
//#endif
//    // NULL sets an open Access Point
//    WiFi.softAP("ESP-WIFI-MANAGER", NULL);
//
//    IPAddress IP = WiFi.softAPIP();
//#ifdef VERBOSE
//    Serial.print("AP IP address: ");
//    Serial.println(IP); 
//#endif
//    // Web Server Root URL
//    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
//    {
//      request->send(SPIFFS, "/wifimanager.html", "text/html");
//    });
//    
//    server.serveStatic("/", SPIFFS, "/");
//    
//    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) 
//    {
//      int params = request->params();
//      for(int i=0;i<params;i++){
//        AsyncWebParameter* p = request->getParam(i);
//        if(p->isPost()){
//          // HTTP POST ssid value
//          if (p->name() == PARAM_INPUT_1) {
//            ssid = p->value().c_str();
//#ifdef VERBOSE
//            Serial.print("SSID set to: ");
//            Serial.println(ssid);
//#endif
//            // Write file to save value
//            writeFile(SPIFFS, ssidPath, ssid.c_str());
//          }
//          // HTTP POST pass value
//          if (p->name() == PARAM_INPUT_2) {
//            pass = p->value().c_str();
//#ifdef VERBOSE
//            Serial.print("Password set to: ");
//            Serial.println(pass);
//#endif
//            // Write file to save value
//            writeFile(SPIFFS, passPath, pass.c_str());
//          }
//          // HTTP POST ip value
//          if (p->name() == PARAM_INPUT_3) {
//            ip = p->value().c_str();
//#ifdef VERBOSE            
//            Serial.print("IP Address set to: ");
//            Serial.println(ip);
//#endif
//            // Write file to save value
//            writeFile(SPIFFS, ipPath, ip.c_str());
//          }
//          // HTTP POST gateway value
//          if (p->name() == PARAM_INPUT_4) {
//            gateway = p->value().c_str();
//#ifdef VERBOSE
//            Serial.print("Gateway set to: ");
//            Serial.println(gateway);
//#endif
//            // Write file to save value
//            writeFile(SPIFFS, gatewayPath, gateway.c_str());
//          }
//        }
//      }
//      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
//      delay(3000);
//      ESP.restart();
//    });
//      #ifdef VERBOSE
//      Serial.print(".");
//      #endif
//      delay(1000);
//    }
    //server.begin();
    //delay(2000);  
    //UpdateLocalTime();
//  }
}
//
//
//
void loop()
{
   mqttClient.loop();
   yield();

  // rain gauge click
  if(clicked)
  {
    clicked = false;
    #ifdef VERBOSE
    Serial.println(">>>>>click<<<<<");
    #endif
    rainByMinute[rainByMinuteIdx]++;
    rainByHour[rainByHourIdx]++;
    rainByDay[rainByDayIdx]++;
    totalTicks++;
  }

  // time to update?
  if((neverUpdated == true) ||
     ((millis() - lastUpdate) >= INTERVAL_MS))
  {
    #ifdef VERBOSE
    Serial.println("\n\nUpdate values");    
    #endif
    // get time
    UpdateLocalTime();
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
    currentTime = millis();
    

    // get next rain by minute index in circular buffer, reset rain clicks to zero for upcoming minute 
    rainByMinuteIdx = rainByMinuteIdx + 1 < MIN_PER_DAY ? rainByMinuteIdx + 1 : 0;
    rainByMinute[rainByMinuteIdx] = 0;

    // get next rain by hour index, reset rain clicks
    // replace with check for hour change, update rainByHourIdx
    if((rainByMinuteIdx % 60) == 0)
    {
      rainByHourIdx = rainByHourIdx + 1 < LAST_X_HOURS ? rainByHourIdx + 1 : 0;
      rainByHour[rainByHourIdx] = 0;
    }
    // get next rain by day index, reset rain clicks
    // replace with check for hour change, update rainByHourIdx
    if(rainByMinuteIdx == 0)
    {
      rainByDayIdx = rainByDayIdx + 1 < LAST_X_DAYS ? rainByDayIdx + 1 : 0;
      rainByDay[rainByDayIdx] = 0;
    }
    //
    // only update on max update time exceeded or any rain amount changed
    //
    if((neverUpdated) ||
       (quarterHourRainInches != priorQuarterHourRainInches) ||
       (hourRainInches != priorHourRainInches) ||
       (dayRainInches != priorDayRainInches) ||
       ((millis() - lastForceUpdate) >= MAX_UPDATE_INTERVAL_MS))
    {
      sprintf(payloadStr,"Connected %s | WiFi: %s | Weather Underground: %s",connectDateTime, 
                                                                             wifiState, 
                                                                             wundergroundState);
      #ifdef VERBOSE
      Serial.println(payloadStr);
      Serial.println(">>>>>Update OpenHAB<<<<<");    
      #endif
      // update last reported values
      priorQuarterHourRainInches = quarterHourRainInches;
      priorHourRainInches = hourRainInches;
      priorDayRainInches = dayRainInches;
      //
      // MQTT update
      //    
      published_payload[0] = String(localTimeStr);
      published_payload[1] = String(quarterHourRainInches, 4);
      published_payload[2] = String(hourRainInches, 4);
      published_payload[3] = String(dayRainInches, 4);
      published_payload[4] = String(payloadStr);
      digitalWrite(LED_PIN, HIGH);
      MQTT_PublishTopics();    
      //
      // Weather Underground update
      //
      // Set up the generic use-every-time part of the URL
      String url = "/weatherstation/updateweatherstation.php";
      url += "?ID=";
      url += WUndergroundID;
      url += "&PASSWORD=";
      url += WUndergroundKey;
      url += "&dateutc=now&action=updateraw";

      // Now let's add in the data that we've collected from our sensors
      // Start with rain in last hour/day
      url += "&rainin=";
      url += hourRainInches;
      url += "&dailyrainin=";
      url += dayRainInches;
      #ifdef WEATHER_UNDERGROUND
      // Connnect to Weather Underground. If the connection fails, return from
      //  loop and start over again.
      #ifdef VERBOSE      
      Serial.println(">>>>>Update Weather Underground<<<<<");    
      #endif
      if (!wifiClient.connect(host, 80))
      {
        #ifdef VERBOSE      
        Serial.println("Weather Underground connection failed");
        #endif
        sprintf(wundergroundState, "Not Connected");
        return;
      }
      else
      {
        #ifdef VERBOSE      
        Serial.println("Weather Underground connected");
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
          Serial.println(">>> Client Timeout !");
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
        //#ifdef VERBOSE      
        //Serial.print(line);
        //#endif      
        sprintf(wundergroundResponse, "%s", line);          
      }
      sprintf(wundergroundState, "%s %s", wundergroundState, wundergroundResponse);          

      #ifdef VERBOSE      
      Serial.println(wundergroundResponse);
      #endif      
      #endif
      lastForceUpdate = millis();
      digitalWrite(LED_PIN, LOW);
    }
  else
  {
      #ifdef VERBOSE
      Serial.printf("no change, no update required ");    
      #endif
  }
    neverUpdated = false;
    // set last update time
    lastUpdate = millis();
    #ifdef VERBOSE
    Serial.printf("time %d next update check at %d next forced update %d\n", millis(), (lastUpdate + INTERVAL_MS), (lastForceUpdate + MAX_UPDATE_INTERVAL_MS));    
    #endif
  }
}
//****************************************************************************
//
// (re)connect to MQTT server
//
//****************************************************************************
void MQTT_Reconnect() 
{
  // Loop until we're reconnected
  int blinkVal = HIGH;
  int attemptCount = 0;
  bool mqttConnect = true;
  while (!mqttClient.connected() && (attemptCount < 10)) 
  {
    mqttConnect = false;
#ifdef VERBOSE
    Serial.print("MQTT client ");
    Serial.print(mqttClientID.c_str());
    Serial.print(" user: ");
    Serial.print(mqtt_user);
    Serial.print(" password: ");
    Serial.print(mqtt_password);
    Serial.print(" attempts ");
    Serial.println(attemptCount);
#endif
    // connected, subscribe and publish
    if (mqttClient.connect(mqttClientID.c_str(), mqtt_user, mqtt_password)) 
    {
      // set up listeners for server updates  
      MQTT_SubscribeTopics();
    }
    else 
    {
      // Wait before retrying
      delay(RECONNECT_DELAY);
    }
    attemptCount++;
  }
  delay(RECONNECT_DELAY);
  if(mqttClient.connected())
  {
    mqttConnect = true;
  }
  if(mqttConnect)
  {
#ifdef VERBOSE
    Serial.println("MQTT connected!");
#endif
    sprintf(mqttState, "%s connected", mqttClientID.c_str());
  }
  else
  {
#ifdef VERBOSE
    Serial.print("MQTT not connected - ");
    Serial.print(attemptCount);
    Serial.print(" attempts");
    Serial.println(mqttClientID.c_str());
#endif
    sprintf(mqttState, "not connected after %d attempts", attemptCount);
  }

  // we're connected, indicator on 
  delay(2000);
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
  Serial.print("MQTT_Callback ");
  Serial.print(topic);
  Serial.print(" payload  ");
  Serial.println(payloadStr);
#endif
}
//
// subscribed MQTT topics (get updates from MQTT server)
//
void MQTT_SubscribeTopics()
{  
  for(int idx = 0; idx < subscribed_count; idx++)
  {
    mqttClient.subscribe(subscribed_topic[idx].c_str());
  }
}
//
// published MQTT topics (send updates to MQTT server)
// if multiple topics could be published, it might be good to either pass in the specific name
// or make separate publish functions
//
void MQTT_PublishTopics()
{
  if (!mqttClient.connected())
  {
#ifdef VERBOSE
    UpdateLocalTime();
    Serial.print("MQTT not connected at ");
    Serial.println(localTimeStr);
#endif
    MQTT_Reconnect();
  }
  //
  int pubSubResult = 0;
  for(int idx = 0; idx < published_count; idx++)
  {
    pubSubResult = mqttClient.publish(published_topic[idx].c_str(), published_payload[idx].c_str());
#ifdef VERBOSE
    Serial.print("Publishing ");
    Serial.print(published_topic[idx]);
    Serial.print(" ");
    Serial.print(published_payload[idx]);
    Serial.print(" ");
    Serial.println((pubSubResult == 0 ? "FAIL" : "SUCCESS"));
#endif
  }
}
