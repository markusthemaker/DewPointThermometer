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

// I2C LCD and Temp Sensor
#define I2C_SDA 21
#define I2C_SCL 22

// LED Pins (for the bi-color LED)
#define RED_LED_PIN 25
#define GREEN_LED_PIN 26
const int RED_MAX_BRIGHTNESS = 255;
const int GREEN_MAX_BRIGHTNESS = 15;

const long frequency = 868100000; // 868.1 MHz

// Wi-Fi credentials
const char* ssid = "";
const char* password = "";

// Adafruit IO credentials
#define IO_USERNAME ""
#define IO_KEY ""

unsigned long lastIoConnectAttempt = 0;
const unsigned long ioReconnectInterval = 30000;

unsigned long lastNoInternetAttemptTime = 0;
const unsigned long noInternetRetryInterval = 60000;

unsigned long lastUploadTime = 0;
const int uploadCycle = 300000;   // Upload data every 5 minutes

const int maxSensorFailures = 5; // Maximum consecutive sensor failures
int sensorFailureCount = 0; // Counter for sensor failures

const unsigned long outdoorDataTimeout = 60000; // 1 minute

SHTSensor sht(SHTSensor::SHT3X);
LiquidCrystal_I2C lcd(0x27, 20, 4);

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
EthernetClient ethClient;

float indoorTemp = 0.0;
float indoorHum = 0.0;
float indoorDewPoint = 0.0;
float outdoorTemp = 0.0;
float outdoorHum = 0.0;
float outdoorDewPoint = 0.0;
unsigned long lastOutdoorDataTime = 0; 

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
  Serial.begin(115200);

  setupLCDDisplay();
  setupSHT85();
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

// ==================== loop() ====================
void loop() {
  unsigned long currentMillis = millis();

  // Handle sensor reading and recovery
  handleSensorRead();

  // Request outdoor data and update lastOutdoorDataTime if new data is received
  bool newData = requestOutdoorData();
  if (newData) {
    lastOutdoorDataTime = currentMillis;
  }

  // Reset outdoor values if no new data for the timeout duration
  if (currentMillis - lastOutdoorDataTime > outdoorDataTimeout) {
    outdoorTemp = 0.0;
    outdoorHum = 0.0;
    outdoorDewPoint = 0.0;
    Serial.println("No new outdoor data for 1 minute. Resetting outdoor values to 0.");
  }

  // Update the LCD and LEDs regardless of network status
  updateLCD();
  updateLED();
  
  // Handle network reconnection attempts
  bool connected = retryInternetConnection();

  // Important, check if connection available, otherwise any io-> method gets "stuck"
  // Always call io->run() to maintain the connection
  if (connected && io) {
    io->run();
  }

  // Upload data to Adafruit IO every uploadCycle
  if (connected && io && io->status() == AIO_CONNECTED && (currentMillis - lastUploadTime >= uploadCycle)) {
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
    indoorTempFeed->save(indoorTemp);
    indoorHumFeed->save(indoorHum);
    indoorDewFeed->save(indoorDewPoint);

    if (newData) {
      outdoorTempFeed->save(outdoorTemp);
      outdoorHumFeed->save(outdoorHum);
      outdoorDewFeed->save(outdoorDewPoint);
    }

    lastUploadTime = currentMillis;
  } else if (!connected || io->status() != AIO_CONNECTED) {
    feedsInitialized = false; // Mark feeds as uninitialized if disconnected
    Serial.println("Not connected to Adafruit IO, skipping upload.");
  }
  delay(5000);
}

// ==================== Sensor Handling ====================
void handleSensorRead() {
  if (sht.readSample()) {
    // Successful sensor read
    indoorTemp = sht.getTemperature();
    indoorHum = sht.getHumidity();
    indoorDewPoint = calculateDewPoint(indoorTemp, indoorHum);

    // Reset failure counter on success
    sensorFailureCount = 0;
    Serial.println("SHT85: Sensor read successful.");
  } else {
    // Increment failure counter on failure
    sensorFailureCount++;
    Serial.println("SHT85: Sensor read failed.");

    // Check if reinitialization is needed
    if (sensorFailureCount >= maxSensorFailures) {
      Serial.println("SHT85: Too many failures, attempting reinitialization...");
      if (sht.init()) {
        sensorFailureCount = 0; // Reset counter if reinitialization succeeds
        Serial.println("SHT85: Reinitialized successfully.");
      } else {
        // Log critical failure if reinitialization fails
        Serial.println("SHT85: Reinitialization failed. Sensor might be offline.");
      }
    }
  }
}


// ==================== Helper Methods ====================
void setupLCDDisplay() {
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcdPrint("    Hallo Thomas",1);
  lcdPrint("Taupunkt Thermometer", 2);
  delay(3000);
  lcd.clear();
}

void setupSHT85() {
  if (sht.init()) {
    lcdPrint("SHT85 Initialized", 1);
  } else {
    lcdPrint("Failed Sensor", 1);
  }
  delay(1000);
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
    // Set sync word once
    LoRa.setSyncWord(0x13); 
    // Enable CRC once
    LoRa.enableCrc();
  }
  delay(1000);
  lcd.clear();
}

