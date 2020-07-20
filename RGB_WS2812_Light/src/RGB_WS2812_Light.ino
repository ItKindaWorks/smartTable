/*
	Copyright (c) 2019 ItKindaWorks All right reserved.
    github.com/ItKindaWorks

    This File is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This File is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with This File.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "ESPHelper_base.h"
#include <HSBColor.h>
#include "FastLED.h"
#include "myTypes.h"


#define AFFIRM_REQ(SUB_TEXT, SUB_DATA) String("<html>\
	<header>\
	<title>Updated!</title>\
	</header>\
	<body>\
	<p><strong>" SUB_TEXT " " + SUB_DATA + "</strong></br>\
	</body>\
	<meta http-equiv=\"refresh\" content=  \"2; url=./\"  />\
	</html>")

//invalid input http str
const String INVALID_REQUEST_STR = String("<html>\
<header>\
<title>Bad Input</title>\
</header>\
<body>\
	<p><strong>400: Invalid Request</strong></br>\
</body>\
<meta http-equiv=\"refresh\" content=  \"0; url=./\"  />\
</html>");

/////////////////////////   Pin Definitions   /////////////////////////

const int ledPin = D2;

/////////////////////////   Networking   /////////////////////////

//bool set to true when the system has just connected or reconnected. Allows us to ping the IP
//or do whatever else is necessary when we connect to wifi
bool justReconnected = true;



/////////////////////////   Timers   /////////////////////////


/////////////////////////   Sensors   /////////////////////////



/////////////////////////   Config Keys    /////////////////////////

char topic[64];
char statusTopic[64];


/////////////////////////   Other   /////////////////////////
#define GRAD_DELTA 1
#define GRAD_CALC gradIndex - (i * GRAD_DELTA)


const int MAX_LEDS = 255;
CRGB leds[MAX_LEDS];
int numLEDS = 1;



enum superModes {SET, MOOD, RAINBOW};
enum moodColors{RED, GREEN, BLUE};
enum modes {NORMAL, FADING};
enum updateTypes{HSB_UPDATE, RGB_UPDATE, POWER_UPDATE};

lightState nextState;

lightState statusValues;

int superMode = SET;	//overall mode of the light (moodlight, network controlled, music bounce, etc)
boolean newCommand = false;

unsigned long currentMillis = 0;

int timeout = 0; //timeout timer to fail to standard moodlight when no known wifi is found within 10 seconds





void setup() {
	Serial.begin(115200);
	checkFS();

	delay(1000);
	Serial.println("\n\nESP WS2812B LED Controller Version 1.0");
	Serial.println("Starting Up...");

	Serial.println("Loading controller configuration");
	loadOtherCfg();



	/////////////////////////   IO init   /////////////////////////
	Serial.println("Setting up LEDs");
	Serial.print("Set LED Count: ");
	Serial.println(numLEDS);
	delay(10);
	FastLED.addLeds<WS2812B, ledPin, GRB>(leds, numLEDS);
	for(int i = 0; i < numLEDS; i++){
		leds[i] = CRGB::Black;
	}
	FastLED.show();

	/////////////////////////   Network Init   /////////////////////////

	Serial.println("Starting WiFi");
	//startup the wifi and web server
	startWifi(true);

	Serial.println("Setting Up Web Server");
	configPage.fillConfig(&config);
	configPage.begin(config.hostname);
	server.on("/", HTTP_GET, handleStatus);
	server.on("/numLEDS", HTTP_POST, handleLEDCountChange);
	server.on("/topicUpdate", HTTP_POST, handleTopicUpdate);

	//UNCOMMENT THIS LINE TO ENABLE MQTT CALLBACK
	myESP.setCallback(callback);
	myESP.addSubscription(topic);

	server.begin();                            // Actually start the server

	Serial.println("Setup Finished. Running Main Loop...");
}

void loop(){
	while(1){
		/////////////////////////   Background Routines   /////////////////////////
		currentMillis = millis();

		/////////////////////////   Network Handling   /////////////////////////

		//get the current status of ESPHelper
		int espHelperStatus = myESP.loop();
		manageESPHelper(espHelperStatus);

		if(espHelperStatus >= WIFI_ONLY){

			if(espHelperStatus == FULL_CONNECTION){
				//full connection loop code here
				lightHandler();
			}

			if(justReconnected){

				//post to mqtt if connected
				if(espHelperStatus == FULL_CONNECTION){
					myESP.publish("/home/IP", String(String(myESP.getHostname()) + " -- " + String(myESP.getIP()) ).c_str() );
				}
				Serial.print("Connected to network with IP: ");
				Serial.println(myESP.getIP());
				justReconnected = false;
			}

		}

		//loop delay can be down to 1ms if needed. Shouldn't go below that though
		//(ie dont use 'yield' if it can be avoided)
		delay(1);
	}

}

























void lightHandler(){
	static lightState newState;
	static lightState currentState;
	static int currentMoodColor = 0;

	static int isFading = 0;

	static int gHue = 0;

	if(superMode == RAINBOW){
		Serial.println("Rainbow");
		fill_rainbow( leds, numLEDS, gHue, 2);
		if(millis() % 13 == 0){gHue++;}
		if(gHue > 255){gHue = 0;}
		FastLED.show();
		return;
	}

	//handle color fading
	if(superMode == MOOD && isFading == 0){
		// fadePeriod = 4000;
		if(currentMoodColor == RED){
			nextState.red = 0;
			nextState.green = 255;
			nextState.blue = 0;
			nextState.updateType = RGB;
			newCommand = true;
			currentMoodColor = GREEN;

		}
		else if(currentMoodColor == GREEN){
			nextState.red = 0;
			nextState.green = 0;
			nextState.blue = 255;
			nextState.updateType = RGB;
			newCommand = true;
			currentMoodColor = BLUE;
		}
		else if(currentMoodColor == BLUE){
			nextState.red = 255;
			nextState.green = 0;
			nextState.blue = 0;
			nextState.updateType = RGB;
			newCommand = true;
			currentMoodColor = RED;
		}
	}

	//update target and current light values
	lightUpdater(&newState, currentState);
	isFading = lightChanger(newState, &currentState);

}


int lightChanger(lightState newState, lightState *currentState){
	static timer changeTimer;
	changeTimer.interval = 1;

	static int changeMode = NORMAL;
	static int currentPeriod = 0;

	//if it is time to update the lights
	if(checkTimer(&changeTimer.previousTime, changeTimer.interval)){

		//check for a new command and update the mode
		if(newCommand){
			Serial.println("New HSV command, updating lights. Mode set to FADING");
			newCommand = false;
			changeMode = FADING;
		}


		//activly in the middle of a fade
		if(changeMode == FADING){
			// Serial.println("Fading Lights");

			//check if all colors are at their target state or if we are still in the middle of a fade operation
			//and have not timed out.
			if((newState.red != currentState->red || \
				newState.blue != currentState->blue || \
				newState.green != currentState->green) || \
				(currentPeriod <= currentState->fadePeriod)){

				//only update the channels if it is time for them to update.
				//Each channel has it's own timing so that all channels end
				//at the same time.

				if(currentPeriod % newState.redRate == 0){
					//fade up if color is too low
					if(newState.red > currentState->red){currentState->red++;}
					//otherwise fade down
					else if (newState.red < currentState->red){currentState->red--;}
				}

				if(currentPeriod % newState.greenRate == 0){
					//fade up if color is too low
					if(newState.green > currentState->green){currentState->green++;}
					//otherwise fade down
					else if (newState.green < currentState->green){currentState->green--;}
				}

				if(currentPeriod % newState.blueRate == 0){
					//fade up if color is too low
					if(newState.blue > currentState->blue){currentState->blue++;}
					//otherwise fade down
					else if (newState.blue < currentState->blue){currentState->blue--;}
				}

				//write any changes to the pins
				// analogWrite(redPin, currentState->red);
				// analogWrite(greenPin, currentState->green);
				// analogWrite(bluePin, currentState->blue);

				CRGB color = CRGB(currentState->red, currentState->green, currentState->blue);
				for (int i = 0; i < numLEDS; i++){leds[i] = color;}
				FastLED.show();

				//update the status values for displaying on the web page. This is needed
				//because the current led values are stored in a static var in lightHandler.
				//this could be fixed in the future when refactoring
				statusValues.red = currentState->red;
				statusValues.green = currentState->green;
				statusValues.blue = currentState->blue;

				// Serial.println("Done");

				//increment the current period.
				currentPeriod++;
				return 1;
			}
			else{
				Serial.println("Fading finished - returning to NORMAL");
				currentPeriod = 0;
				changeMode = NORMAL;
				return 0;
			}

		}


		else if (changeMode == NORMAL){
			return 0;
		}

	}

	return -1;

}


void lightUpdater (lightState *newState, lightState currentState){

	//check for a new command
	if (newCommand){
		Serial.println("Processing Latest Update");

		//hue sat val update (vs RGB)
		if(nextState.updateType == HSB_UPDATE){
			Serial.println("Update was HSV");

			//convert from HSV targets to RGB targets
			int newRGB[3];
			H2R_HSBtoRGBfloat(nextState.hue, nextState.saturation, nextState.brightness, newRGB);
			newState->red = newRGB[0];
			newState->green = newRGB[1];
			newState->blue = newRGB[2];

			//find the number of steps to move for each channel
			int redDiff = abs(newState->red - currentState.red);
			int greenDiff = abs(newState->green - currentState.green);
			int blueDiff = abs(newState->blue - currentState.blue);

			//figure out how long each fade step needs to be for each channel
			//else handles when there are no steps needed because div/0 is bad.
			//if statement divides fadePeriod / diff and if div is 0 then we get
			//#bad things.

			if(redDiff > 0){newState->redRate = (nextState.fadePeriod / redDiff);}
			else{newState->redRate = nextState.fadePeriod;}

			if(greenDiff > 0){newState->greenRate = (nextState.fadePeriod / greenDiff);}
			else{newState->greenRate = nextState.fadePeriod;}

			if(blueDiff > 0){newState->blueRate = (nextState.fadePeriod / blueDiff);}
			else{newState->blueRate = nextState.fadePeriod;}

			// Serial.println("Rates (RGB):");
			// Serial.println(newState->redRate);
			// Serial.println(newState->greenRate);
			// Serial.println(newState->blueRate);

			if(newState->redRate == 0){newState->redRate = 1;}
			if(newState->greenRate == 0){newState->greenRate = 1;}
			if(newState->blueRate == 0){newState->blueRate = 1;}

			//update the fade period
			newState->fadePeriod = nextState.fadePeriod;

			Serial.println("Done.");

		}

		//RGB update (vs HSV)
		else if(nextState.updateType == RGB_UPDATE){
			Serial.println("Update was RGB");

			//grab the new RGB values from the mqtt light struct
			newState->red = nextState.red;
			newState->green = nextState.green;
			newState->blue = nextState.blue;

			//find the number of steps to move for each channel
			int redDiff = abs(newState->red - currentState.red);
			int greenDiff = abs(newState->green - currentState.green);
			int blueDiff = abs(newState->blue - currentState.blue);

			//figure out how long each fade step needs to be for each channel
			//else handles when there are no steps needed because div/0 is bad.
			//if statement divides fadePeriod / diff and if div is 0 then we get
			//#bad things. (also not sure atm why I am +1 to the rate here...)
			if(redDiff > 0){newState->redRate = (nextState.fadePeriod / redDiff) + 1;}
			else{newState->redRate = nextState.fadePeriod;}

			if(greenDiff > 0){newState->greenRate = (nextState.fadePeriod / greenDiff) + 1;}
			else{newState->greenRate = nextState.fadePeriod;}

			if(blueDiff > 0){newState->blueRate = (nextState.fadePeriod / blueDiff) + 1;}
			else{newState->blueRate = nextState.fadePeriod;}

			//update the fade period
			newState->fadePeriod = nextState.fadePeriod;

			Serial.println("Done.");
		}
	}

}



//MQTT callback
//input: topic ptr, payload ptr, size of payload
//output: none
void callback(char* topic, byte* payload, unsigned int length) {

	//convert topic to string to make it easier to work with
	String topicStr = topic;


	//fix the payload string by creating a new one and adding a null terminator.
	char newPayload[40];
	memcpy(newPayload, payload, length);
	newPayload[length] = '\0';


	Serial.println("Got New Command");
	Serial.println(topicStr);
	Serial.println(newPayload);

	//publish back to the status topic as feedback to the system
	myESP.publish(statusTopic, newPayload);	//ping back to the status topic

	Serial.println("Processing...");
	Serial.println(newPayload[0]);

	//hsv command
	if(newPayload[0] == 'h'){

		//convert c string to floats for hsv
		nextState.hue = atof(&newPayload[1]);
		nextState.saturation = atof(&newPayload[7]);
		nextState.brightness = atof(&newPayload[13]);

		//set the update type and fade speed
		nextState.updateType = HSB_UPDATE;
		nextState.fadePeriod = 200;

		//mark a new command
		newCommand = true;

		//make sure that the mode is set to SET (vs MOOD or other mode)
		superMode = SET;

		Serial.println("HSV command receieved");

	}

	//rgb command
	else if (newPayload[0] == 'r'){
		//convert c string to ints for rgb
		nextState.red = atoi(&newPayload[1]);
		nextState.green = atoi(&newPayload[5]);
		nextState.blue = atoi(&newPayload[9]);

		//set the update type and fade speed
		nextState.updateType = RGB_UPDATE;
		nextState.fadePeriod = 2100;

		//mark a new command
		newCommand = true;

		//make sure that the mode is set to SET (vs MOOD or other mode)
		superMode = SET;
	}

	//set mood light mode ('m1' or 'm0')
	else if(newPayload[0] == 'm'){
		Serial.println(newPayload[1]);
		//enable mood light
		if(newPayload[1] == '1'){
			superMode = RAINBOW;
			newCommand = true;
		}

		//turn off moodlight
		else if(newPayload[1] == '0'){
			superMode = SET;
			nextState.fadePeriod = 2100;
			newCommand = true;
		}
	}

	//set for audio bounce mode. (not implemented yet)
	else if(newPayload[0] == 'a'){
	}

}


//performs a map operation but for floating point numbers
//input: starting val, starting val minimum, starting val maxiumum, output minimum, output maximum
//output: mapped value between out_min and out_max (double)
float map_double(double x, double in_min, double in_max, double out_min, double out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


//this functions simplifies checking whether a task needs to run by checking our timing variables
//and returning true or false depending on whether we need to run or not
boolean checkTimer(unsigned long *previousTime, unsigned long interval){
  if(currentMillis - *previousTime >= interval){
    *previousTime = currentMillis;
    return true;
  }
  return false;
}
























void loadOtherCfg(){
	ESPHelperFS::begin();

	//Remember to change this to whatever file you want to store/read your custom config from
	const char* file = "/config.json";

	//read the topic from memory (or use a default topic if there is an error)
	loadKey("topic", file, topic, sizeof(topic), "/home/test");
	//create a temporary string to do some concating to eventually assign to the status topic
	String tempTopic = topic;
	tempTopic += "/status";
	tempTopic.toCharArray(statusTopic, sizeof(statusTopic));

	loadKey("numLEDS", file, &numLEDS);
	numLEDS = constrain(numLEDS, 1, MAX_LEDS);

	// loadKey("Key", file, someFloat);
	// loadKey("thingUser", file, someStr, sizeof(someStr));

	ESPHelperFS::end();
}




//main config page that allows user to enter in configuration info
void handleStatus() {
  server.send(200, "text/html", \
  String("<html>\
  <header>\
  <title>Device Info</title>\
  </header>\
  <body>\
    <p><strong>System Status</strong></br>\
    Device Name: " + String(config.hostname) + "</br>\
    Connected SSID: " + String(myESP.getSSID()) + "</br>\
    Device IP: " + String(myESP.getIP()) + "</br>\
    Uptime (ms): " + String(millis()) + "</p>\
    <p> </p>\
    \
    \
    <!-- THIS IS COMMENTED OUT \
    <p><strong>Device Variables</strong></p>\
    Temperature: " + String("SOME_VAR") + "</br>\
    UNTIL THIS LINE HERE -->\
    \
    \
  </body>\
  \
  \
	<form action=\"/topicUpdate\" method=\"POST\">\
	Update Topic: <input type=\"text\" name=\"topic\" size=\"64\" value=\"" + String (topic) + "\"></br>\
	<input type=\"submit\" value=\"Submit\"></form>\
  \
  \
	<form action=\"/numLEDS\" method=\"POST\">\
	Update Topic: <input type=\"text\" name=\"numLEDS\" size=\"3\" value=\"" + String (numLEDS) + "\"></br>\
	<input type=\"submit\" value=\"Change LED Count (Max 255)\"></form>\
  \
  \
  </html>"));
}


//handler function for updating the relay timer
void handleTopicUpdate() {

	const char* keyName = "topic";

	//Remember to change this to whatever file you want to store/read your custom config from
	const char* file = "/config.json";

	//make sure that all the arguments exist and that at least an SSID and hostname have been entered
	if(!server.hasArg(keyName)){
		server.send(400, "text/plain", INVALID_REQUEST_STR);
		return;
	}

	//handle int/float
	// int data = server.arg(keyName).toInt();
	// char buf[20];
	// String tmp = String(data);
	// tmp.toCharArray(buf, sizeof(buf));

	//handle string
	char buf[64];
	server.arg(keyName).toCharArray(buf, sizeof(buf));

	myESP.removeSubscription(topic);

	//save the key to flash and refresh data var with data from flash
	addKey(keyName, buf , file);
	loadKey(keyName, file, topic, sizeof(topic), "/home/test");

	myESP.addSubscription(topic);

	//tell the user that the config is loaded in and the module is restarting
	server.send(200, "text/html",AFFIRM_REQ("Updated Topic To: " , String(topic)));

}



//handler function for updating the relay timer
void handleLEDCountChange() {

	const char* keyName = "numLEDS";

	//Remember to change this to whatever file you want to store/read your custom config from
	const char* file = "/config.json";

	//make sure that all the arguments exist and that at least an SSID and hostname have been entered
	if(!server.hasArg(keyName)){
		server.send(400, "text/plain", INVALID_REQUEST_STR);
		return;
	}

	// handle int/float
	int data = server.arg(keyName).toInt();
	char buf[20];
	String tmp = String(data);
	tmp.toCharArray(buf, sizeof(buf));

	//save the key to flash and refresh data var with data from flash
	addKey(keyName, buf , file);
	loadKey(keyName, file, &numLEDS);


	FastLED.addLeds<WS2812B, ledPin, GRB>(leds, numLEDS);
	for(int i = 0; i < numLEDS; i++){
		leds[i] = leds[0];
	}
	FastLED.show();


	//tell the user that the config is loaded in and the module is restarting
	server.send(200, "text/html",AFFIRM_REQ("Updated Led Count To: " , String(numLEDS)));

}
