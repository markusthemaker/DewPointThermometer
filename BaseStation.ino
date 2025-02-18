/*
  BaseStation.ino

  This sketch receives LoRa sensor data, displays it on an LCD,
  and (optionally) uploads the data using one of three platform options:
    - PLATFORM_ADAFRUIT
    - PLATFORM_THINGSPEAK
    - PLATFORM_NONE (no upload)

Base Station Wiring Diagram:

1. ESP32 to I2C LCD Display:
   ESP32 Pin    LCD Pin        Notes
   ---------    --------       ------------------------------
   3.3V         VCC            Power supply (3.3V)
   GND          GND            Ground connection
   GPIO 21      SDA            I2C Data line
   GPIO 22      SCL            I2C Clock line

2. ESP32 to LoRa SX1276 Module:
   ESP32 Pin    LoRa Pin       Notes
   ---------    --------       ------------------------------
   3.3V         VCC            Power supply (3.3V)
   GND          GND            Ground connection
   GPIO 18      SCK            SPI Clock (shared if needed)
   GPIO 19      MISO           SPI Master In, Slave Out (shared if needed)
   GPIO 23      MOSI           SPI Master Out, Slave In (shared if needed)
   GPIO 5       NSS (CS)       Chip Select (CS) for LoRa
   GPIO 27      RESET          LoRa module reset
                IRQ            Not connected (set to -1 in code)

3. ESP32 to Ethernet (W5500) Module:
   ESP32 Pin    Ethernet Pin   Notes
   ---------    ------------   ------------------------------
   3.3V/5V*     VCC            Power supply (use voltage per module spec)
   GND          GND            Ground connection
                SCK            Connect to ESP32 SPI SCK (GPIO18; shared with LoRa)
                MISO           Connect to ESP32 SPI MISO (GPIO19; shared with LoRa)
                MOSI           Connect to ESP32 SPI MOSI (GPIO23; shared with LoRa)
   GPIO 4       CS             Chip Select (W5500_CS)

4. ESP32 to LED Indicators:
   ESP32 Pin    LED            Notes
   ---------    ---            ------------------------------
   GPIO 25      Red LED        Red indicator (via current-limiting resistor)
   GPIO 26      Green LED      Green indicator (via current-limiting resistor)

5. ESP32 to LDR Sensor (Ambient Light Control):
   ESP32 Pin    Component      Notes
   ---------    ----------     ------------------------------
   GPIO 34      LDR            Connect one end of the LDR to 3.3V and the other end to GPIO34.
                               Then, add a resistor from GPIO34 to GND (forming a voltage divider).
                               Adjust the resistor value to set the desired light threshold.

* Verify the Ethernet module’s voltage requirements (often 3.3V or 5V) and use the appropriate supply.
*/
*/

#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>               // https://github.com/sandeepmistry/arduino-LoRa
#include <Ethernet.h>           // https://github.com/PaulStoffregen/Ethernet
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>  // https://github.com/johnrickman/LiquidCrystal_I2C
#include "SHTSensor.h"          // https://github.com/Sensirion/arduino-sht
#include <AdafruitIO_WiFi.h>
#include <AdafruitIO_Ethernet.h>
#include <ThingSpeak.h>         // https://thingspeak.mathworks.com/

// Include our uploader header (with inline implementations)
#include "DataUploader.h"

// -------------------- LDR Definitions --------------------
#define LDR_PIN 34         // Analog pin where the LDR is connected (e.g., GPIO34 on ESP32)
#define LDR_THRESHOLD 25   // Adjust this value (empirically) so that readings indicate it's very dark

// -------------------- Pin Definitions --------------------
// SPI Bus pins
#define LORA_SCK 18   
#define LORA_MISO 19  
#define LORA_MOSI 23  

// SPI LoRa pins
#define LORA_CS 5     
#define LORA_RST 27   
#define LORA_IRQ -1   

// SPI Ethernet Module pin (W5500)
#define W5500_CS 4

