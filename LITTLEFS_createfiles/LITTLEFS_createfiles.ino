#include "FS.h"
//#include "SPIFFS.h" 
#include "LITTLEFS.h"
#include <time.h> 
#include <WiFi.h>

#define SPIFFS LITTLEFS

/* This examples uses "quick re-define" of SPIFFS to run 
   an existing sketch with LITTLEFS instead of SPIFFS
   
   To get time/date stamps by file.getLastWrite(), you need an 
   esp32 core on IDF 3.3 and comment a line in file esp_littlefs.c:
   
   //#define CONFIG_LITTLEFS_FOR_IDF_3_2

   You only need to format LITTLEFS the first time you run a
   test or else use the LITTLEFS plugin to create a partition
   https://github.com/lorol/arduino-esp32littlefs-plugin */
   
#define FORMAT_LITTLEFS_IF_FAILED true

const char* ssid     = "FamilyRoom";
const char* password = "ZoeyDora48375";

long timezone = 1; 
byte daysavetime = 1;

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.print (file.name());
            time_t t= file.getLastWrite();
            struct tm * tmstruct = localtime(&t);
            Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n",(tmstruct->tm_year)+1900,( tmstruct->tm_mon)+1, tmstruct->tm_mday,tmstruct->tm_hour , tmstruct->tm_min, tmstruct->tm_sec);
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.print(file.size());
            time_t t= file.getLastWrite();
            struct tm * tmstruct = localtime(&t);
            Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n",(tmstruct->tm_year)+1900,( tmstruct->tm_mon)+1, tmstruct->tm_mday,tmstruct->tm_hour , tmstruct->tm_min, tmstruct->tm_sec);
        }
        file = root.openNextFile();
    }
}

void createDir(fs::FS &fs, const char * path){
    Serial.printf("Creating Dir: %s\n", path);
    if(fs.mkdir(path)){
        Serial.println("Dir created");
    } else {
        Serial.println("mkdir failed");
    }
}

void removeDir(fs::FS &fs, const char * path){
    Serial.printf("Removing Dir: %s\n", path);
    if(fs.rmdir(path)){
        Serial.println("Dir removed");
    } else {
        Serial.println("rmdir failed");
    }
}

void readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\n", path);

    File file = fs.open(path);
    if(!file){
        Serial.println("Failed to open file for reading");
        return;
    }

    Serial.print("Read from file: ");
    while(file.available()){
        Serial.write(file.read());
    }
    file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("Failed to open file for writing");
        return;
    }
    if(file.print(message)){
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
    file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Appending to file: %s\n", path);

    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("Failed to open file for appending");
        return;
    }
    if(file.print(message)){
        Serial.println("Message appended");
    } else {
        Serial.println("Append failed");
    }
    file.close();
}

void renameFile(fs::FS &fs, const char * path1, const char * path2){
    Serial.printf("Renaming file %s to %s\n", path1, path2);
    if (fs.rename(path1, path2)) {
        Serial.println("File renamed");
    } else {
        Serial.println("Rename failed");
    }
}

void deleteFile(fs::FS &fs, const char * path){
    Serial.printf("Deleting file: %s\n", path);
    if(fs.remove(path)){
        Serial.println("File deleted");
    } else {
        Serial.println("Delete failed");
    }
}

