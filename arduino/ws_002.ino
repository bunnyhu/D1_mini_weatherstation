/**
 * WEMOS D1 MINI weatherstation
 * 
 * Main parts:
 * ============================
 * Wemos D1 mini
 * BM-P-280
 * DHT-22
 * Techno Line Tx20
 * 
 * @author Karoly Szabo - Bunny
 * @version 1.0
 * @release 2016. aug. 1.
 * 
*/

#include "config.h" // private variables

// WEBserver
  #include <ESP8266WiFi.h>
  #include <WiFiClient.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266HTTPClient.h>
#include <Wire.h>   // for I2C devices
#include <DHT.h>    // DHT22 Temp and humid sensor
// Air pressure
  #include <SPI.h>
  #include <Adafruit_Sensor.h>
  #include <Adafruit_BMP280.h>

// PIN SETTINGS
#define SOFT_SDA D4       // for i2c devices
#define SOFT_SCL D3       // for i2c devices
#define DHTPIN D1         // DHT22 Temp and humid sensor data pin 
#define TX20_DATAPIN D2   // TX20 TxD - data (brown) pin 
#define TX20_SWITCHPIN D5 // TX20 DTR - transer on/off (green) pin
#define LED 13            // Led pin number

#define DHTTYPE DHT22     // DHT 22  (AM2302), AM2321

// Other constants
const long interval = 30000;  // sensor reading period ms
const String owm_url = "http://openweathermap.org/data/post";
const String windDirectionNameEn[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};

// TX20 Wind sensor
volatile boolean TX20IncomingData = false;  // flag. If true, TX20 sending binary data. Turn on by interrupt method.
boolean TX20ValidData = false; // There is valid TX20 data
int tx20Direction[16];    // array to store directions between two internet data push
int tx20Speed;            // incremented wind speed. Need to divide with tx20SpeedIndex for average speed
int tx20Gust;             // highest wind speed
int tx20SpeedIndex = -1;  // how mutch wind speed data incremented in tx20Speed.

// Global variables
unsigned long previousMillis = 0;

unsigned char chk;          // TX20 temp var
unsigned char sa,sb,sd,se;  // TX20 temp var
unsigned int sc,sf, pin;    // TX20 temp var
String tx20RawDataS = "";   // TX20 binary data in string format for debugging

// The last weather information from the sensors
int stationTemp = 0;
int stationHeatIndex = 0;
int stationHumidity = 0;
float stationPressure = 0;
int stationWindDirection = 0;
String stationWindDirectionName = "";
int stationWindSpeed = 0;
int stationWindGust = 0;

unsigned long stationMillis = 0;  // timestamp for json data

// Initializing
DHT dht(DHTPIN, DHTTYPE); // Temp and humid sensor init
Adafruit_BMP280 bme;      // pressure sensor init
ESP8266WebServer server(80);  // wifi webserver at port 80

/**
 * TX20 windsensor interrupt method
 * 
 * If the TX20_DATAPIN start to pull high (rising period) its call this method.
 * If this happen, the TX20 start to sending data, we must read in loop.
 */
void isTX20Rising() {
  if (!TX20IncomingData) {
    TX20IncomingData = true;
  }  
}

/**
 * If the time is coming, do something.
 * long overflow also detected.
 * 
 * @return bool Need to do or not
 */
boolean timerOverflow() {
  unsigned long currentMillis = millis();
  unsigned long previousMillisCorrected = previousMillis;
  
  if (currentMillis < previousMillis) {
    currentMillis = currentMillis + (4294967295 - previousMillis);
    previousMillisCorrected = 0;
  }
  if ((currentMillis - previousMillisCorrected >= interval) || (previousMillisCorrected==0)) {
    previousMillis = currentMillis;
    return true;
  } else {
    return false;
  }  
}

/**
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   SYSTEM SETUP
 */
void setup(void){
  pinMode(LED, OUTPUT);
  pinMode(TX20_DATAPIN, INPUT);
  pinMode(TX20_SWITCHPIN, OUTPUT);

  Wire.pins(SOFT_SDA, SOFT_SCL);
  Wire.begin();         

  Serial.begin(74880);        // ESP8266 default speed is 74880, so we using, too.
  Serial.println("");
  digitalWrite(LED, 0);

  byte available_networks = WiFi.scanNetworks();
  if (available_networks>0) {
    Serial.println("Wifi connections available");
    for (int network = 0; network < available_networks; network++) {
      for (int f = 0; f<wifi_ssid_max; f++) {
        if (WiFi.SSID(network) == wifi_ssid[f][0]) {
          Serial.print("Found ");  
          Serial.println(wifi_ssid[f][0]);  
          WiFi.begin(wifi_ssid[f][0], wifi_ssid[f][1]);        
          network = available_networks;
        }
      }
    }    
  }  
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Gateway IP address: ");
  Serial.println(WiFi.gatewayIP());
  server.on("/", [](){ server.send(200, "text/plain", textOutput()); });  // root page
  server.on("/json", [](){ server.send(200, "text/plain", jsonOutput()); });  // json page
  server.onNotFound(handleNotFound);  // any other not found request
  server.begin();
  Serial.println("HTTP server started");
  dht.begin();
  if (!bme.begin(0x76)) {  
    Serial.println("Could not find a pressure sensor!");
  }
  delay(2000);
  readDHT();
  readBME(); 
  resetTX20();
  digitalWrite(TX20_SWITCHPIN, LOW);
  attachInterrupt(digitalPinToInterrupt(TX20_DATAPIN), isTX20Rising, RISING);
}

/**
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   SYSTEM LOOP
 */
void loop(void){
  if (TX20IncomingData) {
    boolean validData = readTX20();
    if (validData){      
      Serial.println("TX20 OK");
    } else {
      char a[90];
      Serial.println("");
      Serial.println(tx20RawDataS);
      sprintf(a, "ID: %d\t%d\n", sa, B00100);
      Serial.write (a);
      sprintf(a, "Wind direction: %d\t%d\n", sb, se);
      Serial.write (a);
      sprintf(a, "Wind speed: %d\t%d\n", sc, sf);
      Serial.write (a);
      sprintf(a, "Checksum: %d\t%d\n", sd, chk);
      Serial.write (a);
      Serial.println("!!! TX20 ERROR !!!");
      Serial.println("");
    }
  }

  server.handleClient();  
  unsigned long currentMillis = millis();

  if (timerOverflow()) {   
    stationMillis = currentMillis;
    readDHT();
    readBME();    
    calculateWind();
    resetTX20();
    sendRawAjax();
    sendOpenWeatherMap();
    sendWunderground();
    Serial.println(textOutput());
    Serial.println(" ");
  }
}

