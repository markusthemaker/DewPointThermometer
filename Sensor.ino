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

#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <esp_sleep.h>

// Define pins for LoRa
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_RST 27
#define LORA_IRQ 33

// Define analog pin for battery voltage measurement
#define VOLTAGE_PIN 36  // Choose an available ADC pin on your ESP32

// Voltage divider resistor values (Ohms)
// R1 connects between the battery positive and the ADC pin.
// R2 connects between the ADC pin and ground.
#define R1 33000  // 33 kΩ
#define R2 10000  // 10 kΩ
// The divider ratio is: R2 / (R1 + R2) ≈ 10k / 43k ≈ 0.2326
// The battery voltage is calculated as: 
//   batteryVoltage = (ADC_voltage) * ((R1 + R2) / R2)
// where ADC_voltage = (analogRead(VOLTAGE_PIN)/4095.0)*3.3

// LoRa settings
const long frequency = 868300000; // 868.3 MHz
const int currentSF = 10;         // SF10 for improved range/reliability
const long currentBW = 125000;    // 125 kHz bandwidth
const int currentCR = 6;          // Coding Rate 4/6
const int powerdbM = 14;          // Maximum allowed TX power (14 dBm)

// Sending interval in seconds (device sleeps between sends)
// IMPORTANT: Consider duty cycle restrictions.
const double sendInterval = 40;   // Sleep interval in seconds

// LBT (Listen Before Talk) threshold (dBm)
const int RSSI_THRESHOLD = -80;

void setup() {
  Serial.begin(115200);

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
  
  // Listen Before Talk (LBT)
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
  
  // Read battery voltage using the voltage divider
  int adcValue = analogRead(VOLTAGE_PIN);
  float adcVoltage = ((float)adcValue / 4095.0) * 3.3;  // Voltage at ADC pin
  float batteryVoltage = adcVoltage * ((float)(R1 + R2) / R2);
  
  // Prepare the data string to be sent in the format "V:xx.x"
  String data = "V:" + String(batteryVoltage, 1);
  
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
