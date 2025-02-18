#ifndef DATA_UPLOADER_H
#define DATA_UPLOADER_H

#include <Arduino.h>
#include <AdafruitIO_WiFi.h>
#include <AdafruitIO_Ethernet.h>
#include <AdafruitIO.h>
#include <ThingSpeak.h>
#include <WiFiClient.h>
#include <Ethernet.h>

// Structure to hold sensor readings.
struct SensorData {
  float indoorTemp;
  float indoorHum;
  float indoorDew;
  float outdoorTemp;
  float outdoorHum;
  float outdoorDew;
  bool indoorDataValid;
  bool outdoorDataValid;
  // New field for battery voltage (from third LoRa station)
  // If no reading is received, this value should remain 0.
  float batteryVoltage;
};

// Abstract base class for data upload.
class DataUploader {
public:
  virtual void begin() = 0;
  /// Optional run() method (for example, to service a connection).
  virtual void run() {} 
  virtual void uploadData(const SensorData &data) = 0;
};

////////////////////////////////////////////////////////////////////////
// Adafruit IO Uploader (inline implementation)
////////////////////////////////////////////////////////////////////////
class AdafruitUploader : public DataUploader {
public:
  /// Constructor: Pass a pointer to an already–configured AdafruitIO instance.
  AdafruitUploader(AdafruitIO *io)
    : io(io), feedsInitialized(false),
      indoorTempFeed(nullptr), indoorHumFeed(nullptr), indoorDewFeed(nullptr),
      outdoorTempFeed(nullptr), outdoorHumFeed(nullptr), outdoorDewFeed(nullptr),
      batteryVoltageFeed(nullptr) { }  // New feed pointer initialized to nullptr
      
  virtual void begin() override {
    if (io && !feedsInitialized) {
      Serial.println("Initializing Adafruit IO feeds...");
      // Make sure the feed keys match exactly those in your Adafruit IO account.
      indoorTempFeed = io->feed("indoortemp");
      indoorHumFeed  = io->feed("indoorhum");
      indoorDewFeed  = io->feed("indoordp");
      outdoorTempFeed = io->feed("outdoortemp");
      outdoorHumFeed  = io->feed("outdoorhum");
      outdoorDewFeed  = io->feed("outdoordp");
      // Initialize the battery voltage feed. Adjust the feed key as needed.
      batteryVoltageFeed = io->feed("batteryvoltage");
      feedsInitialized = true;
      Serial.println("Adafruit IO feeds initialized.");
    }
  }
  
  virtual void run() override {
    if (io) {
      io->run();
    }
  }
  
  virtual void uploadData(const SensorData &data) override {
    if (!io) {
      Serial.println("Error: IO object is null in AdafruitUploader.");
      return;
    }
    
    // Upload outdoor data if valid.
    if (data.outdoorDataValid && outdoorTempFeed && outdoorHumFeed && outdoorDewFeed) {
      Serial.print("Uploading outdoor data: Temp=");
      Serial.print(data.outdoorTemp);
      Serial.print(" Hum=");
      Serial.print(data.outdoorHum);
      Serial.print(" Dew=");
      Serial.println(data.outdoorDew);
      outdoorTempFeed->save(data.outdoorTemp);
      outdoorHumFeed->save(data.outdoorHum);
      outdoorDewFeed->save(data.outdoorDew);
    } else {
      Serial.println("Outdoor data not valid or feeds not initialized.");
    }
    
    // Upload indoor data if valid.
    if (data.indoorDataValid && indoorTempFeed && indoorHumFeed && indoorDewFeed) {
      Serial.print("Uploading indoor data: Temp=");
      Serial.print(data.indoorTemp);
      Serial.print(" Hum=");
      Serial.print(data.indoorHum);
      Serial.print(" Dew=");
      Serial.println(data.indoorDew);
      indoorTempFeed->save(data.indoorTemp);
      indoorHumFeed->save(data.indoorHum);
      indoorDewFeed->save(data.indoorDew);
    } else {
      Serial.println("Indoor data not valid or feeds not initialized.");
    }
    
    // Upload battery voltage data.
    Serial.print("Uploading battery voltage: ");
    Serial.println(data.batteryVoltage);
    if (batteryVoltageFeed) {
      batteryVoltageFeed->save(data.batteryVoltage);
    } else {
      Serial.println("Battery voltage feed not initialized.");
    }
  }
  
private:
  AdafruitIO *io;
  bool feedsInitialized;
  AdafruitIO_Feed *indoorTempFeed;
  AdafruitIO_Feed *indoorHumFeed;
  AdafruitIO_Feed *indoorDewFeed;
  AdafruitIO_Feed *outdoorTempFeed;
  AdafruitIO_Feed *outdoorHumFeed;
  AdafruitIO_Feed *outdoorDewFeed;
  // New feed for battery voltage.
  AdafruitIO_Feed *batteryVoltageFeed;
};

////////////////////////////////////////////////////////////////////////
// ThingSpeak Uploader (updated inline implementation)
////////////////////////////////////////////////////////////////////////
class ThingSpeakUploader : public DataUploader {
public:
  /// Constructor: Pass a pointer to a Client (WiFiClient or EthernetClient),
  /// your channel ID, and your write API key.
  ThingSpeakUploader(Client *client, unsigned long channelID, const char* writeAPIKey)
    : client(client), channelID(channelID), writeAPIKey(writeAPIKey) { }
  
  virtual void begin() override {
    // Initialize the ThingSpeak library using the client.
    ThingSpeak.begin(*client);
  }
  
  virtual void uploadData(const SensorData &data) override {
    bool updateData = false;
    
    // If indoor data is valid, update fields 1-3 
    if (data.indoorDataValid) {
      ThingSpeak.setField(1, data.indoorTemp);
      ThingSpeak.setField(2, data.indoorHum);
      ThingSpeak.setField(3, data.indoorDew);
      updateData = true;
    }
    
    // If outdoor data is valid, update fields 4-6
    if (data.outdoorDataValid) {
      ThingSpeak.setField(4, data.outdoorTemp);
      ThingSpeak.setField(5, data.outdoorHum);
      ThingSpeak.setField(6, data.outdoorDew);
      updateData = true;
    }
    
    // If battery voltage is available (greater than 0), update field 7.
    if (data.batteryVoltage > 0) {
      ThingSpeak.setField(7, data.batteryVoltage);
      updateData = true;
    }
    
    if (updateData) {
      int responseCode = ThingSpeak.writeFields(channelID, writeAPIKey);
      if (responseCode == 200) {
        Serial.println("ThingSpeak update successful.");
      } else {
        Serial.print("ThingSpeak update error. HTTP error code: ");
        Serial.println(responseCode);
      }
    } else {
      Serial.println("No valid data to update on ThingSpeak.");
    }
  }
  
private:
  Client *client;
  unsigned long channelID;
  const char* writeAPIKey;
};


#endif