void setupWiFiConnection() {
  lcd.clear();
  lcdPrint("Connecting WiFi ",1);
  WiFi.begin(ssid, password);

  unsigned long wifiStartTime = millis();
  const unsigned long wifiTimeout = 10000;
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
    lcd.setCursor(0, 3);
    lcd.print("RSSI: ");
    lcd.print(WiFi.RSSI());
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
  while ((millis() - ethStartTime) < 5000) {
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
    lcdPrint("Ethernet Connected",2);
    lcd.setCursor(0, 3);
    lcd.print("IP: ");
    lcd.print(Ethernet.localIP());
    io = &ioEthernet; 
    delay(2000);
    lcd.clear();
  }
}

bool retryInternetConnection() {
  if (io && io->status() == AIO_CONNECTED) {
    //Serial.println("Adafruit IO is connected.");
    return true;
  }

  if (millis() - lastNoInternetAttemptTime < noInternetRetryInterval) {
    Serial.println("Not time to retry internet yet.");
    return false;
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

  if (WiFi.status() != WL_CONNECTED && Ethernet.linkStatus() != LinkON) {
    lcd.clear();
    lcdPrint("No Network", 1);
    Serial.println("No network available.");
    return false;
  }

  if (Ethernet.linkStatus() == LinkON) {
    setupEthernetConnection();
    if (io) {
      io->connect();
      Serial.println("Reconnected to Ethernet for Adafruit IO.");
    }
    return true;
  }

  return false;
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

bool requestOutdoorData() {
  LoRa.beginPacket();
  LoRa.print("REQ");
  Serial.println("REQ");
  LoRa.endPacket();

  unsigned long startTime = millis();
  bool dataReceived = false;

  while (millis() - startTime < 2000) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String incoming = "";
      while (LoRa.available()) {
        incoming += (char)LoRa.read();
      }
      Serial.print("Received: ");
      Serial.println(incoming);

      if (parseCSV(incoming)) {
        outdoorDewPoint = calculateDewPoint(outdoorTemp, outdoorHum);
        dataReceived = true;
        lastOutdoorDataTime = millis();
        break;
      }
    }
  }
  
  if (!dataReceived) {
    Serial.println("No outdoor data received. Using previous values.");
  }

  return dataReceived;
}

bool parseCSV(String data) {
  int tIndex = data.indexOf("T=");
  int hIndex = data.indexOf("H=");
  
  if (tIndex != -1 && hIndex != -1) {
    String tempStr = data.substring(tIndex + 2, data.indexOf(',', tIndex));
    String humStr = data.substring(hIndex + 2);
    
    outdoorTemp = tempStr.toFloat();
    outdoorHum = humStr.toFloat();
    return true; // Successfully parsed
  }
  
  return false; // Parsing failed
}


void updateLCD() {
  lcd.setCursor(0, 0);
  lcd.print("In  T");
  if (indoorTemp != 0.0) {
    printFormattedTemperature(indoorTemp);
  } else {
    lcd.print(" -----");
  }
  lcd.print((char)223);
  lcd.print("C");
  lcd.setCursor(14, 0);
  lcd.print("H");
  if (indoorHum != 0.0) {
    printFormattedHumidity((int)indoorHum);
  } else {
    lcd.print(" --");
  }
  lcd.print("%");

  lcd.setCursor(0, 1);
  lcd.print("In  D");
  if (indoorDewPoint != 0.0) {
    printFormattedTemperature(indoorDewPoint);
  } else {
    lcd.print(" -----");
  }
  lcd.print((char)223);
  lcd.print("C");

  lcd.setCursor(0, 2);
  lcd.print("Out T");
  if (outdoorTemp != 0.0) {
    printFormattedTemperature(outdoorTemp);
  } else {
    lcd.print(" -----");
  }
  lcd.print((char)223);
  lcd.print("C");
  lcd.setCursor(14, 2);
  lcd.print("H");
  if (outdoorHum != 0.0) {
    printFormattedHumidity((int)outdoorHum);
  } else {
    lcd.print(" --");
  }
  lcd.print("%");

  lcd.setCursor(0, 3);
  lcd.print("Out D");
  if (outdoorDewPoint != 0.0) {
    printFormattedTemperature(outdoorDewPoint);
  } else {
    lcd.print(" -----");
  }
  lcd.print((char)223);
  lcd.print("C");
}


void updateLED() {
  float difference = indoorDewPoint - outdoorDewPoint;
  int redValue, greenValue;

  //if outdoor values == 0.0, show red, as well as if diff < 0
  if (outdoorHum == 0.0 || difference <= 0) {
    redValue = RED_MAX_BRIGHTNESS;
    greenValue = 0;
  } else if (difference > 5) {
    redValue = 0;
    greenValue = GREEN_MAX_BRIGHTNESS;
  } else {
    //float ratio = difference / 5.0;
    //redValue = (int)((1.0 - ratio) * RED_MAX_BRIGHTNESS);
    //greenValue = (int)(ratio * GREEN_MAX_BRIGHTNESS);
    //No mixed color - instead LED off for borderline conditions
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