// ESP32 to SHT85 Sensor:
// ESP32 Pin	SHT85 Pin	Notes
// 3.3V	VCC	Power supply (3.3V)
// GND	GND	Ground connection
// GPIO 21	SDA	I2C Data line
// GPIO 22	SCL	I2C Clock line

// ESP32 to LoRa SX1276 Module:
// ESP32 Pin	LoRa Pin	Notes
// 3.3V	VCC	Power supply (3.3V)
// GND	GND	Ground connection
// GPIO 5	NSS (CS)	Chip Select (CS) for LoRa
// GPIO 18	SCK	SPI Clock (shared if needed)
// GPIO 19	MISO	SPI Master In, Slave Out (shared if needed)
// GPIO 23	MOSI	SPI Master Out, Slave In (shared if needed)
// GPIO 27	RESET	LoRa module reset
// GPIO 33	DIO0	Interrupt pin (LoRa DIO0)

#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include "SHTSensor.h"
#include <esp_sleep.h>

// Define pins for LoRa
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_RST 27
#define LORA_IRQ 33

//Set to "IN" for Indoor Sensor
//Set to "OUT" for Outdoor Sensor
const String inOrOut = "OUT"; 

// LoRa settings
const long frequency = 868100000; // 868.1 MHz
int currentSF = 7;        // Default Spreading Factor
long currentBW = 125000;  // Default Bandwidth in Hz (125 kHz)
int currentCR = 5;        // Default Coding Rate denominator (e.g., 5 for 4/5)
// Sending the data package with SF 7, 125khz, CR5, takes ~50ms, sending it twice for redundancy ~100ms total
// At a legally allowed max 1% duty cycle allowance, send every 10s at highest interval
double sendInterval = 60; // Send interval 

SHTSensor sht(SHTSensor::SHT85);

void setup() {
  Serial.begin(115200);
  
  // Initialize I2C
  Wire.begin();
  
  // Initialize SHT sensor
  if (sht.init()) {
    Serial.println("SHT sensor initialized.");
  } else {
    Serial.println("Failed to initialize SHT sensor!");
  }
  
  // Initialize LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  
  if (!LoRa.begin(frequency)) {
    Serial.println("Starting LoRa failed!");
    while (true);
  }
  
  LoRa.setSyncWord(0x13);
  LoRa.enableCrc();
  
  // Set LoRa parameters and track them
  LoRa.setSpreadingFactor(currentSF);    // e.g., SF7
  LoRa.setSignalBandwidth(currentBW);    // e.g., 125000 for 125 kHz
  LoRa.setCodingRate4(currentCR);        // e.g., 5 for 4/5
  
  // Print current LoRa settings
  printLoRaSettings();
  
  // Read sensor data
  float temp = 0.0;
  float hum = 0.0;
  if (sht.readSample()) {
    temp = sht.getTemperature();
    hum = sht.getHumidity();
  } else {
    Serial.println("Failed to read from SHT sensor.");
  }
  
  // Prepare data
  String data = inOrOut + String(":T=") + String(temp, 1) + ",H=" + String(hum, 1);
  
  // Send data twice to improve robustness in noisy environment 
  const int repeatCount = 2; // Number of times to send the same packet
  for (int i = 0; i < repeatCount; i++) {
    unsigned long startTime = millis();    
    LoRa.beginPacket();
    LoRa.print(data);
    LoRa.endPacket();
    unsigned long endTime = millis();
    
    // Calculate transmission time
    unsigned long txTime = endTime - startTime;
    Serial.print("Transmission Time: ");
    Serial.print(txTime);
    Serial.println(" ms");
    
    Serial.println("Sent packet (" + String(i + 1) + "): " + data);
    delay(250); // Small delay between repeated packets
  }
  
  // Prepare for deep sleep
  LoRa.end();
  esp_sleep_enable_timer_wakeup(sendInterval * 1000000); // 30 seconds
  Serial.println("Going to sleep for 30 seconds...");
  esp_deep_sleep_start();
}

void loop() {
  // This will not run as the device enters deep sleep after setup()
}

// Function to print current LoRa settings
void printLoRaSettings() {
  Serial.println("Current LoRa Settings:");
  Serial.print("Spreading Factor (SF): ");
  Serial.println(currentSF);
  
  Serial.print("Bandwidth (BW): ");
  Serial.print(currentBW / 1000); // Convert to kHz
  Serial.println(" kHz");
  
  Serial.print("Coding Rate (CR): 4/");
  Serial.println(currentCR);
}
