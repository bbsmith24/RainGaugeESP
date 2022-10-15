/*********
  Rui Santos
  Complete instructions at https://RandomNerdTutorials.com/esp32-wi-fi-manager-asyncwebserver/
  
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*********/

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "SPIFFS.h"
#include <time.h>
#include <ESP32Ping.h>
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>

//#define VERBOSE

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Search for parameter in HTTP POST request
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
const char* PARAM_INPUT_3 = "ip";
const char* PARAM_INPUT_4 = "gateway";


//Variables to save values from HTML form
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

char localTimeStr[256];

#define MIN_PER_DAY 1440
#define LAST_X_DAYS 28
#define LAST_X_HOURS 24
#define INCHES_PER_CLICK 0.011F
#define INTERVAL_MS 60000  // 1 minute interval
// for devkit c
#define RAINGAUGE_PIN 13 
#define LED_PIN 2
// for sparkfun esp32 thing plus
//#define RAINGAUGE_PIN 5  
//#define LED_PIN 13
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
float lastHourRainInches = 0.0;
float lastQuarterRainInches = 0.0;
float lastDayRainInches = 0.0;
unsigned long lastUpdateMillis;
int ledState = 1;
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
//
// counter (pin low->high) interrupt handler
// add 1 count to 
//    current minute
//    past 24 hours
//    day
//
ICACHE_RAM_ATTR void RainGaugeTrigger()
{
  if(digitalRead(RAINGAUGE_PIN) == LOW)
  {
    clicked = true;
#ifdef VERBOSE
    Serial.printf("LOW %d", micros());
#endif
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
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    // Init timeserver
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    getLocalTime(&timeinfo);
  }
  //GET DATE
  //Get full weekday name
  char weekDay[10];
  strftime(weekDay, sizeof(weekDay), "%a", &timeinfo);
  //Get day of month
  char dayMonth[4];
  strftime(dayMonth, sizeof(dayMonth), "%d", &timeinfo);
  //Get abbreviated month name
  char monthName[5];
  strftime(monthName, sizeof(monthName), "%b", &timeinfo);
  //Get year
  char year[6];
  strftime(year, sizeof(year), "%Y", &timeinfo);

  //GET TIME
  //Get hour (24 hour format)
  char hour[4];
  strftime(hour, sizeof(hour), "%H", &timeinfo);
  //Get minute
  char minute[4];
  strftime(minute, sizeof(minute), "%M", &timeinfo);
  char second[4];
  strftime(second, sizeof(second), "%S", &timeinfo);

  sprintf(localTimeStr, "%s %s %s %s %s:%s", weekDay, monthName, dayMonth, year, hour, minute);
}
//
// Initialize SPIFFS
//
void initSPIFFS() 
{
  if (!SPIFFS.begin(true)) 
  {
#ifdef VERBOSE
    Serial.println("An error has occurred while mounting SPIFFS");
#endif
  }
#ifdef VERBOSE  
  Serial.println("SPIFFS mounted successfully");
#endif  
}
//
// Read File from SPIFFS
//
String readFile(fs::FS &fs, const char * path)
{
#ifdef VERBOSE
  Serial.printf("Reading file: %s\r\n", path);
#endif

  File file = fs.open(path);
  if(!file || file.isDirectory())
  {
#ifdef VERBOSE
    Serial.println("- failed to open file for reading");
#endif
    return String();
  }
  
  String fileContent;
  while(file.available()){
    fileContent = file.readStringUntil('\n');
    break;     
  }
  return fileContent;
}
//
// Write file to SPIFFS
//
void writeFile(fs::FS &fs, const char * path, const char * message)
{
#ifdef VERBOSE
  Serial.printf("Writing file: %s\r\n", path);
#endif

  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
#ifdef VERBOSE
    Serial.println("- failed to open file for writing");
#endif
    return;
  }
#ifdef VERBOSE
  if(file.print(message))
  {
    Serial.println("- file written");
  }
  else 
  {
    Serial.println("- file write failed");
  }
#endif
}
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

  ScanWiFi();

  WiFi.begin(ssid.c_str(), pass.c_str());
#ifdef VERBOSE
  Serial.println("Connecting to WiFi...");
