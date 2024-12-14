# Dew Point Thermometer with LoRa and Internet Connectivity

![Dew Point Thermometer](./img/IMG_2939.jpeg)

## Table of Contents
- [Introduction and Motivation](#introduction-and-motivation)
- [Features](#features)
- [Design Principles](#design-principles)
- [Hardware Components](#hardware-components)
- [Wiring](#wiring)
- [Frequency, Sync Word, and CRC](#frequency-sync-word-and-crc)
- [Software Setup](#software-setup)
- [Housing](#housing)
- [Usage](#usage)
- [Maintenance and Troubleshooting](#maintenance-and-troubleshooting)
- [License](#license)

## Introduction and Motivation

In atmospheric science and indoor air quality management, the **dew point** is a critical metric. It represents the temperature at which the water vapor in the air becomes saturated and begins to condense into liquid water. A higher dew point indicates more moisture in the air, often leading to discomfort, potential mold growth, and structural damage. By understanding and controlling dew point, one can maintain healthier, more comfortable indoor conditions, prevent condensation-related issues, and optimize energy usage for heating and cooling. Monitoring dew point is thus invaluable in achieving both human comfort and long-term preservation of building materials.

This project implements a robust dew point thermometer system with **indoor** and **outdoor** stations. The **outdoor station** measures temperature/humidity with an SHT85 sensor and transmits data via LoRa. The **indoor station** receives this data, measures its own conditions, calculates both indoor and outdoor dew points, and determines if airing the house out would help control indoor humidity or not. An LED indicator provides a quick visual guide for whether or not to ventilate, and a LCD display shows the current measured and calculated values for indoor & outdoor. The system is connected to Adafruit Cloud for tracking and graphing data. 

By employing a unique LoRa sync word, CRC checks, ensuring network connectivity before I/O operations, and using timed intervals for tasks, the system remains responsive and reliable—even in noisy RF environments or when the Ethernet cable is disconnected. Additionally, a logic level converter (level shifter) is required for the LCD if it operates at 5V while the rest of the system runs at 3.3V.

## Features

- **Indoor/Outdoor Measurements:**  
  - Indoor station monitors its own temp/hum, receives outdoor data for comparative dew point analysis.

- **Scientific Dew Point Calculation Using the Magnus Approximation:**  
  The dew point can be approximated using the Magnus–Tetens formula, which provides a practical way to compute dew point (`D`) based on temperature (`T`) and relative humidity (`H`). Different constants are used depending on whether the temperature is above or below 0°C:

     ```cpp
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
     ```
 

- **Humidity Control:**  
  - Compares indoor and outdoor dew points to assess the benefit of airing out to reduce indoor humidity.

- **LED Indicators for Ventilation:**
  - **Green LED:** Outdoor conditions favor lowering indoor humidity by airing (delta dew point >5C).
  - **Red LED:** No benefit from airing.
  - **Off:** Borderline conditions, marginal benefit (delta 0-5C).

- **Robust LoRa Communication:**
  - Unique sync word, CRC for reliable data in noisy environments.
  - Operates at 868.1 MHz (EU ISM band).

- **Non-Blocking, Responsive Operation:**
  - Timed intervals for sensor reads, LCD updates, Adafruit IO uploads.
  - Avoids stalling during network issues.

- **Failure Recovery:**
  - Reinitializes SHT85 and LoRa after multiple failures.
  - Resets outdoor values if no data for 1 minute.

- **Optional Cloud Connectivity (Adafruit IO):**
  - Uploads data every 5 minutes if Wi-Fi or Ethernet is connected.
  - Skips uploads gracefully if disconnected.

## Design Principles

1. **Reliability in Noise:**  
   - Unique sync word and CRC filter interference.
   - Verifies network connectivity before I/O ops.

2. **Non-Blocking Operation:**  
   - Uses timed intervals to prevent system stalls.

3. **Error Handling & Recovery:**  
   - Reinitializes sensors and LoRa on failures.
   - Clears old outdoor data if no updates in 1 minute.

4. **Modularity & Maintainability:**  
   - Separate indoor/outdoor code.
   - Clear logic and timing-based structure.

## Hardware Components

### Outdoor Station
- ESP32 (or similar MCU)
- SHT85 Temp/Hum Sensor (3.3V)
- LoRa Module (SX1276/SX1278, 3.3V)
- Stable 3.3V power supply or use a DC-DC step down module

### Indoor Station
- ESP32 (Wi-Fi and/or W5500 Ethernet, all 3.3V)
- SHT85 Temp/Hum Sensor (3.3V)
- LoRa Module (3.3V)
- 20x4 I2C LCD Display (requires 5V)
- Bi-Color LED or separate Red/Green LEDs (3.3V via PWM)
- **Logic Level Converter for LCD I2C Lines**  
  (The LCD is powered at 5V, while ESP32 I2C lines are 3.3V. A level shifter ensures proper logic levels.)
- Stable 5V supply via DC-DC step down module. For 3.3V, sinmply use ESP 3.3V out. 

## Wiring

**Note:** The ESP32 and LoRa, SHT85, Ethernet modules operate at 3.3V logic. The LCD typically requires 5V and is not 3.3V tolerant on I2C lines. Use a bidirectional logic level converter for I2C SDA and SCL lines between ESP32 (3.3V) and LCD (5V).

| ![Dew Point Thermometer Prototype Wiring](./img/3.jpeg) | 
|:--:| 
| *Left: Wiring & Prototype Testing. Top Right: LCD Display Testing. Bottom Right: Assembled Components* |

**SHT85 to ESP32 (Both Stations, 3.3V):**
| SHT85 Pin | ESP32 Pin | Notes        |
|-----------|-----------|--------------|
| SCL       | GPIO22    | I2C Clock (3.3V) |
| SDA       | GPIO21    | I2C Data (3.3V)  |
| VCC       | 3.3V      | Power        |
| GND       | GND       | Ground       |

**LoRa Module to ESP32 (3.3V):**
| LoRa Pin | ESP32 Pin | Notes                      |
|----------|-----------|----------------------------|
| SCK      | GPIO18    | SPI Clock (shared)         |
| MISO     | GPIO19    | SPI MISO (shared)          |
| MOSI     | GPIO23    | SPI MOSI (shared)          |
| NSS (CS) | GPIO5     | LoRa Chip Select           |
| RST      | GPIO27    | LoRa Reset                 |
| DIO0     | -1 or 33  | Not used indoor or GPIO33 if used outdoor |
| VCC      | 3.3V      | Power                      |
| GND      | GND       | Ground                     |

**LCD (I2C) to ESP32 via Level Shifter:**
- The LCD requires 5V power and 5V logic on SDA/SCL.  
- ESP32 runs at 3.3V logic. Use a bidirectional logic level converter on SDA and SCL lines.

| LCD Pin | Level Shifter | ESP32 Pin | Notes                       |
|---------|---------------|-----------|-----------------------------|
| SDA (5V)| ↔ SDA (3.3V)  | GPIO21    | I2C Data through level shifter |
| SCL (5V)| ↔ SCL (3.3V)  | GPIO22    | I2C Clock through level shifter|
| VCC     | 5V            | -         | LCD Power (5V)              |
| GND     | GND           | -         | Ground shared               |

**Ethernet (W5500) to ESP32 (Optional, 3.3V):**
| W5500 Pin | ESP32 Pin | Notes           |
|-----------|-----------|-----------------|
| SCK       | GPIO18    | SPI Clock       |
| MISO      | GPIO19    | SPI MISO        |
| MOSI      | GPIO23    | SPI MOSI        |
| CS        | GPIO4     | W5500 CS        |
| VCC       | 3.3V      | Power           |
| GND       | GND       | Ground          |

**LEDs (3.3V):**
| LED Pin        | ESP32 Pin | Notes                         |
|----------------|-----------|--------------------------------|
| RED_LED_PIN    | GPIO25    | Red LED with resistor, 3.3V logic |
| GREEN_LED_PIN  | GPIO26    | Green LED with resistor, 3.3V logic |

## Frequency, Sync Word, and CRC

```cpp
const long frequency = 868100000; // 868.1 MHz for EU ISM band
LoRa.setSyncWord(0x13); // Unique sync word for your network
LoRa.enableCrc(); // Ensure packet integrity with CRC
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
   - In indoor code:
     ```cpp
     const char* ssid = "YourWiFiSSID";
     const char* password = "YourWiFiPassword";
     ```
     ```cpp
     #define IO_USERNAME "your_adafruit_io_username"
     #define IO_KEY "your_adafruit_io_key"
     ```

4. **Upload Code:**
   - Upload [OutdoorStation.ino](OutdoorStation.ino) to outdoor ESP32.
   - Upload [IndoorStation.ino](IndoorStation.ino) to indoor ESP32.
   - Ensure same frequency & sync word on both units.

## Housing

| ![Dew Point Thermometer Prototype Housing](./img/4.jpeg) | 
|:--:| 
| *Top Left: Custom Indoor Housing with LED, LCD, DC & Ethernet connector, air vents. Top Right: Small cut-out for sensor. Bottom Left: Mounting via spacers and (hot) glue. Bottom right: Outdoor FTA Housing. For both, indoor and outdoor housing, the DC connector is connected to a 0.2A fuse for safety* |

- **Outdoor Unit:**  
  Weatherproof enclosure, ensure airflow for accurate humidity readings.
  Good quality housing: TFA Dostmann Potective Cover for Transmitter. 

- **Indoor Unit:**
  - Indoor Housing was designed using CAD Onshape.
  - Link to my [Onshape design](https://cad.onshape.com/documents/c48dac3dd317ad2774113701/w/70a39c90a07f91a9a65c84c1/e/97dbad388adcff361e7d9df7) to my design.  
  - [3D STL file for printing](IndoorHousing.stl)


## Usage

1. **Power Up Both Units:**
   - Outdoor waits for `REQ` and sends T/H data.
   - Indoor requests outdoor data, measures indoor conditions, calculates dew points.

2. **LED Ventilation Guidance:**
   - Green: Airing out helps reduce indoor humidity.
   - Red: No benefit from airing out.
   - Off: Borderline conditions.

3. **Automatic Recovery:**
   - Outdoor data resets after 1 minute if no updates.
   - Sensor and LoRa reinitialized after repeated failures.
  
4. **Cloud Upload (Optional):**
   - If connected to wifi or Ethernet, indoor uploads data to Adafruit IO every 5 mins:

![Dew Point Thermometer Adafruit IO](./img/Screenshot.png) 

## Maintenance and Troubleshooting

- **Sensor Issues:**  
  Check SHT85 wiring. Code attempts reinitialization after multiple failures.

- **LoRa Communication Problems:**  
  Ensure same frequency, sync word, and CRC on both ends.  
  Check antennas and signal environment.

- **LCD or LED Problems:**  
  Verify I2C address and wiring for LCD.  
  Use a level shifter for I2C lines since LCD runs at 5V.  
  Check LED pins and resistor values.

- **Network Failures:**  
  Check Wi-Fi credentials or Ethernet cable and DHCP.  
  Code retries periodically without blocking main loop.



## License

This project is released under the [MIT License](LICENSE).
