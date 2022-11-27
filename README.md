# ESP32_Credentials
 set up ESP32 WiFi credentials and time from NTP

First time running this creates an access point named ESP-WIFI-MANAGER. Connect to this, open a browser
and enter the ip address created by the access point (192.168.4.1 by default). This will open a page
allowing you to enter your WiFi credentials (SSID, password are required; IP and Gateway are optional)
and timezone/DST offsets. Click 'Submit' and these values are stored in memory on the ESP32, and the 
ESP32 reboots to connect. After connecting, NTP server is queried for time, which is used to update the 
ESP32 RTC. loop() function writes current time to Serial every minute.

This can be used as the starting point for any project that requires a WiFi connection.

comment out '#define VERBOSE' to run in quiet mode
using #define instead of const (HTML input parameters, SPIFFS filenames, NTP server name) saves some program space

Files:
	ESP32_Credentials.ino - program file
	data\wifimanager.html - html file for credentials/timezone page
	data\style.css - style sheet for presentation
	load the files in data using 'ESP32 Sketch Data Upload' in Arduino 1.8 'Tools' menu.
	
	creates files in SPIFFS with data from wifimanager.html
 
ESP32_Credentials Functions:
	setup() - initialize SPIFFS file system for load/store credentials and time info, initialize WiFi. Add any other initialization for
			new projects here

	loop() - print current time to Serial every minute. Add whatever is needed for new projects here

	SPIFFS functions
		SPIFFS_Init - initialize the SPIFFS file system
		SPIFFS_ReadFile - read a file, return contents as a String
		SPIFFS_WriteFile - create a file using pathname and message
		SPIFFS_ListDir - list files in SPIFFS (for debugging)
		SPIFFS_DeleteFile - delete a named file

	WiFi_Init
		LoadCredentials - read credentials and timezone information from files in SPIFFS
		GetCredentials - create AP, load webpage to get credentials and timezone information, write to SPIFFS and reboot
		ClearCredentials - delete credentials files and reboot

	Time functions
		UpdateLocalTime - first call, get time info from NTP server and save to RTC. Subsequent calls use RTC for time.
						  creates localTimeStr in mm/dd/yyyy hh:mm:ss
						  date/time in dayNum, monthNum, yearNum. hourNum, minNum, secNum integer values