// I2C LCD pins
#define I2C_SDA 21
#define I2C_SCL 22

// LED pins (bi–color LED)
#define RED_LED_PIN 25
#define GREEN_LED_PIN 26
const int RED_MAX_BRIGHTNESS = 255;
const int GREEN_MAX_BRIGHTNESS = 15;

// -------------------- LoRa Settings --------------------
const long frequency = 868300000; // 868.3 MHz
const int currentSF = 10;         // SF10
const long currentBW = 125000;    // 125 kHz
const int currentCR = 6;          // Coding rate 4/6

// -------------------- Network Credentials --------------------
const char* ssid = "x";
const char* password = "x";

// -------------------- Platform Selection & Credentials  --------------------
// Add PLATFORM_NONE for no upload.
enum DataPlatformType { PLATFORM_ADAFRUIT, PLATFORM_THINGSPEAK, PLATFORM_NONE };
const DataPlatformType selectedPlatform = PLATFORM_THINGSPEAK; // Change as needed

// Adafruit configuration (if using Adafruit)
#define IO_USERNAME "x"
#define IO_KEY "x"

// ThingSpeak configuration (if using ThingSpeak)
const unsigned long tsChannelID = x;
const char* tsWriteAPIKey = "x";

// -------------------- Timeouts and Intervals --------------------
const unsigned long outdoorDataTimeout = 300000; // ms (if no new data, display "---")
unsigned long lastOutdoorDataTime = 0; 
bool outdoorDataOld = true; // true means no recent data

const unsigned long indoorDataTimeout = 300000; // 5 minutes 
unsigned long lastIndoorDataTime = 0;
bool indoorDataOld = true;

unsigned long lastUploadTime = 0;
const int uploadCycle = 300000; // 5 minutes 

unsigned long lastNoInternetAttemptTime = 0;
const unsigned long noInternetRetryInterval = 300000; // 5 minutes

const unsigned long blinkTimeout = 1000; 
unsigned long lastBlink = 0;
bool blink = true;
        
// -------------------- Global Objects --------------------
SHTSensor sht(SHTSensor::SHT85);
LiquidCrystal_I2C lcd(0x27, 20, 4);

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
EthernetClient ethClient;

float indoorTemp = 0.0;
float indoorHum = 0.0;
float indoorDewPoint = 0.0;
float outdoorTemp = 0.0;
float outdoorHum = 0.0;
float outdoorDewPoint = 0.0;
// New global variable for battery voltage (default to 0 if no reading received)
float batteryVoltage = 0.0;

// Adafruit IO objects
AdafruitIO_WiFi ioWiFi(IO_USERNAME, IO_KEY, ssid, password);
AdafruitIO_Ethernet ioEthernet(IO_USERNAME, IO_KEY);
AdafruitIO *io = nullptr; 

// ThingSpeak IO objects 
WiFiClient tsWiFiClient;
EthernetClient tsEthClient; 
Client *tsClient = nullptr;

// Data uploader pointer (may be null if PLATFORM_NONE or if no network available)
DataUploader *uploader = nullptr;

// -------------------- Function Prototypes --------------------
void setupLCDDisplay();
void setupLEDs();
void setupLoRaModule();
void setupWiFiConnection();
void setupEthernetConnection();
bool retryInternetConnection();
void adafruitIOConnection();
bool parseCSV(String data);
double calculateDewPoint(double temp, double hum);
double calculateWaterVapor(double dewPoint);
void printFormattedTemperature(float temperature);
void printFormattedHumidity(int humidity);
void lcdPrint(const String &text, int line);
void updateLCD();
void updateLED();
void setLEDtoRed();

