# Dew Point Thermometer with LoRa and Internet Connectivity

![Dew Point Thermometer](https://github.com/yourusername/dew-point-thermometer/blob/main/images/overview.jpg)

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

Measuring temperature, humidity, and dew point is essential for understanding and maintaining comfortable and healthy indoor environments. A dew point thermometer helps predict when condensation might form, preventing mold growth and ensuring optimal climate control.

This project consists of **indoor** and **outdoor** stations:

- **Outdoor Station:** Measures temperature and humidity using an SHT85 sensor and transmits data via LoRa.
- **Indoor Station:** Receives outdoor data, measures its own temperature and humidity, calculates the dew point, displays the information on an LCD, and uses LED indicators to guide ventilation decisions. Optionally, it can upload data to Adafruit IO over Wi-Fi or Ethernet.

By implementing CRC checks, a unique sync word for LoRa communication, and verifying network connectivity before performing I/O operations, the system remains responsive and reliable even in noisy RF environments or when the Ethernet cable is disconnected.

## Features

- **Dual Measurements:**
  - **Indoor Station:** Measures indoor temperature, humidity, and dew point.
  - **Outdoor Station:** Measures outdoor temperature and humidity.
  
- **Dew Point Calculation and Humidity Control:**
  - Calculates dew point from indoor and outdoor readings.
  - Compares indoor and outdoor dew points to determine optimal ventilation actions.
  
- **LED Indicators for Humidity Management:**
  - **Green LED:** Indicates that airing out the house will help reduce indoor humidity.
  - **Red LED:** Indicates that airing out the house will not significantly affect indoor humidity.
  - **Mixed Color (if using bi-color LEDs):** Suggests marginal benefits from ventilation.
  
- **Robust LoRa Communication:**
  - Uses a unique sync word (`0x12`) to isolate communication from other LoRa networks.
  - Enables CRC checks to ensure packet integrity in noisy environments.
  
- **Non-Blocking and Responsive Operation:**
  - Utilizes timed intervals for sensor readings, LCD updates, and data uploads.
  - Prevents the system from freezing or stalling during network interruptions.
  
- **Fault Tolerance:**
  - Attempts to reinitialize sensors and LoRa modules after multiple failures.
  
- **Optional Cloud Connectivity:**
  - Uploads data to Adafruit IO via Wi-Fi or Ethernet for remote monitoring.

## Design Principles

1. **Reliability in Noisy Environments:**
   - **LoRa Configuration:** Implements a unique sync word and CRC to enhance communication integrity.
   - **Connection Checks:** Verifies network connectivity before performing Adafruit IO operations to avoid blocking.
   
2. **Non-Blocking Operation:**
   - Employs timed intervals for various operations to maintain system responsiveness.
   
3. **Robust Error Handling:**
   - Reinitializes sensors and LoRa modules after consecutive failures to ensure long-term stability.
   
4. **Modularity and Maintainability:**
   - Separates functionalities into distinct indoor and outdoor units with clear code organization.

## Hardware Components

### Outdoor Station

- **ESP32 (or similar MCU):** Provides I2C, SPI, and LoRa interfaces.
- **SHT85 Temperature/Humidity Sensor:** High-precision sensor for accurate measurements.
- **LoRa Module (SX1276/SX1278):** Facilitates long-range wireless communication.
- **Power Supply:** Stable 3.3V regulated supply.
- **Optional:** Weatherproof enclosure for outdoor deployment.

### Indoor Station

- **ESP32 (with Wi-Fi and optional W5500 Ethernet module):** Manages network connectivity and data processing.
- **SHT85 Temperature/Humidity Sensor:** Measures indoor environmental conditions.
- **LoRa Module (same frequency and sync word as outdoor):** Receives data from the outdoor station.
- **20x4 I2C LCD Display:** Displays temperature, humidity, dew point, and status messages.
- **Bi-Color LED (or separate LEDs):** Indicates whether ventilation is recommended.
- **Ethernet Module (W5500) (Optional):** Provides wired internet connectivity.
- **Power Supply:** Stable 3.3V regulated supply or USB power.
- **Optional:** Enclosure for indoor placement.

## Wiring

### Outdoor Station

**SHT85 to ESP32:**

| SHT85 Pin | ESP32 GPIO Pin | Notes            |
|-----------|----------------|------------------|
| SCL       | GPIO22         | I2C Clock        |
| SDA       | GPIO21         | I2C Data         |
| VCC       | 3.3V           | Power            |
| GND       | GND            | Ground           |

**LoRa Module to ESP32:**

| LoRa Pin | ESP32 GPIO Pin | Notes                      |
|----------|----------------|----------------------------|
| SCK      | GPIO18         | SPI Clock (shared with W5500) |
| MISO     | GPIO19         | SPI MISO (shared)          |
| MOSI     | GPIO23         | SPI MOSI (shared)          |
| NSS (CS) | GPIO5          | Chip Select for LoRa       |
| RST      | GPIO27         | Reset for LoRa module      |
| DIO0     | GPIO33         | Interrupt pin for LoRa     |
| VCC      | 3.3V           | Power                      |
| GND      | GND            | Ground                     |

### Indoor Station

**SHT85 to ESP32:**

| SHT85 Pin | ESP32 GPIO Pin | Notes            |
|-----------|----------------|------------------|
| SCL       | GPIO22         | I2C Clock        |
| SDA       | GPIO21         | I2C Data         |
| VCC       | 3.3V           | Power            |
| GND       | GND            | Ground           |

**LoRa Module to ESP32:**

| LoRa Pin | ESP32 GPIO Pin | Notes                      |
|----------|----------------|----------------------------|
| SCK      | GPIO18         | SPI Clock (shared with W5500) |
| MISO     | GPIO19         | SPI MISO (shared)          |
| MOSI     | GPIO23         | SPI MOSI (shared)          |
| NSS (CS) | GPIO5          | Chip Select for LoRa       |
| RST      | GPIO27         | Reset for LoRa module      |
| DIO0     | GPIO33         | Interrupt pin for LoRa     |
| VCC      | 3.3V           | Power                      |
| GND      | GND            | Ground                     |

**LCD (I2C) to ESP32:**

| LCD Pin | ESP32 GPIO Pin | Notes        |
|---------|----------------|--------------|
| SDA     | GPIO21         | I2C Data     |
| SCL     | GPIO22         | I2C Clock    |
| VCC     | 5V/3.3V        | Power        |
| GND     | GND            | Ground       |

**Ethernet Module (W5500) to ESP32 (Optional):**

| W5500 Pin | ESP32 GPIO Pin | Notes            |
|-----------|----------------|------------------|
| SCK       | GPIO18         | SPI Clock        |
| MISO      | GPIO19         | SPI MISO         |
| MOSI      | GPIO23         | SPI MOSI         |
| CS        | GPIO4          | Chip Select      |
| VCC       | 3.3V           | Power            |
| GND       | GND            | Ground           |

**LEDs to ESP32:**

| LED Pin         | ESP32 GPIO Pin | Notes                     |
|-----------------|----------------|---------------------------|
| RED_LED_PIN     | GPIO25         | Red LED (Indicator)       |
| GREEN_LED_PIN   | GPIO26         | Green LED (Indicator)     |

## Frequency, Sync Word, and CRC

- **Frequency (Europe Example):**  
  ```cpp
  const long frequency = 868100000; // 868.1 MHz
  ```
  - **Note:** Ensure compliance with local regulations when selecting frequency. 868.1 MHz is commonly used in Europe for LoRaWAN.

- **LoRa Sync Word:**  
  ```cpp
  LoRa.setSyncWord(0x12); // Unique sync word to isolate network traffic
  ```
  - **Explanation:** The sync word helps LoRa devices identify and separate their network traffic from others. Using a unique sync word (`0x12` in this case) reduces interference from other LoRa networks operating on the same frequency.

- **Enable CRC:**  
  ```cpp
  LoRa.enableCrc(); // Enable CRC for packet integrity
  ```
  - **Explanation:** CRC (Cyclic Redundancy Check) ensures that received packets are not corrupted. Enabling CRC helps maintain data integrity, especially in noisy environments. It is not enabled by default to minimize packet size and processing overhead, but it's recommended for reliable communication.

## Software Setup

### 1. Clone the Repository

```bash
git clone https://github.com/yourusername/dew-point-thermometer.git
cd dew-point-thermometer
```

### 2. Install Required Libraries

Ensure you have the following libraries installed via the Arduino Library Manager or manually from their GitHub repositories:

- [arduino-LoRa](https://github.com/sandeepmistry/arduino-LoRa)
- [Ethernet_Generic](https://github.com/khoih-prog/Ethernet_Generic)
- [LiquidCrystal_I2C](https://github.com/johnrickman/LiquidCrystal_I2C)
- [SHTSensor](https://github.com/Sensirion/arduino-sht)
- [AdafruitIO_WiFi](https://github.com/adafruit/Adafruit_IO_Arduino)
- [AdafruitIO_Ethernet](https://github.com/adafruit/Adafruit_IO_Arduino)

### 3. Configure Credentials

#### Indoor Station

- Open the indoor station code (`IndoorStation.ino`) and set your Wi-Fi credentials:

  ```cpp
  const char* ssid = "YourWiFiSSID";
  const char* password = "YourWiFiPassword";
  ```

- **Optional:** Configure static IP if you prefer not to use DHCP.

- Set your Adafruit IO credentials:

  ```cpp
  #define IO_USERNAME "your_adafruit_io_username"
  #define IO_KEY "your_adafruit_io_key"
  ```

### 4. Upload Code

- **Outdoor Station:**
  - Open the outdoor station code (`OutdoorStation.ino`) in the Arduino IDE.
  - Verify and upload the code to your outdoor ESP32.

- **Indoor Station:**
  - Open the indoor station code (`IndoorStation.ino`) in the Arduino IDE.
  - Verify and upload the code to your indoor ESP32.

- **Ensure both stations use the same LoRa frequency and sync word:**
  - Frequency: `868.1 MHz`
  - Sync Word: `0x12`

## Usage

1. **Power Up:**
   - Ensure both indoor and outdoor units are powered and connected appropriately.

2. **Data Transmission:**
   - The **Outdoor Station** waits for `REQ` packets from the indoor unit and responds with temperature and humidity data.
   - The **Indoor Station** periodically sends `REQ` packets to request outdoor data.

3. **Dew Point Calculation and Display:**
   - The **Indoor Station** measures its own temperature and humidity.
   - It calculates the dew point for both indoor and outdoor environments.
   - Based on the dew point difference, it determines whether airing out the house will help reduce indoor humidity.

4. **LED Indicators:**
   - **Green LED:** Aired-out conditions will help reduce indoor humidity.
   - **Red LED:** Aired-out conditions will not significantly affect indoor humidity.
   - **Mixed Color (if using a bi-color LED):** Indicates marginal benefits from airing out.

5. **Cloud Upload (Optional):**
   - If connected to Wi-Fi or Ethernet, the **Indoor Station** uploads data to Adafruit IO every 5 minutes for remote monitoring.

## Maintenance and Troubleshooting

- **Sensor Data Issues:**
  - **Problem:** No sensor readings or incorrect data.
  - **Solution:** 
    - Check wiring connections between the SHT85 sensor and ESP32.
    - Ensure the sensor is receiving adequate power.
    - Monitor serial output for initialization and read errors.

- **LoRa Communication Problems:**
  - **Problem:** Data not transmitting or receiving.
  - **Solution:**
    - Verify that both indoor and outdoor LoRa modules are using the same frequency and sync word.
    - Check antenna connections.
    - Ensure minimal physical obstructions between units.
    - Monitor serial output for LoRa initialization and communication errors.

- **LCD Display Issues:**
  - **Problem:** No display or incorrect information.
  - **Solution:**
    - Verify I2C connections between the LCD and ESP32.
    - Check the I2C address (`0x27` by default) matches your LCD module.
    - Ensure the LCD is receiving adequate power.

- **Network Connectivity Issues (Indoor Station):**
  - **Problem:** Unable to connect to Wi-Fi or Ethernet.
  - **Solution:**
    - Check Wi-Fi credentials and network availability.
    - If using Ethernet, verify the Ethernet cable is connected and the W5500 module is properly wired.
    - Monitor serial output for network initialization and connection status.

- **LED Indicators Not Working:**
  - **Problem:** LEDs not lighting up as expected.
  - **Solution:**
    - Check wiring connections for the LEDs.
    - Verify GPIO pins (`GPIO25` for Red, `GPIO26` for Green) are correctly set in the code.
    - Ensure LEDs have appropriate current-limiting resistors.

## Power and Housing

- **Outdoor Unit:**
  - Place in a weatherproof enclosure to protect electronics from the elements.
  - Ensure proper ventilation for accurate humidity readings.
  - Position the SHT85 sensor away from direct sunlight and heating sources to avoid skewed measurements.

- **Indoor Unit:**
  - Position the unit in a central location for optimal sensing and display visibility.
  - Ensure the LCD is easily readable.
  - Mount LEDs in a visible area to clearly indicate ventilation status.

## Future Enhancements

- **Security Enhancements:**
  - Implement encryption or authentication for LoRa communication to protect data integrity and privacy.

- **Power Management:**
  - Incorporate sleep modes in the outdoor unit to conserve power if running on batteries.

- **Data Logging:**
  - Add local storage (e.g., SD card) to log data for offline analysis.

- **Advanced Ventilation Controls:**
  - Integrate with smart home systems to automate ventilation based on dew point readings.

- **Mobile App Integration:**
  - Develop a mobile application to receive notifications or visualize data in real-time.

## License

This project is released under the [MIT License](LICENSE).

---

## Acknowledgments

- **Adafruit:** For providing the Adafruit IO platform and libraries.
- **Sensirion:** For the SHT85 sensor.
- **Sandeep Mistry:** For the Arduino-LoRa library.
- **Arduino Community:** For extensive resources and support.

---

Feel free to customize the README further based on your specific setup, additional features, or personal preferences. Adding images, diagrams, or links to relevant resources can also enhance the documentation and make it more user-friendly.
