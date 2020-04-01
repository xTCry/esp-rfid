/*
MIT License

Copyright (c) 2018 esp-rfid Community
Copyright (c) 2017 Ömer Şiar Baysal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */
#define VERSION "1.3.1"

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TimeLib.h>
#include <Ticker.h>
#include "Ntp.h"
#include <AsyncMqttClient.h>
#include <Bounce2.h>
#include "magicnumbers.h"

 // #define DEBUG

int numRelays=1;

#ifdef OFFICIALBOARD

#include <Wiegand.h>

WIEGAND wg;
int relayPin[MAX_NUM_RELAYS] = {13};
bool activateRelay [MAX_NUM_RELAYS]= {false};
bool deactivateRelay [MAX_NUM_RELAYS]= {false};

#endif

#ifndef OFFICIALBOARD

#include <MFRC522.h>
#include "PN532.h"
#include <Wiegand.h>
#include "rfid125kHz.h"

MFRC522 mfrc522 = MFRC522();
PN532 pn532;
WIEGAND wg;
RFID_Reader RFIDr;

int rfidss;
int readertype;

// relay specific variables

int relayPin[MAX_NUM_RELAYS];
bool activateRelay [MAX_NUM_RELAYS]= {false,false,false,false};
bool deactivateRelay [MAX_NUM_RELAYS]= {false,false,false,false};

#endif

int lockType[MAX_NUM_RELAYS];
int relayType[MAX_NUM_RELAYS];
unsigned long activateTime[MAX_NUM_RELAYS];


// these are from vendors
#include "webh/glyphicons-halflings-regular.woff.gz.h"
#include "webh/required.css.gz.h"
#include "webh/required.js.gz.h"

// these are from us which can be updated and changed
#include "webh/esprfid.js.gz.h"
#include "webh/esprfid.htm.gz.h"
#include "webh/index.html.gz.h"

#ifdef ESP8266
extern "C" {
	#include "user_interface.h"
}
#endif

NtpClient NTP;
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
WiFiEventHandler wifiDisconnectHandler, wifiConnectHandler;
Bounce openLockButton;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

unsigned long blink_ = millis();
bool wifiFlag = false;
bool configMode = false;
int wmode;
uint8_t wifipin = 255;

uint8_t doorstatpin = 255;
uint8_t lastDoorState = 0;

uint8_t openlockpin = 255;
#define LEDoff HIGH
#define LEDon LOW

// Variables for whole scope
const char *http_username = "admin";
char *http_pass = NULL;
unsigned long previousMillis = 0;
unsigned long previousLoopMillis = 0;
unsigned long currentMillis = 0;
unsigned long cooldown = 0;
unsigned long keyTimer = 0;
String currentInput = "";
unsigned long deltaTime = 0;
unsigned long uptime = 0;
bool shouldReboot = false;
bool inAPMode = false;
bool isWifiConnected = false;
unsigned long autoRestartIntervalSeconds = 0;

bool wifiDisabled = true;
bool doDisableWifi = false;
bool doEnableWifi = false;
bool timerequest = false;
bool formatreq = false;
unsigned long wifiTimeout = 0;
unsigned long wiFiUptimeMillis = 0;
char *deviceHostname = NULL;

int mqttenabled = 0;
char *mqttTopic = NULL;
char *mhs = NULL;
char *muser = NULL;
char *mpas = NULL;
int mport;

int timeZone;

unsigned long nextbeat = 0;

unsigned long interval 	= 1800;  // Add to GUI & json config
bool mqttEvents 		= false; // Sends events over MQTT disables SPIFFS file logging


#include "log.esp"
#include "mqtt.esp"
#include "helpers.esp"
#include "wsResponses.esp"
#include "rfid.esp"
#include "wifi.esp"
#include "config.esp"
#include "websocket.esp"
#include "webserver.esp"
#include "door.esp"

