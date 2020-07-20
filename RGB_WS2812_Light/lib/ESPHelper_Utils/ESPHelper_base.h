#ifndef ESPHELPER_BASE
#define ESPHELPER_BASE

#include "ESPHelper.h"
#include <Metro.h>
#include "ESPHelperFS.h"
#include "ESPHelperWebConfig.h"

//setup macros for time
#define SECOND  1000L
#define MINUTE  SECOND * 60L
#define HOUR  MINUTE * 60L

#define NETWORK_HOSTNAME "New-ESP"
const char* HOSTNAME = NETWORK_HOSTNAME;

bool loadConfig();
void loadKey(const char* keyName, const char* filename, char* data, const uint16_t dataSize, const char* defaultData);
void loadKey(const char* keyName, const char* filename, double *data);
void loadKey(const char* keyName, const char* filename, int32_t *data);
void addKey(const char* key, char* buf, const char *file);
void startWifi(bool loadCfg);
void checkForWifiTimeout();
void checkFS();

bool wifiTimeout = false;

//timer that signals the ESP to restart if in broadcast mode for too long
//(generally because it couldnt connect to wifi)
Metro restartTimer = Metro(3 * MINUTE);

//wifi connect timeout timer (drops into broadcast mode when triggered)
Metro connectTimeout = Metro(20 * SECOND);


int progModeCount = 0;


netInfo config;
ESPHelper myESP;

ESP8266WebServer server(80);
ESPHelperWebConfig configPage(&server, "/config");
const char* configFileString = "/networkConfig.json";

const char* broadcastSSID = NETWORK_HOSTNAME "-Hotspot";
const char* broadcastPASS = "";
IPAddress broadcastIP = {192, 168, 1, 1};

extern bool justReconnected;

//ESPHelper
netInfo defaultNet = {	.mqttHost = "",			//can be blank if not using MQTT
                    .mqttUser = "", 	//can be blank
                    .mqttPass = "", 	//can be blank
                    .mqttPort = 1883,					//default port for MQTT is 1883 - only change if needed.
                    .ssid = "",
                    .pass = ""};







//attempt to load a network configuration from the filesystem
bool loadConfig(){
  //check for a good config file and start ESPHelper with the file stored on the ESP
	if(ESPHelperFS::begin()){

		if(ESPHelperFS::validateConfig(configFileString) == GOOD_CONFIG){
			return true;
		}

		//if no good config can be loaded (no file/corruption/etc.) then
		//attempt to generate a new config and restart the module
		else{

			delay(10);
			defaultNet.hostname = HOSTNAME;
			ESPHelperFS::createConfig(&defaultNet, configFileString);
			ESPHelperFS::end();
			ESP.restart();

			//note this will probably never end up returning because of the restart call
			//but gotta have it here because we always need a path to return
			return false;
		}
	}

	//if the filesystem cannot be started, just fail over to the
	//built in network config hardcoded in here
	else{
		return false;
	}

	return false;
}
void manageESPHelper(int wifiStatus){

	//if the system has been running for a long time in broadcast mode, restart it
	if(wifiStatus == BROADCAST && restartTimer.check()){
		// ESP.restart();
        myESP.disableBroadcast();

        //after first failure change the restart timer to a small time because
        //we probably dont need all that extra time after the first failure
        //(ie the first time is possibly because we want to program it but after that
        //its probably because of crappy wifi)

        restartTimer.interval(10*SECOND);
        restartTimer.reset();

        connectTimeout.reset();
        myESP.OTA_enable();
        wifiTimeout = false;
        justReconnected = true;     //reset reconnect val to true for when the ESP does reconnect
        Serial.println("Config Mode Timeout. Restarting Normal Station Mode Through ESPHelper");
        return;
	}

	//if the unit is broadcasting or connected to wifi then reset the timeout vars
	if(wifiStatus == BROADCAST || wifiStatus >= WIFI_ONLY){
		connectTimeout.reset();
		wifiTimeout = false;
	}
	//otherwise check for a timeout condition and handle setting up broadcast
	else if(wifiStatus < WIFI_ONLY){
		checkForWifiTimeout();
	}

	//handle saving a new network config
	if(configPage.handle()){

		myESP.saveConfigFile(configPage.getConfig(), configFileString);
		delay(500);
		ESP.restart();
	}
}