void setup() {
  delay(1000);
  Serial.begin(460800);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(50000);
  
  setupLCDDisplay();
  setupLoRaModule();
  setupWiFiConnection();
  
  // If WiFi is not connected, attempt Ethernet if hardware is present.
  if (WiFi.status() != WL_CONNECTED) {
    setupEthernetConnection();
  }
  
  // Set up network for Adafruit if that's selected:
  if (selectedPlatform == PLATFORM_ADAFRUIT) {
    if (WiFi.status() == WL_CONNECTED)
      io = &ioWiFi;
    else if (Ethernet.linkStatus() == LinkON)
      io = &ioEthernet;

    lcd.clear();
    lcdPrint("Connecting Adafruit", 1);
    adafruitIOConnection();
    uploader = new AdafruitUploader(io);
    uploader->begin();  
    delay(1000);
    lcd.clear();
  }
  
  // Set up network client for ThingSpeak if that's selected:
  if (selectedPlatform == PLATFORM_THINGSPEAK) {
    if (WiFi.status() == WL_CONNECTED)
      tsClient = &tsWiFiClient;
    else if (Ethernet.linkStatus() == LinkON)
      tsClient = &tsEthClient;
    
    if (tsClient) {
      uploader = new ThingSpeakUploader(tsClient, tsChannelID, tsWriteAPIKey);
      uploader->begin();
    }

    lcd.clear();
    lcdPrint("Connect Thingspeak", 1);
    delay(1000);
    lcd.clear();
  }
    
  lcd.clear();
  setupLEDs();
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Listen for incoming LoRa packets.
  while (LoRa.parsePacket()) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }
    Serial.print("Received: ");
    Serial.println(incoming);
    if (parseCSV(incoming)) {
      updateLCD();
      updateLED();
    } else {
      Serial.println("Failed to parse incoming data.");
    }
  }
  
  // Check for outdoor and indoor data timeout.
  if (millis() - lastOutdoorDataTime > outdoorDataTimeout) {
    Serial.println("No new outdoor data.");
    outdoorDataOld = true;
    updateLCD();
    setLEDtoRed();
  }
  if (millis() - lastIndoorDataTime > indoorDataTimeout) {
    Serial.println("No new indoor data.");
    indoorDataOld = true;
    updateLCD();
    setLEDtoRed();
  }
  
  if (millis() - lastBlink > blinkTimeout) {
    lastBlink = millis();
    lcd.setCursor(19, 3);
    if (blink) {
      lcd.print(".");
      blink = false;
    } else {
      lcd.print(" ");
      blink = true;
    }
  }
  
  // Retry Internet connection if needed.
  if (millis() - lastNoInternetAttemptTime >= noInternetRetryInterval) {
    lastNoInternetAttemptTime = currentMillis;
    if (!retryInternetConnection()) {
      Serial.println("Failed to reconnect.");
    }
  }
  
  // If Adafruit is selected, run its connection loop.
  if (selectedPlatform == PLATFORM_ADAFRUIT) {
    if (io) {
      uploader->run();
      // Adafruit IO uses a persistent, event-driven connection.
      // Its run() method must be called continuously.
    }
  }

  // Upload sensor data at defined intervals.
  if (currentMillis - lastUploadTime >= uploadCycle) {
    // Only attempt upload if, for Adafruit, the connection is established.
    if (selectedPlatform != PLATFORM_ADAFRUIT || (io && io->status() == AIO_CONNECTED)) {
      SensorData sensorData;
      sensorData.indoorTemp = indoorTemp;
      sensorData.indoorHum  = indoorHum;
      sensorData.indoorDew  = indoorDewPoint;
      sensorData.outdoorTemp = outdoorTemp;
      sensorData.outdoorHum  = outdoorHum;
      sensorData.outdoorDew  = outdoorDewPoint;
      sensorData.indoorDataValid = !indoorDataOld;
      sensorData.outdoorDataValid = !outdoorDataOld;
      // Include battery voltage reading from the third LoRa station.
      sensorData.batteryVoltage = batteryVoltage;
      
      if (uploader) {
        Serial.println("Uploading data...");
        uploader->uploadData(sensorData);
      }
      lastUploadTime = currentMillis;
    }
  }
    
  delay(10);
}