void setup(){
    Serial.begin(115200);
    // We start by connecting to a WiFi network
    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("Contacting Time Server");
	configTime(3600*timezone, daysavetime*3600, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
	struct tm tmstruct ;
    delay(2000);
    tmstruct.tm_year = 0;
    getLocalTime(&tmstruct, 5000);
	Serial.printf("\nNow is : %d-%02d-%02d %02d:%02d:%02d\n",(tmstruct.tm_year)+1900,( tmstruct.tm_mon)+1, tmstruct.tm_mday,tmstruct.tm_hour , tmstruct.tm_min, tmstruct.tm_sec);
    Serial.println("");
    
    if(!SPIFFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
        Serial.println("LITTLEFS Mount Failed");
        return;
    }

    Serial.println("----list 1----");
    listDir(SPIFFS, "/", 1);
	/*
    Serial.println("----remove old dir----");
    removeDir(SPIFFS, "/mydir");
	
    Serial.println("----create a new dir----");
    createDir(SPIFFS, "/mydir");
	
    Serial.println("----remove the new dir----");
    removeDir(SPIFFS, "/mydir");
	
    Serial.println("----create the new again----");
    createDir(SPIFFS, "/mydir");
	*/
    Serial.println("----create and work with file----");
    writeFile(SPIFFS, "/hello.txt", "Hello ");
    appendFile(SPIFFS, "/hello.txt", "World!\n");


    writeFile(SPIFFS, "/wifimanager.html", "<!DOCTYPE html>\n");
    appendFile(SPIFFS, "/wifimanager.html", "<html>\n");
    appendFile(SPIFFS, "/wifimanager.html", "<head>\n");
    appendFile(SPIFFS, "/wifimanager.html", "  <title>Wi-Fi Credentials</title>\n");
    appendFile(SPIFFS, "/wifimanager.html", "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n");
    appendFile(SPIFFS, "/wifimanager.html", "  <link rel=\"icon\" href=\"data:,\">\n");
    appendFile(SPIFFS, "/wifimanager.html", "  <link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">\n");
    appendFile(SPIFFS, "/wifimanager.html", "</head>\n");
    appendFile(SPIFFS, "/wifimanager.html", "<body>\n");
    appendFile(SPIFFS, "/wifimanager.html", "  <div class=\"topnav\">\n");
    appendFile(SPIFFS, "/wifimanager.html", "    <h1>Rain Gauge Credentials</h1>\n");
    appendFile(SPIFFS, "/wifimanager.html", "  </div>\n");
    appendFile(SPIFFS, "/wifimanager.html", "  <div class=\"content\">\n");
    appendFile(SPIFFS, "/wifimanager.html", "    <div class=\"card-grid\">\n");
    appendFile(SPIFFS, "/wifimanager.html", "      <div class=\"card\">\n");
    appendFile(SPIFFS, "/wifimanager.html", "        <form action=\"/\" method=\"POST\">\n");
    appendFile(SPIFFS, "/wifimanager.html", "          <p>\n");
    appendFile(SPIFFS, "/wifimanager.html", "			       <div>WiFi Credentials</div>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <label for=\"ssid\">SSID</label>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type=\"text\" id =\"ssid\" name=\"ssid\"><br>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <label for=\"pass\">Password</label>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type=\"text\" id =\"pass\" name=\"pass\"><br>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <label for=\"ip\">IP Address</label>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type=\"text\" id =\"ip\" name=\"ip\"><br>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <label for=\"gateway\">Gateway Address</label>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type=\"text\" id =\"gateway\" name=\"gateway\"><br>\n");
    appendFile(SPIFFS, "/wifimanager.html", "		         <hr/>\n");
    appendFile(SPIFFS, "/wifimanager.html", "			       <div>Time zone and DST offsets (hours)</div>\n");
    appendFile(SPIFFS, "/wifimanager.html", "			       <label for=\"tz\">Time Zone offset from GST</label>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type=\"text\" id =\"timezone\" name=\"timezone\"><br>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <label for=\"dst\">DST offset</label>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type=\"text\" id =\"dst\" name=\"dst\"><br>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <hr/>\n");
    appendFile(SPIFFS, "/wifimanager.html", "			       <div>MQTT setup</div>\n");
    appendFile(SPIFFS, "/wifimanager.html", "			       <label for=\"mqtt_serverIP\">MQTT server IP</label>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type=\"text\" id =\"mqtt_serverIP\" name=\"mqtt_serverIP\"><br>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <label for=\"mqtt_port\">MQTT port</label>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type=\"text\" id =\"mqtt_port\" name=\"mqtt_port\" value=\"1883\"><br>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <label for=\"mqtt_user\">MQTT user name</label>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type=\"text\" id =\"mqtt_user\" name=\"mqtt_user\"><br>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <label for=\"mqtt_password\">MQTT password</label>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type=\"text\" id =\"mqtt_password\" name=\"mqtt_password\"><br>\n");
    appendFile(SPIFFS, "/wifimanager.html", "			       <hr/>\n");
    appendFile(SPIFFS, "/wifimanager.html", "			       <div>Weather Underground setup</div>\n");
    appendFile(SPIFFS, "/wifimanager.html", "			       <label for=\"wu_ID\">Weather Underground ID</label>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type=\"text\" id =\"wu_ID\" name=\"wu_ID\"><br>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <label for=\"wu_Key\">Weather Underground Key</label>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type=\"text\" id =\"wu_Key\" name=\"wu_Key\"><br>\n");
    appendFile(SPIFFS, "/wifimanager.html", "			       <hr/>\n");
    appendFile(SPIFFS, "/wifimanager.html", "            <input type =\"submit\" value =\"Submit\">\n");
    appendFile(SPIFFS, "/wifimanager.html", "          </p>\n");
    appendFile(SPIFFS, "/wifimanager.html", "        </form>\n");
    appendFile(SPIFFS, "/wifimanager.html", "      </div>\n");
    appendFile(SPIFFS, "/wifimanager.html", "    </div>\n");
    appendFile(SPIFFS, "/wifimanager.html", "  </div>\n");
    appendFile(SPIFFS, "/wifimanager.html", "</body>\n");
    appendFile(SPIFFS, "/wifimanager.html", "</html>\n");

    writeFile(SPIFFS, "/style.css", "html {\n");
    appendFile(SPIFFS, "/style.css", "  font-family: Arial, Helvetica, sans-serif; \n");
    appendFile(SPIFFS, "/style.css", "  display: inline-block; \n");
    appendFile(SPIFFS, "/style.css", "  text-align: center;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", "\n");
    appendFile(SPIFFS, "/style.css", "h1 {\n");
    appendFile(SPIFFS, "/style.css", "  font-size: 1.8rem; \n");
    appendFile(SPIFFS, "/style.css", "  color: white;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", "\n");
    appendFile(SPIFFS, "/style.css", "p { \n");
    appendFile(SPIFFS, "/style.css", "  font-size: 1.4rem;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", "\n");
    appendFile(SPIFFS, "/style.css", ".topnav { \n");
    appendFile(SPIFFS, "/style.css", "  overflow: hidden; \n");
    appendFile(SPIFFS, "/style.css", "  background-color: #0A1128;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", "\n");
    appendFile(SPIFFS, "/style.css", "body {  \n");
    appendFile(SPIFFS, "/style.css", "  margin: 0;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", "\n");
    appendFile(SPIFFS, "/style.css", ".content { \n");
    appendFile(SPIFFS, "/style.css", "  padding: 5%;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", "\n");
    appendFile(SPIFFS, "/style.css", ".card-grid { \n");
    appendFile(SPIFFS, "/style.css", "  max-width: 800px; \n");
    appendFile(SPIFFS, "/style.css", "  margin: 0 auto; \n");
    appendFile(SPIFFS, "/style.css", "  display: grid; \n");
    appendFile(SPIFFS, "/style.css", "  grid-gap: 2rem; \n");
    appendFile(SPIFFS, "/style.css", "  grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", "\n");
    appendFile(SPIFFS, "/style.css", ".card { \n");
    appendFile(SPIFFS, "/style.css", "  background-color: white; \n");
    appendFile(SPIFFS, "/style.css", "  box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", "\n");
    appendFile(SPIFFS, "/style.css", ".card-title { \n");
    appendFile(SPIFFS, "/style.css", "  font-size: 1.2rem;\n");
    appendFile(SPIFFS, "/style.css", "  font-weight: bold;\n");
    appendFile(SPIFFS, "/style.css", "  color: #034078\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", "\n");
    appendFile(SPIFFS, "/style.css", "input[type=submit] {\n");
    appendFile(SPIFFS, "/style.css", "  border: none;\n");
    appendFile(SPIFFS, "/style.css", "  color: #FEFCFB;\n");
    appendFile(SPIFFS, "/style.css", "  background-color: #034078;\n");
    appendFile(SPIFFS, "/style.css", "  padding: 15px 15px;\n");
    appendFile(SPIFFS, "/style.css", "  text-align: center;\n");
    appendFile(SPIFFS, "/style.css", "  text-decoration: none;\n");
    appendFile(SPIFFS, "/style.css", "  display: inline-block;\n");
    appendFile(SPIFFS, "/style.css", "  font-size: 16px;\n");
    appendFile(SPIFFS, "/style.css", "  width: 100px;\n");
    appendFile(SPIFFS, "/style.css", "  margin-right: 10px;\n");
    appendFile(SPIFFS, "/style.css", "  border-radius: 4px;\n");
    appendFile(SPIFFS, "/style.css", "  transition-duration: 0.4s;\n");
    appendFile(SPIFFS, "/style.css", "  }\n");
    appendFile(SPIFFS, "/style.css", "\n");
    appendFile(SPIFFS, "/style.css", "input[type=submit]:hover {\n");
    appendFile(SPIFFS, "/style.css", "  background-color: #1282A2;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", "\n");
    appendFile(SPIFFS, "/style.css", "input[type=text], input[type=number], select {\n");
    appendFile(SPIFFS, "/style.css", "  width: 50%;\n");
    appendFile(SPIFFS, "/style.css", "  padding: 12px 20px;\n");
    appendFile(SPIFFS, "/style.css", "  margin: 18px;\n");
    appendFile(SPIFFS, "/style.css", "  display: inline-block;\n");
    appendFile(SPIFFS, "/style.css", "  border: 1px solid #ccc;\n");
    appendFile(SPIFFS, "/style.css", "  border-radius: 4px;\n");
    appendFile(SPIFFS, "/style.css", "  box-sizing: border-box;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", "\n");
    appendFile(SPIFFS, "/style.css", "label {\n");
    appendFile(SPIFFS, "/style.css", "  font-size: 1.2rem; \n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", ".value{\n");
    appendFile(SPIFFS, "/style.css", "  font-size: 1.2rem;\n");
    appendFile(SPIFFS, "/style.css", "  color: #1282A2;  \n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", ".state {\n");
    appendFile(SPIFFS, "/style.css", "  font-size: 1.2rem;\n");
    appendFile(SPIFFS, "/style.css", "  color: #1282A2;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", "button {\n");
    appendFile(SPIFFS, "/style.css", "  border: none;\n");
    appendFile(SPIFFS, "/style.css", "  color: #FEFCFB;\n");
    appendFile(SPIFFS, "/style.css", "  padding: 15px 32px;\n");
    appendFile(SPIFFS, "/style.css", "  text-align: center;\n");
    appendFile(SPIFFS, "/style.css", "  font-size: 16px;\n");
    appendFile(SPIFFS, "/style.css", "  width: 100px;\n");
    appendFile(SPIFFS, "/style.css", "  border-radius: 4px;\n");
    appendFile(SPIFFS, "/style.css", "  transition-duration: 0.4s;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", ".button-on {\n");
    appendFile(SPIFFS, "/style.css", "  background-color: #034078;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", ".button-on:hover {\n");
    appendFile(SPIFFS, "/style.css", "  background-color: #1282A2;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", ".button-off {\n");
    appendFile(SPIFFS, "/style.css", "  background-color: #858585;\n");
    appendFile(SPIFFS, "/style.css", "}\n");
    appendFile(SPIFFS, "/style.css", ".button-off:hover {\n");
    appendFile(SPIFFS, "/style.css", "  background-color: #252524;\n");
    appendFile(SPIFFS, "/style.css", "} \n");

    readFile(SPIFFS, "/hello.txt");
    readFile(SPIFFS, "/wifimanager.html");
    readFile(SPIFFS, "/style.css");

    Serial.println("----list 2----");
    listDir(SPIFFS, "/", 1);
	/*
    Serial.println("----attempt to remove dir w/ file----");
    removeDir(SPIFFS, "/mydir");
	
    Serial.println("----remove dir after deleting file----");
    deleteFile(SPIFFS, "/mydir/hello.txt");
    removeDir(SPIFFS, "/mydir");
	
	Serial.println("----list 3----");
    listDir(SPIFFS, "/", 1);
	
*/
	Serial.println( "Test complete" );

}

void loop(){

}
