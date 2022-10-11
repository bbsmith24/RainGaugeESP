/*********
  Rui Santos
  Complete project details at https://randomnerdtutorials.com/esp8266-dht11dht22-temperature-and-humidity-web-server-with-arduino-ide/
*********/

// Import required libraries
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <time.h>
//#include <Arduino.h>
//#include <ESP8266WiFi.h>
//#include <Hash.h>
//#include <ESPAsyncTCP.h>
//#include <ESPAsyncWebServer.h>

// Replace with your network credentials
const char* ssid = "FamilyRoom";
const char* password = "ZoeyDora48375";
//const char* ssid = "NoviDems_Guest";
//const char* password = "NoviDems48375";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
/*****************************************/
ICACHE_RAM_ATTR void RainGaugeTrigger();
// 
// function prototypes
//
void ConnectToWiFi(const char *, const char *);
void ScanWiFi();
void UpdateLocalTime();

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

// Set your Static IP address
IPAddress local_IP(192, 168, 1, 143);
// Set your Gateway IP address
IPAddress gateway(192, 168, 1, 1);

IPAddress subnet(255, 255, 0, 0);
IPAddress primaryDNS(8, 8, 8, 8);   //optional
IPAddress secondaryDNS(8, 8, 4, 4); //optional
/*****************************************/

// Generally, you should use "unsigned long" for variables that hold time
// The value will quickly become too large for an int to store
unsigned long previousMillis = 0;    // will store last time DHT was updated

// Updates every 10 seconds
const long interval = 10000;  

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

// Replaces placeholder with sensor values
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
  delay(1000);
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

  // scan available wifi
  ScanWiFi();
  // Connect to Wi-Fi
  ConnectToWiFi(ssid, password);
/*  
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi");
  int attemptCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    attemptCount++;
    if(attemptCount % 10 == 0)
    {
      Serial.println();
    }
  }
  Serial.println();
  Serial.print("Connected as ");
  Serial.println(WiFi.localIP());
*/
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
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

  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

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
    if (WiFi.status() != WL_CONNECTED) 
    {
      // Connect to Wi-Fi
      ConnectToWiFi(ssid, password);
    }
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
//
//
//
void ConnectToWiFi(const char * ssid, const char * pwd)
{
  int ledState = 0;

  Serial.println("\nConnecting to WiFi network: " + String(ssid) + " >" + String(password) + "<");
  WiFi.mode(WIFI_STA);

  // Configures static IP address
  //if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
  //  Serial.println("STA Failed to configure");
  //}
  String hostName = "ESP32 Weather";
  WiFi.setHostname(hostName.c_str());
    
  WiFi.begin(ssid, pwd);
  delay(1000);
  int countAttempts = 1;
  while (WiFi.status() != WL_CONNECTED) 
  {
    Serial.print(".");
    if(countAttempts % 50 == 0)
    {
      Serial.println();
    }
    countAttempts++;
    // Blink LED while we're connecting:
    digitalWrite(LED_PIN, ledState);
    ledState = (ledState + 1) % 2; // Flip ledState
    delay(1000);
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}
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