// -------------------- Function Definitions --------------------
void setupLCDDisplay() {
  lcd.init();
  lcd.backlight();
  lcdPrint("      Hallo", 1);
  lcdPrint("Taupunkt Thermometer", 2);
  delay(3000);
  lcd.clear();
}

void setupLEDs() {
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  analogWrite(RED_LED_PIN, 0);
  analogWrite(GREEN_LED_PIN, 0);
}

void setupLoRaModule() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin(frequency)) {
    lcdPrint("LoRa Failed", 1);
  } else {
    lcdPrint("LoRa Initialized", 1);
    LoRa.setSyncWord(0x13);
    LoRa.enableCrc();
    LoRa.setSpreadingFactor(currentSF);
    LoRa.setSignalBandwidth(currentBW);
    LoRa.setCodingRate4(currentCR);
  }
  delay(1000);
  lcd.clear();
}

void setupWiFiConnection() {
  lcd.clear();
  lcdPrint("Connecting WiFi", 1);
  WiFi.begin(ssid, password);
  unsigned long wifiStartTime = millis();
  const unsigned long wifiTimeout = 10000;
  lcd.setCursor(0, 2);
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < wifiTimeout) {
    delay(500);
    lcd.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    lcdPrint("Connected to WiFi", 1);
    lcd.setCursor(0, 2);
    lcd.print("IP: ");
    lcd.print(WiFi.localIP());
    delay(2000);
    lcd.clear();
  } else {
    lcd.clear();
    lcdPrint("WiFi Failed", 1);
    delay(1000);
    lcd.clear();
  }
}

void setupEthernetConnection() {
  Ethernet.init(W5500_CS);  // Initialize SPI and CS pin
  
  lcd.clear();
  lcdPrint("Trying Ethernet ...", 1);
  unsigned long ethStartTime = millis();
  bool haveLink = false;
  lcd.setCursor(0, 2);
  while ((millis() - ethStartTime) < 3000) {
    if (Ethernet.linkStatus() == LinkON) {
      haveLink = true;
      break;
    }
    delay(500);
    lcd.print(".");
  }
  if (!haveLink) {
    lcd.clear();
    lcdPrint("No Ethernet Link", 1);
    delay(2000);
    return;
  }
  
  // Attempt to initialize with DHCP
  if (!Ethernet.begin(mac, 3000, 1000)) {
    lcd.clear();
    lcdPrint("No DHCP Response", 1);
    delay(2000);
    Serial.println("DHCP failed");
    return;
  }
  
  // Check for a valid IP address.
  IPAddress ip = Ethernet.localIP();
  if (ip == IPAddress(0, 0, 0, 0)) {
    lcd.clear();
    lcdPrint("Ethernet init fail", 1);
    delay(2000);
    Serial.println("Ethernet init failed (no valid IP)");
    return;
  }
  
  lcd.clear();
  lcd.setCursor(0, 1);
  lcdPrint("Ethernet Connected", 2);
  lcd.setCursor(0, 2);
  lcd.print("IP: ");
  lcd.print(ip);
  io = &ioEthernet;
  delay(2000);
  lcd.clear();
}

bool retryInternetConnection() {
  if (selectedPlatform == PLATFORM_ADAFRUIT) {
    if (io && io->status() == AIO_CONNECTED)
      return true;
  } else {
    if (WiFi.status() == WL_CONNECTED || Ethernet.linkStatus() == LinkON)
      return true;
  }
  lastNoInternetAttemptTime = millis();
  lcd.clear();
  lcdPrint("Reconnecting...", 1);
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFiConnection();
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (io) {
      io->connect();
      Serial.println("Reconnected to Wi-Fi for Adafruit IO.");
    }
    return true;
  }
  if (Ethernet.linkStatus() == LinkON) {
    setupEthernetConnection();
    if (io) {
      io->connect();
      Serial.println("Reconnected to Ethernet for Adafruit IO.");
    }
    return true;
  }

  lcd.clear();
  lcdPrint("No Network", 1);
  Serial.println("No network available.");
  return false;
}

