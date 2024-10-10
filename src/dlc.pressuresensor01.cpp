/*********************************************************************
This sketch is intended to run on a D1 Mini. It reads pressure 
from a transducer and sends the readings to tago.io.

This sketch uses the 1.3" OLED display, SH1106 128x64, I2C comms.

x1Val and x2Val must be calibrated to the voltage readings at
0 and 100 PSI respectively for the output to be accurate.

*********************************************************************/

//This ProgramID is the name of the sketch and identifies what code is running on the D1 Mini
//Device Information
const char* ProgramID = "dlc.presr01";
const char* SensorType = "Pressure";
//const char* mqtt_topic = "boosttrip01";
const char* mqtt_unit = "PSI";
const char* mqtt_server_init = "192.168.1.182";
const char* mqtt_user = "mqttuser";
const char* mqtt_password = "Quik5ilver7";
unsigned long mqtt_frequency = 5000; //mqtt posting frequency in milliseconds (1000 = 1 second)
int SerialOn = 0;

#include <SPI.h>
#include <Wire.h>

//OTA Stuff
#include <ArduinoOTA.h>

//Function definitions
float roundoff(float value, unsigned char prec);
void printWifiStatus();

//For 1.3in displays
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#define i2c_Address 0x3c //initialize with the I2C addr 0x3C Typically eBay OLED's
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1   //   QT-PY / XIAO
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//Wifi Stuff
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
//const char *ssid =	"LMWA-PumpHouse";		// cannot be longer than 32 characters!
//const char *pass =	"ds42396xcr5";		//
const char *ssid =	"WiFiFoFum";		// cannot be longer than 32 characters!
const char *pass =	"6316EarlyGlow";		//
WiFiClient wifi_client;
String wifistatustoprint;

//Tago.io server address and device token
char server[] = "api.tago.io";
String Device_Token = "1c275a1c-27ee-42b0-b59b-d82e40de269d"; //d1_002_pressure_sensor Default token
//String pressure_string = "";

//Timing
unsigned long now = 0;
int uptimeSeconds = 0;
int uptimeDays;
int uptimeHours;
int secsRemaining;
int uptimeMinutes;
char uptimeTotal[30];

//Data payload variables
int counter = 1;
char pressureout[32];
String pressuretoprint;

//Sensor setup & payload variables
const int SensorPin = A0; //Pin used to read from the Transducer
const float alpha = 0.95; // Low Pass Filter alpha (0 - 1 ).
const float aRef = 5; // analog reference
float filteredVal = 512; // midway starting point
float transducerVal; // Raw analog value read from the Transducer
float voltageVal; //Calculated Voltage from the Analog read of the Transducer
float psi; //Calculated PSI

//binary sensor stuff
//#define BINARYPIN 13

//MQTT Stuff
#include <bits/stdc++.h>
#include <string>
#include <iostream>
#include <PubSubClient.h>
void callback(char* topic, byte* payload, unsigned int length);
void reconnect();
void sendMQTT(double PressureReading);
const char* mqtt_server = mqtt_server_init;  //Your network's MQTT server (usually same IP address as Home Assistant server)
PubSubClient pubsub_client(wifi_client);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];
int value = 0;

void setup() {


  Serial.begin(115200);
  while (!Serial) {
    ;                     // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("SETUP");
  Serial.println();

 //MQTT Setup
  pubsub_client.setServer(mqtt_server, 1883);
  pubsub_client.setCallback(callback);


  //pinMode(BINARYPIN, INPUT_PULLUP);

  //1.3" OLED Setup
  delay(250); // wait for the OLED to power up
  display.begin(i2c_Address, true); // Address 0x3C default
 //display.setContrast (0); // dim display
 
  display.display();
  delay(2000);

  // Clear the buffer.
  display.clearDisplay();

  // draw a single pixel
  display.drawPixel(64, 64, SH110X_WHITE);
  // Show the display buffer on the hardware.
  // NOTE: You _must_ call display after making any drawing commands
  // to make them visible on the display hardware!
  display.display();
  delay(2000);
  display.clearDisplay();


    //OTA Setup Stuff
  if(1){
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    display.clearDisplay();
    Serial.println("Start OTA");
    display.setCursor(0, 0);
    display.println("Starting OTA!");
    display.display();
    });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd OTA - Rebooting!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("OTA Done!"); display.println("Rebooting!");
    display.display();
    ESP.restart();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Progress: " + (progress / (total / 100)));
    display.display();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

    //Start OTA
  ArduinoOTA.begin();
  Serial.println("OTA Listenerer Started");

  }//End OTA Code Wrapper

}

