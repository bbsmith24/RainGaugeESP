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

IPAddress localIP;
//IPAddress localIP(192, 168, 1, 200); // hardcoded

// Set your Gateway IP address
IPAddress localGateway;
//IPAddress localGateway(192, 168, 1, 1); //hardcoded
IPAddress subnet(255, 255, 0, 0);

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
//#define VERBOSE
// for devkit c
#define RAINGAUGE_PIN 13 
#define LED_PIN 2
// for sparkfun esp32 thing plus
//#define RAINGAUGE_PIN 5  
//#define LED_PIN 13

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
// Set LED GPIO
//const int ledPin = 2;
// Stores LED state

//String ledState;
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
    Serial.printf("LOW %d", micros());
  }
}
//
// scan for wifi access points
//
void ScanWiFi()
{
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
}
//
//
//
void UpdateLocalTime()
{
  struct tm timeinfo;
  getLocalTime(&timeinfo);

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
  //Get hour (12 hour format)
  /*char hour[4];
  strftime(hour, sizeof(hour), "%I", &timeinfo);*/
  
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
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
    Serial.println("- failed to open file for reading");
    return String();
  }
  
  String fileContent;
  while(file.available()){
    fileContent = file.readStringUntil('\n');
    break;     
  }
  return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- frite failed");
  }
}

// Initialize WiFi
bool initWiFi() {
  if(ssid=="" || ip==""){
    Serial.println("Undefined SSID or IP address.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  localIP.fromString(ip.c_str());
  localGateway.fromString(gateway.c_str());


  if (!WiFi.config(localIP, localGateway, subnet)){
    Serial.println("STA Failed to configure");
    return false;
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while(WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      Serial.println("Failed to connect.");
      return false;
    }
  }

  Serial.println(WiFi.localIP());
  return true;
}
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <style>
    html {
     font-family: Arial;
     display: inline-block;
     margin: 0px auto;
     text-align: start;
    }
    h2 { font-size: 3.0rem; }
    p { font-size: 3.0rem; }
    .units { font-size: 1.2rem; }
    .gaugeLabels{
      font-size: 1.5rem;
      vertical-align:middle;
      padding-bottom: 15px;
    }
  </style>
</head>
<body>
  <h2>Rain Gauge ESP32</h2>
  <p>
    <i class="fas fa-tint" style="color:#00add6;"></i> 
    <span class="gaugeLabels">15 Minutes</span> 
    <span id="quarterHour">%QUARTERHOUR%</span>
    <sup class="units">in</sup>
  </p>
  <p>
    <i class="fas fa-tint" style="color:#00add6;"></i> 
    <span class="gaugeLabels">1 Hour</span>
    <span id="lastHour">%LASTHOUR%</span>
    <sup class="units">in</sup>
  </p>
  <p>
    <i class="fas fa-tint" style="color:#00add6;"></i> 
    <span class="gaugeLabels">24 Hours</span>
    <span id="day">%DAY%</span>
    <sup class="units">in</sup>
  </p>
  <p>
    <i class="fas fa-thin fa-clock" style="color:#00add6;"></i> 
    <span class="gaugeLabels"></span>
    <span id="dateTime">%DATETIME%</span>
    <sup class="units"></sup>
  </p>
</body>
<script>
setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("quarterHour").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/quarterHour", true);
  xhttp.send();
}, 10000 );

setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("day").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/day", true);
  xhttp.send();
}, 10000 );

setInterval(function ( ) 
{
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("lastHour").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/lastHour", true);
  xhttp.send();
}, 10000 );

setInterval(function ( ) 
{
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("dateTime").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/dateTime", true);
  xhttp.send();
}, 10000 );
</script>
</html>)rawliteral";

// 
// Replaces placeholder with sensor values
//
String processor(const String& var)
{
  //Serial.println(var);
  if(var == "QUARTERHOUR"){
    return String(currentRainInches);
  }
  else if(var == "LASTHOUR"){
    return String(lastHourRainInches);
  }
  else if(var == "DAY"){
    return String(lastDayRainInches);
  }
  else if(var == "DATETIME"){
    return String(localTimeStr);
  }
  return String();
}

void setup() 
{
  // Serial port for debugging purposes
  Serial.begin(115200);

  /*****************************************************/
  pinMode(LED_PIN, OUTPUT);
  Serial.println("BBS Sept 2022");
  Serial.println("Rain gauge website host");
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

  initSPIFFS();
  
  // Load values saved in SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  ip = readFile(SPIFFS, ipPath);
  gateway = readFile (SPIFFS, gatewayPath);
  Serial.println(ssid);
  Serial.println(pass);
  Serial.println(ip);
  Serial.println(gateway);

  if(initWiFi()) 
  {
    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) 
    {
      request->send(SPIFFS, "/index.html", "text/html", false, processor);
    });
    server.serveStatic("/", SPIFFS, "/");
    

    server.begin();
  }
  else 
  {
    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP("ESP-WIFI-MANAGER", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP); 

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
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Write file to save value
            writeFile(SPIFFS, ssidPath, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Write file to save value
            writeFile(SPIFFS, passPath, pass.c_str());
          }
          // HTTP POST ip value
          if (p->name() == PARAM_INPUT_3) {
            ip = p->value().c_str();
            Serial.print("IP Address set to: ");
            Serial.println(ip);
            // Write file to save value
            writeFile(SPIFFS, ipPath, ip.c_str());
          }
          // HTTP POST gateway value
          if (p->name() == PARAM_INPUT_4) {
            gateway = p->value().c_str();
            Serial.print("Gateway set to: ");
            Serial.println(gateway);
            // Write file to save value
            writeFile(SPIFFS, gatewayPath, gateway.c_str());
          }
          //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
      }
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      delay(3000);
      ESP.restart();
    });
    server.begin();
  }
}

void loop()
{
  // rain gauge click
  if(clicked)
  {
    clicked = false;
    digitalWrite(LED_PIN, ledState);
    Serial.println(">>>>>click<<<<<");
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
    Serial.printf("Date %s\tTicks %d\t\tRain\t15 minutes %0.4f\thour %0.4f\tDay %0.4f\n", localTimeStr, 
                                                                                          totalTicks, 
                                                                                          lastQuarterRainInches, 
                                                                                          lastHourRainInches, 
                                                                                          lastDayRainInches);
#ifdef VERBOSE
    Serial.printf("\t  Hour  \t   Day   \n");
    Serial.printf("\t========\t=========\n");
    int limit = LAST_X_DAYS < LAST_X_HOURS ? LAST_X_DAYS : LAST_X_HOURS;
    for(int idx = 0; idx < limit; idx++)
    {
      if(idx < LAST_X_DAYS)
      {
        Serial.printf("\t%0.4f\t", (float)rainByHour[idx] * INCHES_PER_CLICK);
        if(idx == rainByHourIdx)
        {
          Serial.print("*");
        }
      }
      else
      {
        Serial.print("\t\t");
      }
      if(idx < LAST_X_DAYS)
      {
        Serial.printf("\t%0.4f\t", (float)rainByDay[idx] * INCHES_PER_CLICK);
        if(idx == rainByDayIdx)
        {
          Serial.print("*");
        }
      }
      Serial.println();
    }
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
  }
}
