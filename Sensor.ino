// Sensor.ino

// ESP32 to SHT85 Sensor:
// ESP32 Pin    SHT85 Pin   Notes
// 3.3V         VCC         Power supply (3.3V)
// GND          GND         Ground connection
// GPIO 21      SDA         I2C Data line
// GPIO 22      SCL         I2C Clock line

// ESP32 to LoRa SX1276 Module:
// ESP32 Pin    LoRa Pin    Notes
// 3.3V         VCC         Power supply (3.3V)
// GND          GND         Ground connection
// GPIO 5       NSS (CS)    Chip Select (CS) for LoRa
// GPIO 18      SCK         SPI Clock (shared if needed)
// GPIO 19      MISO        SPI Master In, Slave Out (shared if needed)
// GPIO 23      MOSI        SPI Master Out, Slave In (shared if needed)
// GPIO 27      RESET       LoRa module reset
// GPIO 33      DIO0        Interrupt pin (LoRa DIO0)

// Sensor.ino
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

// Set to "IN" for Indoor Sensor, "OUT" for Outdoor Sensor
const String inOrOut = "OUT";

// LoRa settings
const long frequency = 868300000; // 868.3 MHz
const int currentSF = 10;         // SF10 for improved range/reliability
const long currentBW = 125000;    // Bandwidth remains at 125 kHz
const int currentCR = 6;          // Coding Rate set to 4/6
const int powerdbM = 14;          // Maximum allowed TX power (14 dBm)

// Sending interval in seconds (device sleeps between sends)
// IMPORTANT: The duty cycle restrictions apply (e.g. if send time = 400ms, send only every 40s).
const double sendInterval = 40;   // Sleep interval in seconds

// LBT (Listen Before Talk) threshold
const int RSSI_THRESHOLD = -80;   // dBm

SHTSensor sht(SHTSensor::SHT85);

void setup() {
  Serial.begin(115200);
  
  // Initialize I2C for sensor communication
  Wire.begin();
   
  // Initialize LoRa SPI interface and module
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin(frequency)) {
    Serial.println("Starting LoRa failed!");
    while (true);
  }
  LoRa.setSyncWord(0x13);
  LoRa.enableCrc();
  LoRa.setSpreadingFactor(currentSF);   
  LoRa.setSignalBandwidth(currentBW);  
  LoRa.setCodingRate4(currentCR);       
  LoRa.setTxPower(powerdbM); 
  
  // Print LoRa settings for verification
  printLoRaSettings();
  
  // Implement Listen Before Talk (LBT)
  Serial.println("Checking channel before transmission...");
  while (!isChannelClear(RSSI_THRESHOLD)) {
    Serial.print("\tChannel busy (RSSI threshold: ");
    Serial.print(RSSI_THRESHOLD);
    Serial.println(" dBm). Retrying in 100 ms...");
    delay(100);
  }
  Serial.print("\tChannel is clear (threshold: ");
  Serial.print(RSSI_THRESHOLD);
  Serial.println(" dBm). Proceeding to send data.");
  
  // Read sensor data from the SHT85 sensor
  if (sht.init()) {
    Serial.println("SHT sensor initialized.");
  } else {
    Serial.println("Failed to initialize SHT sensor!");
  }
  float temp = 100.0;
  float hum = 0.0;
  if (sht.readSample()) {
    temp = sht.getTemperature();
    hum = sht.getHumidity();
  } else {
    Serial.println("Failed to read from SHT sensor.");
  }

  // Prepare the data string to be sent
  String data = inOrOut + String(":T=") + String(temp, 1) + ",H=" + String(hum, 1);
  
  // Send the data packet once
  unsigned long startTime = millis();    
  LoRa.beginPacket();
  LoRa.print(data);
  LoRa.endPacket();
  unsigned long endTime = millis();
  unsigned long txTime = endTime - startTime;
  Serial.println("Sent packet: " + data);
  Serial.print("Transmission Time: ");
  Serial.print(txTime);
  Serial.println(" ms");
  
  // End LoRa session and go to deep sleep
  Serial.println("-------------------------------------------------");
  LoRa.end();
  esp_sleep_enable_timer_wakeup(sendInterval * 1000000); // Sleep for sendInterval seconds
  Serial.println("Going to sleep for " + String(sendInterval) + " seconds...");
  esp_deep_sleep_start();
}

void loop() {
  // This loop will not run because the device enters deep sleep in setup().
}

void printLoRaSettings() {
  Serial.println("Current LoRa Settings:");
  Serial.print("\tFrequency: ");
  Serial.println(frequency);
  Serial.print("\tBandwidth (BW): ");
  Serial.print(currentBW / 1000); // kHz
  Serial.println(" kHz");
  Serial.print("\tSpreading Factor (SF): ");
  Serial.println(currentSF);
}

bool isChannelClear(int threshold) {
  // Switch LoRa module to RX mode to measure RSSI
  LoRa.receive();
  unsigned long measureStart = millis();
  int measuredRSSI = -200; // Start with a very low value
  
  // Measure for 200 ms
  while (millis() - measureStart < 200) {
    int currentRSSI = LoRa.packetRssi();
    if (currentRSSI > measuredRSSI) {
      measuredRSSI = currentRSSI;
    }
    delay(10);
  }
  
  // Switch back to idle mode for transmission
  LoRa.idle();
  
  Serial.print("\tMeasured RSSI: ");
  Serial.println(measuredRSSI);
  
  return (measuredRSSI < threshold);
}
