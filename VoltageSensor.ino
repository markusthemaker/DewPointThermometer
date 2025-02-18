// Sensor.ino
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <esp_sleep.h>
#include "esp_adc_cal.h"  // ADC calibration header
#include "driver/adc.h"   // For adc1_config_width() and adc1_config_channel_atten()

// ----- LoRa Pin Definitions -----
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS   5
#define LORA_RST  27
#define LORA_IRQ  33

// ----- ADC/Voltage Measurement Definitions -----
// Use GPIO35 (ADC1_CHANNEL_7)
#define VOLTAGE_PIN 35

// Voltage divider resistor values (Ohms)
// Battery positive -> R1 (33kΩ) -> ADC junction -> R2 (10kΩ) -> Battery negative (common ground)
#define R1 33000  
#define R2 10000  

// Use the default Vref value (eFuse value will be used if available)
#define DEFAULT_VREF 1100  // in mV

// Global variable for ADC characteristics
esp_adc_cal_characteristics_t *adc_chars;

// ----- LoRa & Sleep Settings -----
const long frequency = 868300000;  // 868.3 MHz
const int currentSF = 10;          // Spreading Factor
const long currentBW = 125000;     // Bandwidth in Hz
const int currentCR = 6;           // Coding Rate (4/6)
const int powerdbM = 14;           // TX power in dBm

const double sendInterval = 120;   // Sleep interval in seconds
const int RSSI_THRESHOLD = -80;    // Listen-before-talk threshold in dBm

void setup() {
  Serial.begin(115200);

  // ----- ADC Setup -----
  // Configure ADC1 channel: 12-bit resolution and 11 dB attenuation (approx. 3.3V range)
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);
  
  // Allocate memory and characterize ADC; eFuse Vref will be used if available.
  adc_chars = (esp_adc_cal_characteristics_t *) calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);

  // ----- LoRa Setup -----
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

  printLoRaSettings();

  // ----- Listen Before Talk (LBT) -----
  Serial.println("Checking channel before transmission...");
  while (!isChannelClear(RSSI_THRESHOLD)) {
    delay(100);
  }
  Serial.println("Channel is clear. Proceeding to send data.");

  // ----- Voltage Measurement -----
  uint32_t adc_voltage = 0;
  // Get calibrated voltage in mV from ADC1_CHANNEL_7 (GPIO35)
  esp_adc_cal_get_voltage((adc_channel_t)ADC1_CHANNEL_7, adc_chars, &adc_voltage);
  float adcVoltage = adc_voltage / 1000.0;  // Convert mV to V

  // Calculate battery voltage using the divider ratio:
  // V_battery = V_ADC * ((R1 + R2) / R2)
  float batteryVoltage = adcVoltage * ((float)(R1 + R2) / R2);

  // Debug prints
  Serial.print("Raw ADC voltage (V): ");
  Serial.println(adcVoltage, 2);
  Serial.print("Calculated battery voltage (V): ");
  Serial.println(batteryVoltage, 2);

  // ----- LoRa Transmission -----
  String data = "V:" + String(batteryVoltage, 1);  // Format: "V:xx.x"
  LoRa.beginPacket();
  LoRa.print(data);
  LoRa.endPacket();
  Serial.println("Sent packet: " + data);

  // ----- Deep Sleep -----
  LoRa.end();
  esp_sleep_enable_timer_wakeup(sendInterval * 1000000); // Convert seconds to microseconds
  Serial.println("Going to sleep for " + String(sendInterval) + " seconds...");
  esp_deep_sleep_start();
}

void loop() {
  // Not used; device sleeps after setup()
}

void printLoRaSettings() {
  Serial.println("Current LoRa Settings:");
  Serial.print("\tFrequency: ");
  Serial.println(frequency);
  Serial.print("\tBandwidth: ");
  Serial.print(currentBW / 1000);
  Serial.println(" kHz");
  Serial.print("\tSpreading Factor: ");
  Serial.println(currentSF);
}

bool isChannelClear(int threshold) {
  LoRa.receive();
  unsigned long startTime = millis();
  int measuredRSSI = -200;
  while (millis() - startTime < 200) {
    int currentRSSI = LoRa.packetRssi();
    if (currentRSSI > measuredRSSI) {
      measuredRSSI = currentRSSI;
    }
    delay(10);
  }
  LoRa.idle();
  Serial.print("Measured RSSI: ");
  Serial.println(measuredRSSI);
  return (measuredRSSI < threshold);
}
