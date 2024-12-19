//v1.1: Status Blink in LCD. 
////Rounding Humidity. 
////Indoor Temp/Hum Offset Option.
////Fixed Type: SHTSensor::SHT85
////Lora settings 
////Removed internal sensor, instead reads from Lora sensors - prefix IN/OUT

#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <Ethernet.h> // https://github.com/khoih-prog/Ethernet_Generic
#include <WiFi.h>
#include <LiquidCrystal_I2C.h> // https://github.com/johnrickman/LiquidCrystal_I2C
#include "SHTSensor.h" // https://github.com/Sensirion/arduino-sht
#include <AdafruitIO_WiFi.h>     
#include <AdafruitIO_Ethernet.h> 

// Define pins for LoRa
#define LORA_SCK 18   
#define LORA_MISO 19  
#define LORA_MOSI 23  
#define LORA_CS 5     
#define LORA_RST 27   
#define LORA_IRQ -1   

// Define pins for W5500 Ethernet Module
#define W5500_CS 4

// I2C LCD 
#define I2C_SDA 21
#define I2C_SCL 22

// LED Pins (for the bi-color LED)
#define RED_LED_PIN 25
#define GREEN_LED_PIN 26
const int RED_MAX_BRIGHTNESS = 255;
const int GREEN_MAX_BRIGHTNESS = 15;

// LoRa settings
const long frequency = 868100000; // 868.1 MHz
int currentSF = 7;        // Default Spreading Factor
long currentBW = 125000;  // Default Bandwidth in Hz (125 kHz)
int currentCR = 5;        // Default Coding Rate denominator (e.g., 5 for 4/5)

// Wi-Fi credentials
const char* ssid = "";
const char* password = "#";

// Adafruit IO credentials
#define IO_USERNAME ""
#define IO_KEY ""

const unsigned long outdoorDataTimeout = 150000; //set display to "---" if no data received after timeout
unsigned long lastOutdoorDataTime = 0; 
bool outdoorDataOld = true; // Start with no data (so display "---")

const unsigned long indoorDataTimeout = 60000; //set display to "---" if no data received after timeout
unsigned long lastIndoorDataTime = 0; 
bool indoorDataOld = true; // Start with no data (so display "---")

unsigned long lastNoInternetAttemptTime = 0;
const unsigned long noInternetRetryInterval = 300000;

unsigned long lastIoConnectAttempt = 0;
const unsigned long ioReconnectInterval = 300000;

unsigned long lastUploadTime = 0;
const int uploadCycle = 300000;  

const unsigned long blinkTimeout = 1000; 
unsigned long lastBlink = 0;
bool blink = true;
        
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

AdafruitIO_WiFi ioWiFi(IO_USERNAME, IO_KEY, ssid, password);
AdafruitIO_Ethernet ioEthernet(IO_USERNAME, IO_KEY);
AdafruitIO *io = nullptr; 
bool feedsInitialized = false;

AdafruitIO_Feed *indoorTempFeed;
AdafruitIO_Feed *indoorHumFeed;
AdafruitIO_Feed *indoorDewFeed;
AdafruitIO_Feed *outdoorTempFeed;
AdafruitIO_Feed *outdoorHumFeed;
AdafruitIO_Feed *outdoorDewFeed;

// ==================== setup() ====================
void setup() {
  delay(1000);
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(50000);

  setupLCDDisplay();
  setupLoRaModule();
  setupWiFiConnection();

  if (WiFi.status() != WL_CONNECTED) {
    setupEthernetConnection();
  }

  if (io != nullptr) {
    setupAdafruitIO();
  } else {
    lcd.clear();
    lcdPrint("No Internet", 1);
    delay(2000);
  }

  lcd.clear();
  setupLEDs();
}

