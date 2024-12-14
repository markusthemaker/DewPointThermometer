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
#include <LoRa.h>      //https://github.com/sandeepmistry/arduino-LoRa
#include "SHTSensor.h" //https://github.com/Sensirion/arduino-sht

// Define pins for LoRa
#define LORA_SCK 18    // SPI Clock (shared with other SPI devices)
#define LORA_MISO 19   // SPI MISO (shared)
#define LORA_MOSI 23   // SPI MOSI (shared)
#define LORA_CS 5      // Chip Select for LoRa
#define LORA_RST 27    // Reset for LoRa module
#define LORA_IRQ 33    // Interrupt pin for LoRa (DIO0)

// Initialize SHT85 sensor
SHTSensor sht(SHTSensor::SHT3X);

// LoRa settings
const long frequency = 868100000; // 868.1 MHz

// Failure counters
const int maxSensorFailures = 5;
const int maxLoRaFailures = 3;
int sensorFailureCount = 0;
int loraFailureCount = 0;

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);

  // Initialize I2C
  Wire.begin();

  // Initialize SHT85 sensor
  if (sht.init()) {
    Serial.println("SHT85 sensor initialized.");
    sensorFailureCount = 0;
  } else {
    Serial.println("Failed to initialize SHT85 sensor!");
    sensorFailureCount++;
  }

  // Initialize LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);

  if (!LoRa.begin(frequency)) {
    Serial.println("Starting LoRa failed!");
    loraFailureCount++;
  } else {
    Serial.println("LoRa initialized.");
    loraFailureCount = 0;
    // Set sync word once
    LoRa.setSyncWord(0x13); 
    // Enable CRC once
    LoRa.enableCrc();
  }
}

void loop() {
  // Check for incoming requests
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }

    incoming.trim(); // Remove any leading/trailing whitespace
    Serial.println(incoming); Serial.println("");

    if (incoming == "REQ") {
      // Read sensor data
      float temp = 0.0;
      float hum = 0.0;
      if (sht.readSample()) {
        temp = sht.getTemperature();
        hum = sht.getHumidity();
        sensorFailureCount = 0; // Reset sensor failure count on success
      } else {
        Serial.println("Failed to read from SHT85 sensor.");
        sensorFailureCount++;
        if (sensorFailureCount >= maxSensorFailures) {
          Serial.println("Reinitializing SHT85 sensor...");
          if (sht.init()) {
            Serial.println("SHT85 reinitialized successfully.");
            sensorFailureCount = 0;
          } else {
            Serial.println("SHT85 reinitialization failed.");
          }
        }
      }

      // Prepare data string
      String data = "T=" + String(temp, 1) + ",H=" + String(hum, 1);

      // Send data back
      if (loraFailureCount < maxLoRaFailures) {
        LoRa.beginPacket();
        LoRa.print(data);
        if (LoRa.endPacket() == 0) {
          Serial.println("Failed to send LoRa packet.");
          loraFailureCount++;
          if (loraFailureCount >= maxLoRaFailures) {
            Serial.println("Reinitializing LoRa...");
            LoRa.end();
            delay(100);
            if (LoRa.begin(frequency)) {
              Serial.println("LoRa reinitialized successfully.");
              loraFailureCount = 0;
              // Set sync word once
              LoRa.setSyncWord(0x13); 
              // Enable CRC once
              LoRa.enableCrc();
            } else {
              Serial.println("LoRa reinitialization failed.");
            }
          }
        } else {
          Serial.println("Data sent: " + data);
          loraFailureCount = 0; // Reset LoRa failure count on success
        }
      }
    }
  }

  // Small delay to prevent overloading the loop
  delay(10);
}