void loop() {


  ArduinoOTA.handle(); // Start listening for OTA Updates

  now = millis();
  delay (.01); //sample delay

  uptimeSeconds=now/1000;
  uptimeHours= uptimeSeconds/3600;
  uptimeDays=uptimeHours/24;
  secsRemaining=uptimeSeconds%3600;
  uptimeMinutes=secsRemaining/60;
  uptimeSeconds=secsRemaining%60;
  sprintf(uptimeTotal,"Uptime %02dD:%02d:%02d:%02d",uptimeDays,uptimeHours,uptimeMinutes,uptimeSeconds);


  //Wifi Stuff
  if (WiFi.status() != WL_CONNECTED) {
    
    //Write wifi connection to display
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("Booting Program ID:");
    display.println(ProgramID);
    display.println("Sensor Type:");
    display.println(SensorType);
    display.println("Connecting To WiFi:");
    display.println(ssid);
    display.println("\nWait for it......");
    display.display();

    //write wifi connection to serial
    Serial.print("Connecting to ");
    Serial.print(ssid);
    Serial.println("...");
    WiFi.setHostname(ProgramID);
    WiFi.begin(ssid, pass);

    //delay 8 seconds for effect
    delay(8000);

    if (WiFi.waitForConnectResult() != WL_CONNECTED){
      return;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("Water Pressure Sensor\nDevice ID: d1_002");
    display.setTextSize(1);
    display.println(" ");
    display.println("Connected To WiFi:");
    display.println(ssid);
    display.println(" ");
    display.display();

    Serial.println("\n\nWiFi Connected! ");
    printWifiStatus();

  }

  if (WiFi.status() == WL_CONNECTED) {
    wifistatustoprint="Wifi Connected!";
  }else{
    wifistatustoprint="Womp, No Wifi!";
  }




  //perform reading and filter the value.
  transducerVal = (float)analogRead(SensorPin);  //raw Analog To Digital Converted (ADC) value from sensor
  filteredVal = (alpha * filteredVal) + ((1.0 - alpha) * transducerVal); // Low Pass Filter, smoothes out readings.
  voltageVal = (filteredVal * aRef) / 1023; //calculate voltage using smoothed sensor reading... 5.0 is system voltage, 1023 is resolution of the ADC...
  psi = (23.608 * voltageVal) - 20.446; // generated by Excel Scatterplot for transducer S/N 2405202308207

  if(psi<0){
      //Serial.println("PSI<0, Making it 0.");
      psi=0;
  }

  if(SerialOn){
    //print values to serial console for inspection
    Serial.print("Raw ADC: "); Serial.println(transducerVal, 0);
    Serial.print("Filtered ADC: "); Serial.println(filteredVal, 0);
    Serial.print("Voltage: "); Serial.println(voltageVal, 0);
    Serial.print("psi= "); Serial.println(psi, 1);
    Serial.println("  ");
  }
  
  //Write values to the display
  
  display.clearDisplay(); // clear the display

  //buffer next display payload
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.print("Sensor: "); display.println(SensorType);
  display.print("Prog. ID: "); display.println(ProgramID);
  display.print("Raw: "); display.println(transducerVal);
  display.print("Filtered: "); display.println(filteredVal);
  display.print("Voltage: "); display.println(voltageVal);
  display.print("PSI: "); display.println(psi);
  display.print("IP:"); display.println(WiFi.localIP());
  display.print(uptimeTotal);

  display.display(); // Write the buffer to the display

  // if upload interval has passed since your last connection sent MQTT

  now = millis();
  if (now - lastMsg > mqtt_frequency) {
    lastMsg = now;
    sendMQTT(psi);
  }

  counter++;
}


//this method prints wifi network details
void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  Serial.print("Hostname: ");
  Serial.println(WiFi.getHostname());

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
  Serial.println("");
}


//MQTT Callback
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

}

//connect MQTT if not
void reconnect() {
  int mqtt_retries = 0;  
  
  // Loop until we're reconnected
  while (!pubsub_client.connected()) {
    Serial.print("Attempting MQTT connection...\n");
    // Create a random pubsub_client ID
    String clientId = ProgramID;
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (pubsub_client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("MQTT Connected.");
    } else {
      mqtt_retries++;      
      Serial.print("Failed, pubsub_client.state=");
      Serial.println(pubsub_client.state());
      Serial.print("Retries: "); Serial.println(mqtt_retries);
      Serial.println(" try again in 1 second...\n");

      // Wait 3 seconds before retrying
      delay(1000);
    }
    if(mqtt_retries==2){
      Serial.println("Too many retries. Looping.");
      return;
    }
  }
}

void sendMQTT(double PressureReading) {

  if (!pubsub_client.connected()) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Sensor: "); display.println(SensorType);
    display.print("Prog.ID: "); display.println(ProgramID);
    display.println("\nMQTT Offline!\n");
    display.print("Hostname: "); display.println(WiFi.getHostname());
    display.print("IP: "); display.println(WiFi.localIP());
    display.print(uptimeTotal);
    display.display();
    reconnect();
  }

   if(pubsub_client.connected()){
    //unsigned long now = millis();
    ++value;

    Serial.println("\nSending alert via MQTT...");

    //msg variable contains JSON string to send to MQTT server
    //snprintf (msg, MSG_BUFFER_SIZE, "\{\"amps\": %4.1f, \"humidity\": %4.1f\}", temperature, humidity);
    snprintf (msg, MSG_BUFFER_SIZE, "\{\"PSI\": %4.2f\}", PressureReading);
    //Due to a quirk with escaping special characters, if you're using an ESP8266 you will need to use this instead:
    //snprintf (msg, MSG_BUFFER_SIZE, "{\"temperature\": %4.1f, \"humidity\": %4.1f}", temperature, humidity);

    Serial.print("Publish message: ");
    Serial.println(msg);
    pubsub_client.publish("PRESSURE01", msg);

  }else{
    Serial.println("MQTT Not Connected... Bail on loop!\n");
  }



}