void loop() {
  unsigned long currentMillis = millis();

  // Continuously listen for data
  while (LoRa.parsePacket()) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }
    Serial.print("Received: ");
    Serial.println(incoming);

    if (parseCSV(incoming)) {
      updateLCD();                        // Update the LCD with new data
      updateLED();
    } else {
      Serial.println("Failed to parse outdoor data.");
    }
  }

  // Check outdoor data timeout
  if (millis() - lastOutdoorDataTime > outdoorDataTimeout) {
    Serial.println("No new outdoor data. Display will show ---");
    outdoorDataOld = true; 
    updateLCD();
    setLEDtoRed();
  }

  // Check indoor data timeout
  if (millis() - lastIndoorDataTime > indoorDataTimeout) {
    Serial.println("No new indoor data. Display will show ---");
    indoorDataOld = true; 
    updateLCD();
    setLEDtoRed();
  }

  if (millis() - lastNoInternetAttemptTime >= noInternetRetryInterval) {
    lastNoInternetAttemptTime = currentMillis; // Update last retry attempt time
    if (!retryInternetConnection()) {
      Serial.println("Failed to reconnect to the internet.");
    }
  }

  // Skip Adafruit IO operations if not connected
  if (!io || io->status() != AIO_CONNECTED) {
    return; // Exit the loop early
  }

  // Maintain Adafruit IO connection
  io->run();

  // Upload data to Adafruit IO every uploadCycle
  if (currentMillis - lastUploadTime >= uploadCycle) {
    if (!feedsInitialized) {
      Serial.println("Initializing Adafruit IO feeds...");
      indoorTempFeed = io->feed("indoor_temperature");
      indoorHumFeed = io->feed("indoor_humidity");
      indoorDewFeed = io->feed("indoor_dew_point");
      outdoorTempFeed = io->feed("outdoor_temperature");
      outdoorHumFeed = io->feed("outdoor_humidity");
      outdoorDewFeed = io->feed("outdoor_dew_point");
      feedsInitialized = true;
    }

    // Upload data
    Serial.println("Send data to Adafruit feeds...");
    // Only upload outdoor data if not timed out
    if (!outdoorDataOld) {
      outdoorTempFeed->save(outdoorTemp);
      outdoorHumFeed->save(outdoorHum);
      outdoorDewFeed->save(outdoorDewPoint);
    }
    if (!indoorDataOld) {
      indoorTempFeed->save(indoorTemp);
      indoorHumFeed->save(indoorHum);
      indoorDewFeed->save(indoorDewPoint);
    }

    lastUploadTime = currentMillis;
  } 

  if(millis() - lastBlink > blinkTimeout) {
    lastBlink = millis();
    lcd.setCursor(19,3);
    if(blink) {
      lcd.print(".");
      blink = false;
    } else {
      lcd.print(" ");
      blink = true; 
    }
  }

  delay(10);
}


bool parseCSV(String data) {
  if (data.startsWith("OUT:")) {
    data = data.substring(4); 
    int tIndex = data.indexOf("T=");
    int hIndex = data.indexOf("H=");
    
    if (tIndex != -1 && hIndex != -1) {
      String tempStr = data.substring(tIndex + 2, data.indexOf(',', tIndex));
      String humStr = data.substring(hIndex + 2);
      
      outdoorTemp = tempStr.toFloat();
      outdoorHum = humStr.toFloat();
      outdoorDewPoint = calculateDewPoint(outdoorTemp, outdoorHum);

      lastOutdoorDataTime = millis();
      outdoorDataOld = false; // Data received, clear the reset flag
      return true; // Successfully parsed
    }
  } else if (data.startsWith("IN:")) {
    data = data.substring(3); 
    int tIndex = data.indexOf("T=");
    int hIndex = data.indexOf("H=");
    
    if (tIndex != -1 && hIndex != -1) {
      String tempStr = data.substring(tIndex + 2, data.indexOf(',', tIndex));
      String humStr = data.substring(hIndex + 2);
      
      indoorTemp = tempStr.toFloat();
      indoorHum = humStr.toFloat();
      indoorDewPoint = calculateDewPoint(indoorTemp, indoorHum);

      lastIndoorDataTime = millis();
      indoorDataOld = false; // Data received, clear the reset flag
      return true; // Successfully parsed
    }
  }
  return false; // Parsing failed
}

// ==================== Helper Methods ====================
void setupLCDDisplay() {
  lcd.init();
  lcd.backlight();
  lcdPrint("    Hallo Thomas",1);
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
  lcdPrint("Connecting WiFi ",1);
  WiFi.begin(ssid, password);

  unsigned long wifiStartTime = millis();
  const unsigned long wifiTimeout = 5000;
  lcd.setCursor(0,2);
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < wifiTimeout) {
    delay(500);
    lcd.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    io = &ioWiFi; 
    lcd.clear();
    lcdPrint("Connected to WiFi", 1);
    lcd.setCursor(0,2);
    lcd.print("IP: ");
    lcd.print(WiFi.localIP());
    delay(2000);
    lcd.clear();
  } else {
    lcd.clear();
    lcdPrint("WiFi Failed",1);
    delay(1000);
    lcd.clear();
  }
}

