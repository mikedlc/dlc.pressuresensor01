/*********************************************************************
This sketch is intended to run on a D1 Mini. It reads pressure 
from a transducer and sends the readings to tago.io.

This sketch uses the 1.3" OLED display, SH1106 128x64, I2C comms.

x1Val and x2Val must be calibrated to the voltage readings at
0 and 100 PSI respectively for the output to be accurate.

*********************************************************************/

//This ProgramID is the name of the sketch and identifies what code is running on the D1 Mini
const char* ProgramID = "LMWA002";
const char* SensorType = "Pressure";
int SerialOn = 0;

#include <SPI.h>
#include <Wire.h>

//Function definitions
float roundoff(float value, unsigned char prec);
void httpRequest();
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
unsigned long currentMillis = 0;
int uptimeSeconds = 0;
int uptimeDays;
int uptimeHours;
int secsRemaining;
int uptimeMinutes;
char uptimeTotal[30];


unsigned long lastConnectionTime = 0;            // last time you connected to the server, in milliseconds
unsigned long postingInterval = 10 * 1000; // delay between updates, in milliseconds


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
void callback(char* topic, byte* payload, unsigned int length);
void reconnect();
void sendMQTT(double PowerReading);
const char* mqtt_server = "homeassistant.local";  //Your network's MQTT server (usually same IP address as Home Assistant server)
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

}

void loop() {
  currentMillis = millis();
  delay (.01); //sample delay

  uptimeSeconds=currentMillis/1000;
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
      Serial.println("PSI<0, Making it 0.");
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

  // if upload interval has passed since your last connection,
  // then connect again and send data to tago.io

  if(SerialOn){
  Serial.print("currentMillis: "); Serial.println(currentMillis, 0);
  Serial.print("lastConnectionTime: "); Serial.println(lastConnectionTime, 0);
  Serial.print("PostingInterval: "); Serial.println(postingInterval, 0);
  }

  //if (currentMillis - lastConnectionTime > postingInterval) {
  //  Serial.print("Time to post to tago.io at "); Serial.println(uptimeTotal);
    // then, send data to Tago
    //httpRequest();
  //}
  sendMQTT(psi);
  Serial.println();
  counter++;
}

// this method makes a HTTP connection to tago.io
void httpRequest() {
/*
  Serial.println("Sending this Pressure:");
  Serial.println(psi);

    // close any connection before send a new request.
    // This will free the socket on the WiFi shield
    wifi_client.stop();

    Serial.println("Starting connection to server for Pressure...");
    // if you get a connection, report back via serial:
    String PostPressure = String("{\"variable\":\"pressure\", \"value\":") + String(psi)+ String(",\"unit\":\"PSI\"}");
    String Dev_token = String("Device-Token: ")+ String(Device_Token);
    if (wifi_client.connect(server,80)) {                      // we will use non-secured connnection (HTTP) for tests
    Serial.println("Connected to server");
    // Make a HTTP request:
    wifi_client.println("POST /data? HTTP/1.1");
    wifi_client.println("Host: api.tago.io");
    wifi_client.println("_ssl: false");                        // for non-secured connection, use this option "_ssl: false"
    wifi_client.println(Dev_token);
    wifi_client.println("Content-Type: application/json");
    wifi_client.print("Content-Length: ");
    wifi_client.println(PostPressure.length());
    wifi_client.println();
    wifi_client.println(PostPressure);
    Serial.println("Pressure sent!\n");
    }  else {
      // if you couldn't make a connection:
      Serial.println("Server connection failed.");
    }

    client.stop();

    // note the time that the connection was made:
    lastConnectionTime = currentMillis;
    */
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
  // Loop until we're reconnected
  while (!pubsub_client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random pubsub_client ID
    String clientId = "PRESSURESENSOR-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (pubsub_client.connect(clientId.c_str(), "mqttuser", "Quik5ilver7")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(pubsub_client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void sendMQTT(double PressureReading) {

  if (!pubsub_client.connected()) {
    reconnect();
    Serial.println("Reconnect MQTT");
  }

  unsigned long now = millis();
  if (now - lastMsg > 2000) {
    lastMsg = now;
    ++value;

  Serial.println("Sending alert via MQTT...");
  Serial.println(PressureReading);

    //msg variable contains JSON string to send to MQTT server
    //snprintf (msg, MSG_BUFFER_SIZE, "\{\"amps\": %4.1f, \"humidity\": %4.1f\}", temperature, humidity);
    snprintf (msg, MSG_BUFFER_SIZE, "\{\"PSI\": %4.2f\}", PressureReading);
    //Due to a quirk with escaping special characters, if you're using an ESP8266 you will need to use this instead:
    //snprintf (msg, MSG_BUFFER_SIZE, "{\"temperature\": %4.1f, \"humidity\": %4.1f}", temperature, humidity);

    Serial.print("Publish message: ");
    Serial.println(msg);
    pubsub_client.publish("PRESSURE01", msg);
  }

}