//function that checks for no network connection for a period of time
//and starting up AP mode when that time has elapsed
void checkForWifiTimeout(){
	if(connectTimeout.check() && !wifiTimeout){

		restartTimer.reset();
		wifiTimeout = true;
		myESP.broadcastMode(broadcastSSID, broadcastPASS, broadcastIP);
		myESP.OTA_setPassword(config.otaPassword);
		myESP.OTA_setHostnameWithVersion(config.hostname);
		myESP.OTA_enable();

		Serial.printf("\n\n\
		Could not connect to network.\n\
		Broadcasting our own at %s.\n\
		Connect to network and go to 192.168.1.1/config to setup network settings\n",broadcastSSID);
    }
}


void startWifi(bool loadCfg){
	static bool cfgLoaded = false;
	if(loadCfg){
  		cfgLoaded = loadConfig();
		// loadOtherCfg();
	}

	if(cfgLoaded){myESP.begin(configFileString);}
	else{myESP.begin(&defaultNet);}

	config = myESP.getNetInfo();

	//setup other ESPHelper info and enable OTA updates
	myESP.setHopping(false);
	// myESP.enableHeartbeat(blinkPin);
	myESP.OTA_setPassword(config.otaPassword);
	myESP.OTA_setHostnameWithVersion(config.hostname);
	myESP.OTA_enable();



	delay(10);
	//connect to wifi before proceeding. If cannot connect then switch to ap mode and create a network to config from
	while(myESP.loop() < WIFI_ONLY){
		checkForWifiTimeout();
		if(wifiTimeout){return;}
		delay(10);
	}


}


void loadKey(const char* keyName, const char* filename, char* data, const uint16_t dataSize, const char* defaultData){
	ESPHelperFS::begin();
	String toLoad = ESPHelperFS::loadKey(keyName, filename);
	//could not load (0 length) or good load
	if(toLoad.length() == 0){
		ESPHelperFS::addKey(keyName, defaultData , filename);
		String(defaultData).toCharArray(data, dataSize);
	}
	else{
		toLoad.toCharArray(data, dataSize);
	}
	ESPHelperFS::end();
}


void loadKey(const char* keyName, const char* filename, double *data){
	ESPHelperFS::begin();
	String toLoad = ESPHelperFS::loadKey(keyName, filename);
	//could not load (0 length) or good load
	if(toLoad.length() == 0){
		ESPHelperFS::addKey(keyName, "0.0" , filename);
		*data = 0.0;
	}
	else{*data = toLoad.toFloat();}
	ESPHelperFS::end();
}


void loadKey(const char* keyName, const char* filename, int32_t *data){
	ESPHelperFS::begin();
	String toLoad = ESPHelperFS::loadKey(keyName, filename);
	//could not load (0 length) or good load
	if(toLoad.length() == 0){
		ESPHelperFS::addKey(keyName, "0" , filename);
		*data = 0;
	}
	else{*data = toLoad.toInt();}
	ESPHelperFS::end();
}


void addKey(const char* key, char* buf, const char *file){
	ESPHelperFS::begin();
	ESPHelperFS::addKey(key, buf, file);
	ESPHelperFS::end();
}


void checkFS(){
    SPIFFS.begin();
    //if file does not exist then FS is not formatted
    if (!SPIFFS.exists("/formatComplete.txt")) {
        Serial.println("Please wait 30 secs for SPIFFS to be formatted");
        SPIFFS.format();
        Serial.println("Spiffs formatted");

        File f = SPIFFS.open("/formatComplete.txt", "w");
        if (!f) {} // failed to open file
        else {f.println("Format Complete");}
    }
    SPIFFS.end();
}

#endif