void ICACHE_FLASH_ATTR setup()
{
#ifdef OFFICIALBOARD
	// Set relay pin to LOW signal as early as possible
	pinMode(13, OUTPUT);
	digitalWrite(13, LOW);
	delay(200);
#endif

#ifdef DEBUG
	Serial.begin(9600);
	Serial.println();

	Serial.print(F("[ INFO ] ESP RFID v"));
	Serial.println(VERSION);

	uint32_t realSize = ESP.getFlashChipRealSize();
	uint32_t ideSize = ESP.getFlashChipSize();
	FlashMode_t ideMode = ESP.getFlashChipMode();
	Serial.printf("Flash real id:   %08X\n", ESP.getFlashChipId());
	Serial.printf("Flash real size: %u\n\n", realSize);
	Serial.printf("Flash ide  size: %u\n", ideSize);
	Serial.printf("Flash ide speed: %u\n", ESP.getFlashChipSpeed());
	Serial.printf("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));
	if (ideSize != realSize)
	{
		Serial.println("Flash Chip configuration wrong!\n");
	}
	else
	{
		Serial.println("Flash Chip configuration ok.\n");
	}
#endif

	if (!SPIFFS.begin())
	{
#ifdef DEBUG
		Serial.print(F("[ WARN ] Formatting filesystem..."));
#endif
		if (SPIFFS.format())
		{
			writeEvent("WARN", "sys", "Filesystem formatted", "");

#ifdef DEBUG
			Serial.println(F(" completed!"));
#endif
		}
		else
		{
#ifdef DEBUG
			Serial.println(F(" failed!"));
			Serial.println(F("[ WARN ] Could not format filesystem!"));
#endif
		}
	}
	wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
	wifiConnectHandler = WiFi.onStationModeConnected(onWifiConnect);
	configMode = loadConfiguration();
	if (!configMode)
	{
		fallbacktoAPMode();
		configMode = false;
	}
	else {
		configMode = true;
	}
	setupWebServer();
	writeEvent("INFO", "sys", "System setup completed, running", "");
}

void ICACHE_RAM_ATTR loop()
{
	currentMillis = millis();
	deltaTime = currentMillis - previousLoopMillis;
	uptime = NTP.getUptimeSec();
	previousLoopMillis = currentMillis;

	openLockButton.update();
	if (openLockButton.fell())
	{
#ifdef DEBUG
		Serial.println("Button has been pressed");
#endif
		writeLatest("", "(used open/close button)", 1);
		activateRelay[0] = true;
		mqtt_publish_button(now());
	}

	if (wifipin != 255 && configMode && !wmode)
	{
		if (!wifiFlag)
		{
			if ((currentMillis - blink_) > 500)
			{
				blink_ = currentMillis;
				digitalWrite(wifipin, !digitalRead(wifipin));
			}
		}
		else
		{
			if (!(digitalRead(wifipin)==LEDon)) digitalWrite(wifipin, LEDon);
		}
	}

	if (doorstatpin != 255)
	{
    doorStatus();
    delay(1);
	}

	if (currentMillis >= cooldown)
	{
		rfidloop();
	}

	// Continuous relay mode

	for (int currentRelay = 0; currentRelay < numRelays ; currentRelay++){
	  if (lockType[currentRelay] == LOCKTYPE_CONTINUOUS)
		{
		if (activateRelay[currentRelay])
		{
			// currently OFF, need to switch ON
			if (digitalRead(relayPin[currentRelay]) == !relayType[currentRelay])
			{
#ifdef DEBUG
				Serial.print("mili : ");
				Serial.println(millis());
				Serial.printf("activating relay %d now\n",currentRelay);
#endif
				digitalWrite(relayPin[currentRelay], relayType[currentRelay]);
			}
			else	// currently ON, need to switch OFF
			{
#ifdef DEBUG
				Serial.print("mili : ");
				Serial.println(millis());
				Serial.printf("deactivating relay %d now\n",currentRelay);
#endif
				digitalWrite(relayPin[currentRelay], !relayType[currentRelay]);
			}
			activateRelay[currentRelay] = false;
		}
	  }
	  else if (lockType[currentRelay] == LOCKTYPE_MOMENTARY)	// momentary relay mode
	  {
		if (activateRelay[currentRelay])
		{
#ifdef DEBUG
			Serial.print("mili : ");
			Serial.println(millis());
			Serial.printf("activating relay %d now\n",currentRelay);
#endif
			digitalWrite(relayPin[currentRelay], relayType[currentRelay]);
			previousMillis = millis();
			activateRelay[currentRelay] = false;
			deactivateRelay[currentRelay] = true;
		}
		else if ((currentMillis - previousMillis >= activateTime[currentRelay]) && (deactivateRelay[currentRelay]))
		{
#ifdef DEBUG
			Serial.println(currentMillis);
			Serial.println(previousMillis);
			Serial.println(activateTime[currentRelay]);
			Serial.println(activateRelay[currentRelay]);
			Serial.println("deactivate relay after this");
			Serial.print("mili : ");
			Serial.println(millis());
#endif
			digitalWrite(relayPin[currentRelay], !relayType[currentRelay]);
			deactivateRelay[currentRelay] = false;
		}
	  }
	}
	if (formatreq)
	{
#ifdef DEBUG
		Serial.println(F("[ WARN ] Factory reset initiated..."));
#endif
		SPIFFS.end();
		ws.enable(false);
		SPIFFS.format();
		ESP.restart();
	}

	if (timerequest)
	{
		timerequest = false;
		sendTime();
	}

	if (autoRestartIntervalSeconds > 0 && uptime > autoRestartIntervalSeconds * 1000)
	{
		writeEvent("INFO", "sys", "System is going to reboot", "");
#ifdef DEBUG
		Serial.println(F("[ WARN ] Auto restarting..."));
#endif
		shouldReboot = true;
	}

	if (shouldReboot)
	{
		writeEvent("INFO", "sys", "System is going to reboot", "");
#ifdef DEBUG
		Serial.println(F("[ INFO ] Rebooting..."));
#endif
		ESP.restart();
	}

	if (isWifiConnected)
	{
		wiFiUptimeMillis += deltaTime;
	}

	if (wifiTimeout > 0 && wiFiUptimeMillis > (wifiTimeout * 1000) && isWifiConnected == true)
	{
		writeEvent("INFO", "wifi", "WiFi is going to be disabled", "");
		doDisableWifi = true;
	}

	if (doDisableWifi == true)
	{
		doDisableWifi = false;
		wiFiUptimeMillis = 0;
		disableWifi();
	}
	else if (doEnableWifi == true)
	{
		writeEvent("INFO", "wifi", "Enabling WiFi", "");
		doEnableWifi = false;
		if (!isWifiConnected)
		{
			wiFiUptimeMillis = 0;
			enableWifi();
		}
	}

	if (mqttenabled == 1)
	{
		if (mqttClient.connected())
		{
			if ((unsigned)now() > nextbeat)
			{
				mqtt_publish_heartbeat(now());
				nextbeat = (unsigned)now() + interval;
#ifdef DEBUG
				Serial.print("[ INFO ] Nextbeat=");
				Serial.println(nextbeat);
#endif
			}
		}
	}
	// Keep an eye on timeout waiting for keypress
	// Clear code and timer when timeout is reached
	if (keyTimer > 0 && millis() - keyTimer >= KEYBOARD_TIMEOUT_MILIS)
	{
#ifdef DEBUG
		Serial.println("[ INFO ] Keycode timeout");
#endif
		keyTimer = 0;
		currentInput = "";
	}

}