void setupEthernetConnection() {
  Ethernet.init(W5500_CS);
  lcd.clear();
  lcdPrint("Trying Ethernet ...", 1);

  unsigned long ethStartTime = millis();
  bool haveLink = false;

  lcd.setCursor(0,2);
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

  if (!Ethernet.begin(mac, 3000, 1000)) {
    lcd.clear();
    lcdPrint("No DHCP Response", 1);
    delay(2000);
    Serial.println("DHCP failed");
  } else {
    lcd.clear();
    lcd.setCursor(0,1);
    lcdPrint("Ethernet Connected",2);
    lcd.setCursor(0, 2);
    lcd.print("IP: ");
    lcd.print(Ethernet.localIP());
    io = &ioEthernet; 
    delay(2000);
    lcd.clear();
  }
}


void setupAdafruitIO() {
  if (io != nullptr) {
    lcd.clear();
    lcdPrint("Connecting Adafruit ", 1);
    adafruitIOConnection();
    delay(1000);
    lcd.clear();
  }
}

bool retryInternetConnection() {
  // Check if already connected
  if (io && io->status() == AIO_CONNECTED) {
    return true;
  }

  lastNoInternetAttemptTime = millis(); // Update last retry attempt time
  lcd.clear();
  lcdPrint("Reconnecting...", 1);

  // First try WiFi
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFiConnection();
  }

  // Check if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    if (io) {
      io->connect();
      Serial.println("Reconnected to Wi-Fi for Adafruit IO.");
    }
    return true;
  }

  // Try Ethernet if the cable is connected
  if (Ethernet.linkStatus() == LinkON) {
    setupEthernetConnection();
    if (io) {
      io->connect();
      Serial.println("Reconnected to Ethernet for Adafruit IO.");
    }
    return true;
  }

  // If no network is available
  if (WiFi.status() != WL_CONNECTED && Ethernet.linkStatus() != LinkON) {
    lcd.clear();
    lcdPrint("No Network", 1);
    Serial.println("No network available.");
    return false;
  }

  return false;
}

void adafruitIOConnection() {
  if (!io) return;

  if (io->status() == AIO_IDLE || io->status() < AIO_CONNECTED) {
    Serial.println("adafruitIOConnection: Attempting IO connect");
    unsigned long start = millis();
    while (io->status() < AIO_CONNECTED && (millis() - start) < 5000) {
      if ((WiFi.status() == WL_CONNECTED) ||
          (Ethernet.linkStatus() == LinkON && Ethernet.localIP() != IPAddress(0,0,0,0))) {
        io->run();
      } else {
        Serial.println("No network in adafruitIOConnection loop, breaking early");
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

void updateLCD() {
  lcd.setCursor(0, 0);
  lcd.print("In  T");
  if (!indoorDataOld) {
    printFormattedTemperature(indoorTemp);
    lcd.print((char)223);
    lcd.print("C");
    lcd.setCursor(14, 0);
    lcd.print("H");
    printFormattedHumidity((int)round(indoorHum));
    lcd.print("%");
  } else {
    lcd.print(" -----  ");
    lcd.setCursor(14,0);
    lcd.print("H ---");
  }

  lcd.setCursor(0, 1);
  lcd.print("In  D");
  if (!indoorDataOld) {
    printFormattedTemperature(indoorDewPoint);
    lcd.print((char)223);
    lcd.print("C");
  } else {
    lcd.print(" -----  ");
  }

  lcd.setCursor(0, 2);
  lcd.print("Out T");
  if (!outdoorDataOld) {
    printFormattedTemperature(outdoorTemp);
    lcd.print((char)223);
    lcd.print("C");
    lcd.setCursor(14, 2);
    lcd.print("H");
    printFormattedHumidity((int)round(outdoorHum));
    lcd.print("%");
  } else {
    lcd.print(" -----  ");
    lcd.setCursor(14,2);
    lcd.print("H ---");
  }

  lcd.setCursor(0, 3);
  lcd.print("Out D");
  if (!outdoorDataOld) {
    printFormattedTemperature(outdoorDewPoint);
    lcd.print((char)223);
    lcd.print("C");
  } else {
    lcd.print(" -----  ");
  }
}

void setLEDtoRed() {
    analogWrite(RED_LED_PIN, RED_MAX_BRIGHTNESS);
}

void updateLED() {
  if (indoorDataOld || outdoorDataOld) {
    // If either is reset, just show red (no data scenario)
    analogWrite(RED_LED_PIN, RED_MAX_BRIGHTNESS);
    analogWrite(GREEN_LED_PIN, 0);
    return;
  }

  float difference = indoorDewPoint - outdoorDewPoint;
  int redValue, greenValue;

  //if outdoor values == no data or difference < 0 => red
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
  lcd.setCursor(0, line); // Default to the specified line (0-indexed)
  lcd.print(text);
}