void adafruitIOConnection() {
  if (!io) return;
  if (io->status() == AIO_IDLE || io->status() < AIO_CONNECTED) {
    Serial.println("Attempting Adafruit IO connection...");
    unsigned long start = millis();
    while (io->status() < AIO_CONNECTED && (millis() - start) < 5000) {
      if ((WiFi.status() == WL_CONNECTED) ||
          (Ethernet.linkStatus() == LinkON && Ethernet.localIP() != IPAddress(0,0,0,0))) {
        io->run();
      } else {
        Serial.println("No network during Adafruit IO connect loop, breaking early");
        break;
      }
      lcd.print(". ");
      delay(500);
    }
    bool connected = (io->status() == AIO_CONNECTED);
    lcd.clear();
    if (connected) { 
      lcdPrint("Adafruit Connected", 1);
      Serial.println("Adafruit connected");
    } else { 
      lcdPrint("Adafruit Failed", 1);
      Serial.println("Adafruit failed");
    }
    delay(1000);
    lcd.clear();
  }
}

bool parseCSV(String data) {  
  data.trim();  // Remove any leading/trailing whitespace and newline characters
  if (data.startsWith("OUT:")) {
    data = data.substring(4);
    int tIndex = data.indexOf("T=");
    int hIndex = data.indexOf("H=");
    if (tIndex != -1 && hIndex != -1) {
      String tempStr = data.substring(tIndex + 2, data.indexOf(',', tIndex));
      String humStr = data.substring(hIndex + 2);
      outdoorTemp = tempStr.toFloat();
      outdoorHum  = humStr.toFloat();
      outdoorDewPoint = calculateDewPoint(outdoorTemp, outdoorHum);
      lastOutdoorDataTime = millis();
      outdoorDataOld = false;
      return true;
    }

  } else if (data.startsWith("IN:")) {
    data = data.substring(3);
    int tIndex = data.indexOf("T=");
    int hIndex = data.indexOf("H=");
    if (tIndex != -1 && hIndex != -1) {
      String tempStr = data.substring(tIndex + 2, data.indexOf(',', tIndex));
      String humStr = data.substring(hIndex + 2);
      indoorTemp = tempStr.toFloat();
      indoorHum  = humStr.toFloat();
      indoorDewPoint = calculateDewPoint(indoorTemp, indoorHum);
      lastIndoorDataTime = millis();
      indoorDataOld = false;
      return true;
    }
  }

  if (data.startsWith("V:")) {
    // Extract the voltage value from the string (e.g., "V:12.3")
    String voltageStr = data.substring(2);
    batteryVoltage = voltageStr.toFloat();
    Serial.print("Battery Voltage updated: ");
    Serial.println(batteryVoltage);
    return true;
  }

  return false;
}

double calculateDewPoint(double temp, double hum) {
  double a, b;
  if (temp >= 0.0) {
    a = 17.62; b = 243.12;
  } else {
    a = 22.46; b = 272.62;
  }
  double rh = hum / 100.0;
  double alpha = log(rh) + (a * temp / (b + temp));
  return (b * alpha) / (a - alpha);
}

// ---- New function: Calculate water vapor density (g/m³) from dewpoint ----
double calculateWaterVapor(double dewPoint) {
  // Using an approximate formula:
  double e = 6.112 * exp((17.62 * dewPoint) / (243.12 + dewPoint));  // in hPa
  double density = 216.7 * (e / (dewPoint + 273.15));  // in g/m³
  return density;
}

void printFormattedTemperature(float temperature) {
  char tempBuffer[7];
  snprintf(tempBuffer, sizeof(tempBuffer), "%+6.1f", temperature);
  lcd.print(tempBuffer);
}

void printFormattedHumidity(int humidity) {
  char humBuffer[4];
  snprintf(humBuffer, sizeof(humBuffer), "%3d", humidity);
  lcd.print(humBuffer);
}

void lcdPrint(const String &text, int line) {
  lcd.setCursor(0, line);
  lcd.print(text);
}