#endif

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while(WiFi.status() != WL_CONNECTED) 
  {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) 
    {
#ifdef VERBOSE
      Serial.println("Failed to connect.");
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

#ifdef DEBUG_WIFI 
  char remote_host[512];
  sprintf(remote_host, "www.google.com");
  while(true)
  {
    if (Ping.ping(remote_host)) 
    {
#ifdef VERBOSE
      Serial.print("Ping GOOGLE ");
      Serial.print(remote_host);
      Serial.println(" SUCCESS!");
#endif      
      break;
    } 
    else 
    {
#ifdef VERBOSE
      Serial.print("Ping GOOGLE ");
      Serial.print(remote_host);
      Serial.println(" FAIL!");
#endif
      delay(500);
    }
  }
  sprintf(remote_host, "192.168.1.76");
  while(true)
  {
    if (Ping.ping(remote_host)) 
    {
#ifdef VERBOSE      
      Serial.print("Ping OpenHab ");
      Serial.print(remote_host);
      Serial.println(" SUCCESS!");
#endif
      break;
    } 
    else 
    {
#ifdef VERBOSE
      Serial.print("Ping OpenHab ");
      Serial.print(remote_host);
      Serial.println(" FAIL!");
#endif
      delay(500);
    }
  }
#endif
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
    return String(lastHourRainInches);
  }
  else if(var == "DAY")
  {
    return String(lastDayRainInches);
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

  /*****************************************************/
  pinMode(LED_PIN, OUTPUT);
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
  pinMode(RAINGAUGE_PIN, INPUT);
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
  
  initSPIFFS();
  
  // Load values saved in SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  ip = readFile(SPIFFS, ipPath);
  gateway = readFile (SPIFFS, gatewayPath);
  if(initWiFi()) 
  {
    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    {
      request->send(SPIFFS, "/index.html", "text/html");
    });    
     server.on("/quarterHour", HTTP_GET, [](AsyncWebServerRequest *request){
       request->send_P(200, "text/plain", String(lastQuarterRainInches).c_str());
     });
     server.on("/day", HTTP_GET, [](AsyncWebServerRequest *request){
       request->send_P(200, "text/plain", String(lastDayRainInches).c_str());
     });
     server.on("/lastHour", HTTP_GET, [](AsyncWebServerRequest *request){
       request->send_P(200, "text/plain", String(lastHourRainInches).c_str());
     });
     server.on("/dateTime", HTTP_GET, [](AsyncWebServerRequest *request){
       request->send_P(200, "text/plain", String(localTimeStr).c_str());
     });

  // Start server
    server.begin();

    UpdateLocalTime();
  }
  else 
  {
    // Connect to Wi-Fi network with SSID and password
#ifdef VERBOSE
    Serial.println("Setting AP (Access Point)");
#endif
    // NULL sets an open Access Point
    WiFi.softAP("ESP-WIFI-MANAGER", NULL);

    IPAddress IP = WiFi.softAPIP();
#ifdef VERBOSE
    Serial.print("AP IP address: ");
    Serial.println(IP); 
#endif
    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    {
      request->send(SPIFFS, "/wifimanager.html", "text/html");
    });
    
    server.serveStatic("/", SPIFFS, "/");
    
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) 
    {
      int params = request->params();
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
          // HTTP POST ssid value
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
#ifdef VERBOSE
            Serial.print("SSID set to: ");
            Serial.println(ssid);
#endif
            // Write file to save value
            writeFile(SPIFFS, ssidPath, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
#ifdef VERBOSE
            Serial.print("Password set to: ");
            Serial.println(pass);
#endif
            // Write file to save value
            writeFile(SPIFFS, passPath, pass.c_str());
          }
          // HTTP POST ip value
          if (p->name() == PARAM_INPUT_3) {
            ip = p->value().c_str();
#ifdef VERBOSE            
            Serial.print("IP Address set to: ");
            Serial.println(ip);
#endif
            // Write file to save value
            writeFile(SPIFFS, ipPath, ip.c_str());
          }
          // HTTP POST gateway value
          if (p->name() == PARAM_INPUT_4) {
            gateway = p->value().c_str();
#ifdef VERBOSE
            Serial.print("Gateway set to: ");
            Serial.println(gateway);
#endif
            // Write file to save value
            writeFile(SPIFFS, gatewayPath, gateway.c_str());
          }
        }
      }
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      delay(3000);
      ESP.restart();
    });
    server.begin();
    delay(2000);  
    UpdateLocalTime();
  }
}
//
//
//
void loop()
{
   mqttClient.loop();

  // rain gauge click
  if(clicked)
  {
    clicked = false;
    digitalWrite(LED_PIN, ledState);
#ifdef VERBOSE
    Serial.println(">>>>>click<<<<<");
#endif
    ledState = !ledState;
    rainByMinute[rainByMinuteIdx]++;
    rainByHour[rainByHourIdx]++;
    rainByDay[rainByDayIdx]++;
    totalTicks++;
  }

  // one minute updates
  if((millis() - lastUpdateMillis) >= INTERVAL_MS)
  {
    lastUpdateMillis = millis();
    // check for wifi
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
    lastQuarterRainInches = (float)rainForLastQuarter * INCHES_PER_CLICK;
    lastDayRainInches = (float)rainForLastDay * INCHES_PER_CLICK;
    lastHourRainInches = (float)rainForLastHour * INCHES_PER_CLICK;
    currentTime = millis();
    
    sprintf(payloadStr,"%s\t%0.4f\t%0.4f\t%0.4f", localTimeStr, 
                                                   lastQuarterRainInches, 
                                                   lastHourRainInches, 
                                                   lastDayRainInches);
#ifdef VERBOSE
    Serial.println(payloadStr);
#endif

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
    published_payload[0] = String(localTimeStr);
    published_payload[1] = String(lastQuarterRainInches, 4);
    published_payload[2] = String(lastHourRainInches, 4);
    published_payload[3] = String(lastDayRainInches, 4);
    published_payload[4] = String(payloadStr);

    MQTT_PublishTopics();    
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
  while (!mqttClient.connected()) 
  {
#ifdef VERBOSE
    Serial.print("mqttClientID: ");
    Serial.println(mqttClientID);
    Serial.print("mqtt_user: ");
    Serial.println(mqtt_user);
    Serial.print("mqtt_password: ");
    Serial.println(mqtt_password);
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
#ifdef VERBOSE
    Serial.print("MQTT connect attempt as ");
    Serial.println(mqttClientID.c_str());
    #endif
  }
#ifdef VERBOSE
  Serial.print("MQTT connected as ");
  Serial.println(mqttClientID.c_str());
#endif
  mqttClient.publish("Rain_Gauge", "10/14/2022 7:10:00");

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
    UpdateLocalTime();
#ifdef VERBOSE
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
