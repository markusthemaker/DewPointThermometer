# Dew Point Thermometer with LoRa and Internet Connectivity

![Dew Point Thermometer](./images/overview.jpg)

## Table of Contents
- [Introduction and Motivation](#introduction-and-motivation)
- [Features](#features)
- [Design Principles](#design-principles)
- [Hardware Components](#hardware-components)
- [Wiring](#wiring)
- [Frequency, Sync Word, and CRC](#frequency-sync-word-and-crc)
- [Software Setup](#software-setup)
- [Usage](#usage)
- [Maintenance and Troubleshooting](#maintenance-and-troubleshooting)
- [Power and Housing](#power-and-housing)
- [Future Enhancements](#future-enhancements)
- [License](#license)

## Introduction and Motivation

This project builds a robust dew point thermometer system with **indoor** and **outdoor** stations. The **outdoor station** measures temperature/humidity with an SHT85 sensor and sends data over LoRa. The **indoor station** receives this data, measures its own indoor conditions, calculates dew points, and determines if airing the house out would help reduce indoor humidity. An LED indicator provides a quick visual guide for ventilation decisions.

By using a unique LoRa sync word, enabling CRC, verifying network connectivity before I/O operations, and employing non-blocking timed intervals, the system remains responsive and reliable even in noisy RF conditions or when Ethernet is disconnected.

## Features

- **Indoor/Outdoor Measurements:**  
  Indoor station measures its own temp/hum and receives outdoor data for comparative dew point analysis.

- **Dew Point Calculation & Humidity Control:**  
  Compares indoor and outdoor dew points to determine if opening windows helps lower indoor humidity.

- **LED Indicators for Ventilation:**
  - **Green LED:** Outdoor conditions favor reducing indoor humidity by airing out.
  - **Red LED:** No benefit from airing out.
  - **Off:** Borderline conditions with marginal benefit.

- **Robust LoRa Communication:**
  - Unique sync word and CRC ensure data integrity in noisy environments.
  - Uses 868.1 MHz (within EU ISM band).

- **Non-Blocking, Responsive Operation:**
  - Timed intervals for sensor reads, LCD updates, Adafruit IO uploads.
  - Avoids blocking during network interruptions.

- **Failure Recovery:**
  - Reinitializes SHT85 and LoRa after consecutive failures.
  - Resets outdoor values if no data arrives for 1 minute.

- **Optional Cloud Connectivity (Adafruit IO):**
  - Uploads data every 5 minutes if connected to Wi-Fi or Ethernet.
  - Skips uploads gracefully if disconnected.

## Design Principles

1. **Reliability in Noise:**  
   Unique sync word and CRC filters interference; checks network before I/O ops.

2. **Non-Blocking Operation:**  
   Timed intervals ensure system never stalls.

3. **Error Handling & Recovery:**  
   Reinitializes sensor/LoRa modules on repeated failures.

4. **Modularity & Maintainability:**  
   Separate indoor/outdoor logic; clear code organization.

## Hardware Components

### Outdoor Station
- ESP32 (or similar MCU)
- SHT85 Temp/Hum Sensor
- LoRa Module (SX1276/SX1278)
- Stable 3.3V power supply

### Indoor Station
- ESP32 (Wi-Fi and/or W5500 Ethernet)
- SHT85 Temp/Hum Sensor (indoor)
- LoRa Module (matching frequency/sync word)
- 20x4 I2C LCD Display
- Bi-Color LED or separate Red/Green LEDs
- Stable 3.3V power supply

## Wiring

**SHT85 to ESP32 (Both Stations):**
| SHT85 Pin | ESP32 Pin | Notes        |
|-----------|-----------|--------------|
| SCL       | GPIO22    | I2C Clock    |
| SDA       | GPIO21    | I2C Data     |
| VCC       | 3.3V      | Power        |
| GND       | GND       | Ground       |

**LoRa Module to ESP32:**
| LoRa Pin | ESP32 Pin | Notes                      |
|----------|-----------|----------------------------|
| SCK      | GPIO18    | SPI Clock (shared)         |
| MISO     | GPIO19    | SPI MISO (shared)          |
| MOSI     | GPIO23    | SPI MOSI (shared)          |
| NSS (CS) | GPIO5     | LoRa Chip Select           |
| RST      | GPIO27    | LoRa Reset                 |
| DIO0     | -1 or 33  | Not used (indoor), 33 if used outdoor |
| VCC      | 3.3V      | Power                      |
| GND      | GND       | Ground                     |

**LCD (I2C) to ESP32 (Indoor):**
| LCD Pin | ESP32 Pin | Notes    |
|---------|-----------|----------|
| SDA     | GPIO21    | I2C Data |
| SCL     | GPIO22    | I2C Clock|
| VCC     | 5V/3.3V   | Power    |
| GND     | GND       | Ground   |

**Ethernet (W5500) to ESP32 (Optional, Indoor):**
| W5500 Pin | ESP32 Pin | Notes           |
|-----------|-----------|-----------------|
| SCK       | GPIO18    | SPI Clock       |
| MISO      | GPIO19    | SPI MISO        |
| MOSI      | GPIO23    | SPI MOSI        |
| CS        | GPIO4     | W5500 Chip Select|
| VCC       | 3.3V      | Power           |
| GND       | GND       | Ground          |

**LEDs:**
| LED Pin        | ESP32 Pin | Notes                        |
|----------------|-----------|------------------------------|
| RED_LED_PIN    | GPIO25    | Red LED (Indicator)          |
| GREEN_LED_PIN  | GPIO26    | Green LED (Indicator)        |

Use appropriate resistors for LEDs.

## Frequency, Sync Word, and CRC

- **Frequency:**  
  ```cpp
  const long frequency = 868100000; // 868.1 MHz (EU ISM band)
  ```

- **LoRa Sync Word:**  
  ```cpp
  LoRa.setSyncWord(0x13); // Unique sync word to isolate your network traffic
  ```
  
- **Enable CRC:**  
  ```cpp
  LoRa.enableCrc(); // Ensures packet integrity
  ```

## Software Setup

1. **Clone Repository:**
   ```bash
   git clone https://github.com/yourusername/dew-point-thermometer.git
   cd dew-point-thermometer
   ```

2. **Install Required Libraries:**
   - [arduino-LoRa](https://github.com/sandeepmistry/arduino-LoRa)
   - [Ethernet_Generic](https://github.com/khoih-prog/Ethernet_Generic)
   - [LiquidCrystal_I2C](https://github.com/johnrickman/LiquidCrystal_I2C)
   - [SHTSensor](https://github.com/Sensirion/arduino-sht)
   - [AdafruitIO_WiFi](https://github.com/adafruit/Adafruit_IO_Arduino)
   - [AdafruitIO_Ethernet](https://github.com/adafruit/Adafruit_IO_Arduino)

3. **Configure Credentials:**
   - In the indoor code, set your Wi-Fi credentials:
     ```cpp
     const char* ssid = "YourWiFiSSID";
     const char* password = "YourWiFiPassword";
     ```
   - Set Adafruit IO credentials:
     ```cpp
     #define IO_USERNAME "your_adafruit_io_username"
     #define IO_KEY "your_adafruit_io_key"
     ```

4. **Upload Code:**
   - Upload `OutdoorStation.ino` to the outdoor ESP32.
   - Upload `IndoorStation.ino` to the indoor ESP32.
   - Ensure both use the same frequency and sync word.

## Usage

1. **Power Up Both Units:**
   - Outdoor station waits for "REQ" and responds with T/H data.
   - Indoor station requests outdoor data periodically, measures indoor conditions, and calculates dew points.

2. **LED Ventilation Guidance:**
   - **Green LED:** Airing out will likely reduce indoor humidity.
   - **Red LED:** No benefit from airing out.
   - **Off:** Borderline conditions, limited benefit.

3. **Cloud Upload (Optional):**
   - If Wi-Fi or Ethernet connected, indoor unit uploads data to Adafruit IO every 5 minutes.

4. **Automatic Recovery:**
   - Outdoor data reset after 1 minute of inactivity (no updates).
   - Sensor and LoRa reinitialized after repeated failures.

## Maintenance and Troubleshooting

- **No Sensor Data:**
  - Check SHT85 wiring and power.
  - Code attempts reinitialization after repeated failures.

- **LoRa Communication Issues:**
  - Ensure matching frequency, sync word, and CRC on both stations.
  - Check antennas and signal conditions.

- **LCD or LED Problems:**
  - Verify I2C address and wiring for the LCD.
  - Ensure LED pins and brightness values are correct.

- **Network Failures:**
  - For Wi-Fi: Check SSID, password, and signal strength.
  - For Ethernet: Check cable, DHCP success. Consider static IP if needed.
  - Code retries connections periodically without blocking.

## Power and Housing

- **Outdoor Unit:**
  - Use a weatherproof enclosure.
  - Ensure airflow for accurate humidity readings.

- **Indoor Unit:**
  - Place centrally, ensure LCD and LEDs are visible for quick humidity management decisions.

## Future Enhancements

- **Security:**
  - Add encryption or authentication if privacy is a concern.

- **Power Saving:**
  - Implement sleep modes for battery-powered outdoor units.

- **Data Logging:**
  - Add local storage (SD card) for offline analysis.

- **Integration:**
  - Interface with smart home systems to automate ventilation or alerts.

## License

This project is released under the [MIT License](LICENSE).