// Helper function to print temperature and humidity on one line.
// 'label' is the string to print at the beginning (e.g. "I: T" or "O: T").
// 'temp' and 'hum' are the temperature and humidity values.
// 'valid' indicates whether valid data is available.
// 'row' is the LCD row (0 or 2).
void printTempHum(const char* label, float temp, float hum, bool valid, int row) {
  lcd.setCursor(0, row);
  lcd.print(label);
  if (valid) {
    printFormattedTemperature(temp);
    lcd.print((char)223);  // Degree symbol
    lcd.print("C");
    lcd.setCursor(13, row);
    lcd.print("H ");
    char humStr[6];
    // Using float formatting (xx.x) for humidity:
    snprintf(humStr, sizeof(humStr), "%4.1f", hum);
    lcd.print(humStr);
    lcd.print("%");
  } else {
    lcd.print(" -----  ");
    lcd.setCursor(13, row);
    lcd.print("H ----");
  }
}

// Helper function to print dew point and water vapor density on one line.
// 'label' is the string to print (e.g. "I: D" or "O: D").
// 'dew' is the dew point value.
// 'valid' indicates valid data.
// 'row' is the LCD row (1 or 3).
void printDewWater(const char* label, float dew, bool valid, int row) {
  lcd.setCursor(0, row);
  lcd.print(label);
  if (valid) {
    printFormattedTemperature(dew);
    lcd.print((char)223);
    lcd.print("C");
    lcd.setCursor(13, row);
    double water = calculateWaterVapor(dew);  // Assume this function is defined elsewhere.
    char waterStr[9];
    snprintf(waterStr, sizeof(waterStr), "G %4.1f", water);
    lcd.print(waterStr);
  } else {
    lcd.print(" -----  ");
    lcd.setCursor(13, row);
    lcd.print("       ");
  }
}

// Simplified updateLCD() using the helper functions.
void updateLCD() {
  // Line 0: Indoor Temperature and Humidity
  printTempHum("I: T", indoorTemp, indoorHum, !indoorDataOld, 0);
  
  // Line 1: Indoor Dew Point and Water Vapor Density
  printDewWater("I: D", indoorDewPoint, !indoorDataOld, 1);
  
  // Line 2: Outdoor Temperature and Humidity
  printTempHum("O: T", outdoorTemp, outdoorHum, !outdoorDataOld, 2);
  
  // Line 3: Outdoor Dew Point and Water Vapor Density
  printDewWater("O: D", outdoorDewPoint, !outdoorDataOld, 3);
  
  // LDR Logic: If ambient light (analog read on LDR_PIN) is below threshold, turn off backlight.
  if (analogRead(LDR_PIN) < LDR_THRESHOLD)
    lcd.noBacklight();
  else
    lcd.backlight();
}

void updateLED() {
  // NO LED at Night 
  if (analogRead(LDR_PIN) < LDR_THRESHOLD) {
    analogWrite(GREEN_LED_PIN, 0);
    analogWrite(RED_LED_PIN, 0);
    return;
  }
  // Red LED if no Data
  if (indoorDataOld || outdoorDataOld) {
    analogWrite(RED_LED_PIN, RED_MAX_BRIGHTNESS);
    analogWrite(GREEN_LED_PIN, 0);
    return;
  }
  // Red / Green / No LED depending on Dewpoint Delta
  float difference = indoorDewPoint - outdoorDewPoint;
  int redValue, greenValue;
  if (difference <= 0) {
    redValue = RED_MAX_BRIGHTNESS;
    greenValue = 0;
  } else if (difference > 5) {
    redValue = 0;
    greenValue = GREEN_MAX_BRIGHTNESS;
  } else {
    redValue = 0;
    greenValue = 0;
  }
  analogWrite(RED_LED_PIN, redValue);
  analogWrite(GREEN_LED_PIN, greenValue);
}

void setLEDtoRed() {
  analogWrite(RED_LED_PIN, RED_MAX_BRIGHTNESS);
}
