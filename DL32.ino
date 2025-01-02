/*

  DL32 v3 by Mark Booth
  For use with Wemos S3 and DL32 S3 hardware rev 20240812
  Last updated 02/01/2025
  https://github.com/Mark-Roly/DL32/

  Board Profile: ESP32S3 Dev Module
  Upload settings:
    USB CDC on Boot: Enabled
    CPU frequency: 240Mhz (WiFi)
    USB DFU on boot: Disabled
    USB Firmware MSC on boot: Disabled
    Partition Scheme: Custom (partitions.csv provided on Github)
    Upload Mode: UART0/Hardware CDC
    Upload speed: 921600
    USB Mode: Hardware CDC and JTAG   
  
  DIP Switch settings:
    DS01 = Offline mode
    DS02 = FailSafe Strike mode
    DS03 = Silent mode
    DS04 = Garage mode

  SD card pins:
    CD DAT3 CS 34
    CMD DI DIN MOSI 36
    CLK SCLK 38
    DAT0 D0 MISO 35
    
*/

#define codeVersion 20250102

// Include Libraries
#include <Arduino.h>            // Arduino by Arduino https://github.com/arduino/ArduinoCore-avr/blob/master/cores/arduino
#include <WiFi.h>               // WiFi by Ivan Grokhotkov https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi
#include <WebServer.h>          // WebServer by Ivan Grokhotkov https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer
#include <SPI.h>                // SPI by Hristo Gochkov https://github.com/espressif/arduino-esp32/blob/master/libraries/SPI
#include <LittleFS.h>           // LittleFS by Espressif https://github.com/espressif/arduino-esp32/blob/master/libraries/LittleFS
#include <PubSubClient.h>       // PubSubClient by Nick O'leary https://github.com/knolleary/pubsubclient
#include <Adafruit_NeoPixel.h>  // Adafruit_NeoPixel by Adafruit https://github.com/adafruit/Adafruit_NeoPixel
#include <Wiegand.h>            // YetAnotherArduinoWiegandLibrary by paula-raca https://github.com/paulo-raca/YetAnotherArduinoWiegandLibrary
#include <ArduinoJson.h>        // ArduinoJSON by Benoit Blanchon https://arduinojson.org/?utm_source=meta&utm_medium=library.properties
#include <Ticker.h>             // Ticker by Bert Melis https://github.com/espressif/arduino-esp32/blob/master/libraries/Ticker
#include <uri/UriRegex.h>       // UriRegex by espressif https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer
#include <uptime_formatter.h>   // Uptime-Library by YiannisBourkelis https://github.com/YiannisBourkelis/Uptime-Library
#include <ElegantOTA.h>         // ElegantOTA by yushsharma82 https://github.com/ayushsharma82/ElegantOTA
#include <FS.h>                 // FS by Ivan Grokhotkov https://github.com/espressif/arduino-esp32/blob/master/libraries/FS
#include <FFat.h>               // FFAT by espressif https://github.com/espressif/arduino-esp32/blob/master/libraries/FFat
#include <SD.h>                 // SD by espressif https://github.com/espressif/arduino-esp32/blob/master/libraries/SD

// Hardware Rev 20240812 pins [Since codeVersion 20240819]
#define buzzer_pin 14
#define neopix_pin 47
#define lockRelay_pin 1
#define AUXButton_pin 6
#define exitButton_pin 21
#define bellButton_pin 17
#define magSensor_pin 15
#define attSensor_pin 9
#define wiegand_0_pin 16
#define wiegand_1_pin 18
#define DS01 33
#define DS02 37
#define DS03 5
#define DS04 10
#define GH01 2
#define GH02 4
#define GH03 12
#define GH04 13
#define GH05 8
#define GH06 11
#define SD_CS_PIN 34
#define SD_CLK_PIN 38
#define SD_MOSI_PIN 36
#define SD_MISO_PIN 35
#define SD_CD_PIN 7

// Define struct for storing configuration
struct Config {
  char wifi_enabled[8];
  char wifi_ssid[32];
  char wifi_password[32];
  char mqtt_enabled[8];
  char mqtt_server[32];
  char mqtt_port[8];
  char mqtt_topic[32];
  char mqtt_cmnd_topic[32];
  char mqtt_stat_topic[32];
  char mqtt_keys_topic[32];
  char mqtt_addr_topic[32];
  char mqtt_uptm_topic[32];
  char mqtt_client_name[32];
  char mqtt_auth[8];
  char mqtt_user[32];
  char mqtt_password[32];
};

#define exitButDur 5
#define httpDur 5
#define keypadDur 15 //tenths-of-seconds
#define keyDur 5
#define garageDur 500
#define addKeyDur 10
#define unrecognizedKeyDur 4
#define WDT_TIMEOUT 60

// Number of neopixels used
#define NUMPIXELS 1

#define FORMAT_FFAT false

unsigned long lastMsg = 0;
unsigned long disconCount = 0;
unsigned long lastMQTTConnectAttempt = 0;
unsigned long lastWifiConnectAttempt = 0;
unsigned long wifiReconnectInterval = 60000;
unsigned long mqttReconnectInterval = 60000;
int add_count = 0;
int seqTmr = 0;
int hwRev = 0;
String scannedKey = "";
String serialCmd;

unsigned long ota_progress_millis = 0;

boolean validKeyRead = false;
boolean forceOffline = false;
boolean invalidKeyRead = false;
boolean SD_present = false;
boolean SD_mounted = false;
boolean FFat_present = false;
boolean doorOpen = true;
boolean failSecure = true;
boolean add_mode = false;
boolean garage_mode = false;

String pageContent = "";
const char* config_filename = "/dl32.json";
const char* addressing_filename = "/addressing.json";
const char* keys_filename = "/keys.txt";

// buzzer settings
int freq = 2000;
int channel = 0;
int resolution = 8;

// integer for watchdog counter
volatile int watchdogCount = 0;

// Define onboard and offvoard neopixels
Adafruit_NeoPixel pixel = Adafruit_NeoPixel(NUMPIXELS, neopix_pin, NEO_GRB + NEO_KHZ800);

// instantiate objects for Configuration struct, wifi client, webserver, mqtt client, qiegand reader and WDT timer
Config config;
WiFiClient esp32Client;
WebServer webServer(80);
PubSubClient MQTTclient(esp32Client);
Wiegand wiegand;
Ticker secondTick;

// --- Watchdog Functions --- Watchdog Functions --- Watchdog Functions --- Watchdog Functions --- Watchdog Functions --- Watchdog Functions ---
void ISRwatchdog() {
  watchdogCount++;
  if (watchdogCount == WDT_TIMEOUT) {
    Serial.println("Watchdog invoked!");
    ESP.restart();
  }
}

// --- OTA Functions --- OTA Functions --- OTA Functions --- OTA Functions --- OTA Functions --- OTA Functions --- OTA Functions ---

void onOTAStart() {
  // Log when OTA has started
  Serial.println("OTA update started!");
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
}

// --- Uptime Functions --- Uptime Functions --- Uptime Functions --- Uptime Functions --- Uptime Functions --- Uptime Functions --- Uptime Functions ---

void publishUptime() {
  if (MQTTclient.connected()) {
    MQTTclient.publish(config.mqtt_uptm_topic, uptime_formatter::getUptime().c_str());  
  } 
}

void printUptime() {
  Serial.println("Uptime: " + uptime_formatter::getUptime());
}

// --- Wiegand Functions --- Wiegand Functions --- Wiegand Functions --- Wiegand Functions --- Wiegand Functions --- Wiegand Functions --- Wiegand Functions ---

// function that checks if a provided key is present in the authorized list
boolean keyAuthorized(String key) {
  boolean verboseScanOutput = false;
  File keysFile = FFat.open(keys_filename, "r");
  int charMatches = 0;
  char keyBuffer[16];
  Serial.print("Checking key: ");
  Serial.println(key);
  if (verboseScanOutput) {
    Serial.print("Input Key length: ");
    Serial.println(key.length());
  }
  while (keysFile.available()) {
    int keyDigits = (keysFile.readBytesUntil('\n', keyBuffer, sizeof(keyBuffer))-1);
    if (verboseScanOutput) {
      Serial.print("card digits = ");
      Serial.println(String(keyDigits));
      keyBuffer[keyDigits] = 0;
    }
    charMatches = 0;
    for (int loopCount = 0; loopCount < (keyDigits); loopCount++) {
      if (verboseScanOutput) {
        Serial.print("comparing ");
        Serial.print(key[loopCount]);
        Serial.print(" with ");
        Serial.println(keyBuffer[loopCount]);
      }
      if (key[loopCount] == keyBuffer[loopCount]) {
        charMatches++;
      }
    }
    if (verboseScanOutput) {
      Serial.print("charMatches: ");
      Serial.println(charMatches);
      Serial.print("keyDigits: ");
      Serial.println(keyDigits);
      Serial.print("Input Key length: ");
      Serial.println(key.length());
    }
    if ((charMatches == keyDigits)&&(keyDigits == key.length())) {
      if (verboseScanOutput) {
        Serial.print(keyBuffer);
        Serial.print(" - ");
        Serial.println("MATCH");
      }
      keysFile.close();
      return true;
    } else {
      if (verboseScanOutput) {
        Serial.println("NO MATCH");
      }
    }
  }
  keysFile.close();
  return false;
}

void writeKey(String key) {
  if (keyAuthorized(key)) {
    add_mode = false;
    Serial.print("Key ");
    Serial.print(key);
    Serial.println(" is already authorized");
    playUnauthorizedTone();
  } else if ((key.length() < 3)||(key.length() > 10)) {
    add_mode = false;
    Serial.print("Key ");
    Serial.print(key);
    Serial.println(" is an invalid length");
  } else {
    appendlnFile(FFat, keys_filename, key.c_str());
    add_mode = false;
    Serial.print("Added key ");
    Serial.print(key);
    Serial.println(" to authorized list");
    playSuccessTone();
  }
}

void removeKey(String key) {
  boolean verboseScanOutput = false;
  File keysFile = FFat.open(keys_filename, "r");
  int charMatches = 0;
  int removeMatches = 0;
  char keyBuffer[16];
  if (FFat.exists(keys_filename) == false) {
    Serial.println("Key file not present. Cancelling operation.");
    return;
  }
  if (FFat.exists("/keys_old")) {
    deleteFile(FFat, "/keys_old");
  }
  if (FFat.exists("/keys_temp")) {
    deleteFile(FFat, "/keys_temp");
  }
  Serial.print("Checking key for removal: ");
  Serial.println(key);
  while (keysFile.available()) {
    int keyDigits = (keysFile.readBytesUntil('\n', keyBuffer, sizeof(keyBuffer))-1);
    if (verboseScanOutput) {
      Serial.print("key digits = ");
      Serial.println(String(keyDigits));
    }
    keyBuffer[keyDigits] = 0;
    charMatches = 0;
    for (int loopCount = 0; loopCount < (keyDigits); loopCount++) {
      if (verboseScanOutput) {
        Serial.print("comparing ");
        Serial.print(key[loopCount]);
        Serial.print(" with ");
        Serial.println(keyBuffer[loopCount]);
      }
      if (key[loopCount] == keyBuffer[loopCount]) {
        charMatches++;
      }
    }
    if (verboseScanOutput) {
      Serial.print("charMatches: ");
      Serial.println(charMatches);
      Serial.print("keyDigits: ");
      Serial.println(keyDigits);
      Serial.print("Input Key length: ");
      Serial.println(key.length());
    }
    if ((charMatches == keyDigits)&&(keyDigits == key.length())) {
      if (verboseScanOutput) {
        Serial.print(keyBuffer);
        Serial.print(" - ");
        Serial.println("MATCH");
        Serial.print("----- EXCLUDING ");
        Serial.print(keyBuffer);
        Serial.println(" FROM NEW FILE");
      }
      removeMatches++;
      //exclude from copy
      
    } else {
      if (verboseScanOutput) {
        Serial.println("NO MATCH");
        Serial.print("----- COPYING ");
        Serial.print(keyBuffer);
        Serial.println(" TO NEW FILE");
      }
    appendlnFile(FFat, "/keys_temp", keyBuffer);
    }
  }
  keysFile.close();
  if (removeMatches > 0) {
    Serial.print("Removed key ");
    Serial.print(key);
    Serial.println(" from authorized list.");
    renameFile(FFat, keys_filename, "/keys_old");
    renameFile(FFat, "/keys_temp", keys_filename);
  } else {
    Serial.print("Key ");
    Serial.print(key);
    Serial.println(" not present in authorized list.");
  }
  if (FFat.exists("/keys_temp")) {
    deleteFile(FFat, "/keys_temp");
  }
}

// Polled function to check if a key has been recently read
void checkKey() {
  noInterrupts();
  wiegand.flush();
  interrupts();
  String keypadBuffer;
  int keypadCounter = 0;
  if (scannedKey == "") {
    return;
  //Othwerwise if the key pressed is 0A (*/ESC), then ring the bell and finish
  } else if ((scannedKey == "0A") && (add_mode == false)) {
    ringBell();
    scannedKey = "";
    return;
  } else if ((scannedKey == "0B")) {
    scannedKey = "";
    add_mode = false;
    return;
  }
  //function to recognize keypad input
  if ((scannedKey.length()) == 2) {
    Serial.println("Keypad entry...");
    keypadBuffer += scannedKey.substring(1);
    scannedKey = "";
    while ((keypadCounter < keypadDur) && (keypadBuffer.length() < 12)) {
      noInterrupts();
      wiegand.flush();
      interrupts();
      delay(100);
      keypadCounter++;
      //If the input was a keypad digit, then reset the inter-key timeout and wait for another one
      if ((scannedKey.length()) == 2) {
        keypadCounter = 0;
        //If the key pressed is 0B (#/ENT, then exit the loop of waiting for input
        if (scannedKey == "0B") {
          keypadCounter = keypadDur;
        } else if (scannedKey == "0A") {
          Serial.println("Cancelling keypad input");
          add_mode = false;
          scannedKey = "";
          return;
        } else {
          keypadBuffer += scannedKey.substring(1);
        }
        scannedKey = "";
      }
    }
    if (keypadBuffer.length() < 4) {
      Serial.println("Key too short - discarded");
      add_mode = false;
      playUnauthorizedTone();
      return;
    } else if (keypadBuffer.length() > 10) {
      Serial.println("Key too long - discarded");
      add_mode = false;
      playUnauthorizedTone();
      return;
    } else {
      scannedKey = keypadBuffer;
    }
  }

  if (MQTTclient.connected()) {
    MQTTclient.publish(config.mqtt_keys_topic, scannedKey.c_str());  
  } 

  bool match_found = keyAuthorized(scannedKey);

  if ((match_found == false) and (add_mode)) {
    writeKey(scannedKey);
  } else if (match_found and (add_mode)) {
    add_mode = false;
    Serial.print("Key ");
    Serial.print(scannedKey);
    Serial.println(" is already authorized!");
    playUnauthorizedTone();
  } else if (match_found and (add_mode == false)) {
    Serial.print("Key ");
    Serial.print(scannedKey);
    Serial.println(" is authorized!");
    unlock(keyDur);
  } else {
    add_mode = false;
    Serial.print("Key ");
    Serial.print(scannedKey);
    Serial.println(" is unauthorized!");
    setPixRed();
    playUnauthorizedTone();
    delay(unrecognizedKeyDur*1000);
    setPixBlue();
  }
  scannedKey = "";
  return;
}

// When any of the pins have changed, update the state of the wiegand library
void pinStateChanged() {
  wiegand.setPin0State(digitalRead(wiegand_0_pin));
  wiegand.setPin1State(digitalRead(wiegand_1_pin));
}

// Notifies when a reader has been connected or disconnected.
// Instead of a message, the seconds parameter can be anything you want -- Whatever you specify on `wiegand.onStateChange()`
void stateChanged(bool plugged, const char* message) {
  Serial.print(message);
  Serial.println(plugged ? "CONNECTED" : "DISCONNECTED");
}

// IRQ function for reading keys
void receivedData(uint8_t* data, uint8_t bits, const char* message) {
  String key = "";
  String key_buff = "";
  add_count = (addKeyDur * 100);
  //Print value in HEX
  uint8_t bytes = (bits+7)/8;
  for (int i=0; i<bytes; i++) {
    String FirstNum = (String (data[i] >> 4, 16));
    String SecondNum = (String (data[i] & 0xF, 16));
    key_buff = key;
    key = (FirstNum + SecondNum + key_buff);
  }
  scannedKey = key;
  scannedKey.toUpperCase();
  return;
}

// Notifies when an invalid transmission is detected
void receivedDataError(Wiegand::DataError error, uint8_t* rawData, uint8_t rawBits, const char* message) {
  Serial.print(message);
  Serial.print(Wiegand::DataErrorStr(error));
  Serial.print(" - Raw data: ");
  Serial.print(rawBits);
  Serial.print("bits / ");
  //Print value in HEX
  uint8_t bytes = (rawBits+7)/8;
  for (int i=0; i<bytes; i++) {
    Serial.print(rawData[i] >> 4, 16);
    Serial.print(rawData[i] & 0xF, 16);
  }
  Serial.println();
}

// --- SD Functions --- SD Functions --- SD Functions --- SD Functions --- SD Functions --- SD Functions --- SD Functions ---

void sd_setup() {
  SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if(!SD.begin(SD_CS_PIN)){
    Serial.println("SD filesystem mount failed");
    return;
  }
  Serial.println("SD filesystem successfully mounted");
  SD_mounted = true;
}


void checkSDPresent(int output) {
  if ((SD_present == false) && (digitalRead(SD_CD_PIN) == LOW)) {
    SD_present = true;
    if (output == 1) {
      Serial.println("SD Card inserted");
    }
  } else if ((SD_present == true) && (digitalRead(SD_CD_PIN) == HIGH)) {
    SD_present = false;
    if (output == 1) {
      Serial.println("SD Card ejected");
    }
  }
}

// --- FATFS Functions --- FATFS Functions --- FATFS Functions --- FATFS Functions --- FATFS Functions --- FATFS Functions --- FATFS Functions ---

void fatfs_setup() {
  if (FORMAT_FFAT) {
    FFat.format();
  }  
  if(!FFat.begin(true)){
    Serial.println("FFAT filesystem mount failed");
    return;
  }
  Serial.println("FFAT filesystem successfully mounted");
  FFat_present = true;
}

void listDir(fs::FS & fs, const char * dirname, uint8_t levels){
  Serial.printf("Listing directory: %s... ", dirname);
  File root = fs.open(dirname);
  if(!root){
    Serial.println("failed to open directory");
    return;
  }
  if(!root.isDirectory()){
    Serial.println("not a directory");
    return;
  }
  File file = root.openNextFile();
  while(file){
    if(file.isDirectory()){
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if(levels){
        listDir(fs, file.path(), levels -1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void readFile(fs::FS & fs, const char * path){
  Serial.printf("Reading file: %s... ", path);
  File file = fs.open(path);
  if(!file || file.isDirectory()){
    Serial.println("failed to open file for reading");
    return;
  }
  Serial.println("- read from file:");
  while(file.available()){
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS & fs, const char * path, const char * message){
  Serial.printf("Writing file: %s... ", path);
  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("failed to open file for writing");
    return;
  }
  if(file.print(message)){
      Serial.println("file written");
  } else {
    Serial.println("write failed");
  }
  file.close();
}

void appendlnFile(fs::FS & fs, const char * path, const char * message){
  //Serial.printf("Appending line to file: %s... ", path);
  File file = fs.open(path, FILE_APPEND);
  if(!file){
    Serial.println("- failed to open file for appending line");
    return;
  }
  if(file.println(message)){
    //Serial.println("message line appended");
  } else {
    Serial.println("append line failed");
  }
  file.close();
}

void renameFile(fs::FS & fs, const char * path1, const char * path2){
  //Serial.printf("Renaming file %s to %s... ", path1, path2);
  if (fs.rename(path1, path2)) {
    //Serial.println("file renamed");
  } else {
    Serial.println("rename failed");
  }
}

void deleteFile(fs::FS & fs, const char * path) {
  //Serial.printf("Deleting file: %s...", path);
  if (fs.remove(path)) {
    //Serial.println("file deleted");
  } else {
    Serial.println("delete failed");
  }
}

void createDir(fs::FS & fs, const char * path) {
  //Serial.printf("Creating Dir: %s\n", path);
  if (fs.mkdir(path)) {
    //Serial.println("Dir created");
  } else {
    Serial.println("mkdir failed");
  }
}

void removeDir(fs::FS & fs, const char * path) {
  //Serial.printf("Removing Dir: %s\n", path);
  if (fs.rmdir(path)) {
    //Serial.println("Dir removed");
  } else {
    Serial.println("rmdir failed");
  }
}

// Loads the configuration from a file
void loadFSJSON(const char* config_filename, Config& config) {
  // Open file for reading
  File file = FFat.open(config_filename);

  // Allocate a temporary JsonDocument
  JsonDocument doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println(F("Failed to read file, using default configuration"));
  }
  strlcpy(config.wifi_enabled, doc["wifi_enabled"] | "false", sizeof(config.wifi_enabled));
  strlcpy(config.wifi_ssid, doc["wifi_ssid"] | "null_wifi_ssid", sizeof(config.wifi_ssid));
  strlcpy(config.wifi_password, doc["wifi_password"] | "null_wifi_pass", sizeof(config.wifi_password));
  strlcpy(config.mqtt_enabled, doc["mqtt_enabled"] | "false", sizeof(config.mqtt_enabled));
  strlcpy(config.mqtt_server, doc["mqtt_server"] | "null_mqtt_server", sizeof(config.mqtt_server));
  strlcpy(config.mqtt_port, doc["mqtt_port"] | "1883", sizeof(config.mqtt_port));
  strlcpy(config.mqtt_topic, doc["mqtt_topic"] | "DEFAULT_dl32s3", sizeof(config.mqtt_topic));
  strlcpy(config.mqtt_stat_topic, doc["mqtt_topic"] | "DEFAULT_dl32s3", sizeof(config.mqtt_stat_topic));
  strlcpy(config.mqtt_cmnd_topic, doc["mqtt_topic"] | "DEFAULT_dl32s3", sizeof(config.mqtt_cmnd_topic));
  strlcpy(config.mqtt_keys_topic, doc["mqtt_topic"] | "DEFAULT_dl32s3", sizeof(config.mqtt_keys_topic));
  strlcpy(config.mqtt_addr_topic, doc["mqtt_topic"] | "DEFAULT_dl32s3", sizeof(config.mqtt_addr_topic));
  strlcpy(config.mqtt_uptm_topic, doc["mqtt_topic"] | "DEFAULT_dl32s3", sizeof(config.mqtt_uptm_topic));
  strcat(config.mqtt_stat_topic, "/stat");
  strcat(config.mqtt_cmnd_topic, "/cmnd");
  strcat(config.mqtt_keys_topic, "/keys");
  strcat(config.mqtt_addr_topic, "/addr");
  strcat(config.mqtt_uptm_topic, "/uptm");
  strlcpy(config.mqtt_client_name, doc["mqtt_client_name"] | "DEFAULT_dl32s3", sizeof(config.mqtt_client_name));
  strlcpy(config.mqtt_auth, doc["mqtt_auth"] | "true", sizeof(config.mqtt_auth));
  strlcpy(config.mqtt_user, doc["mqtt_user"] | "mqtt", sizeof(config.mqtt_user));
  strlcpy(config.mqtt_password, doc["mqtt_password"] | "null_mqtt_pass", sizeof(config.mqtt_password));
  file.close();
}

boolean configSDtoFFat() {
  if ((SD_present == true) && (SD.exists(config_filename))) {
    File sourceFile = SD.open(config_filename);
    File destFile = FFat.open(config_filename, FILE_WRITE);
    static uint8_t buf[1];
    while ( sourceFile.read( buf, 1) ) {
      destFile.write( buf, 1 );
    }
    destFile.close();
    sourceFile.close();
  } else {
    playUnauthorizedTone();
    Serial.println("No SD Card Mounted or no such file");
    return false;
  }
  playSuccessTone();
  Serial.println("Config file successfuly copied from SD to FFat");
  Serial.println("Restarting...");
  Serial.print("");
  ESP.restart();
  return true;
}

void addressingSDtoFS() {
  if ((SD_present == true) && (SD.exists(addressing_filename))) {
    File sourceFile = SD.open(addressing_filename);
    File destFile = FFat.open(addressing_filename, FILE_WRITE);
    static uint8_t buf[1];
    while ( sourceFile.read( buf, 1) ) {
      destFile.write( buf, 1 );
    }
    destFile.close();
    sourceFile.close();
  } else {
    Serial.println("");
    Serial.println("No SD Card Mounted or no such file");
    return;
  }
  Serial.println("Addressing file successfuly copied from SD to FFat");
  ESP.restart();
}

void keysSDtoFFat() {
  if ((SD_present == true) && (SD.exists(keys_filename))) {
    File sourceFile = SD.open(keys_filename);
    File destFile = FFat.open(keys_filename, FILE_WRITE);
    static uint8_t buf[1];
    while ( sourceFile.read( buf, 1) ) {
      destFile.write( buf, 1 );
    }
    destFile.close();
    sourceFile.close();
    Serial.println("Keys file successfuly copied from SD to FFat");
  } else {
    Serial.println("");
    playUnauthorizedTone();
    Serial.println("No SD Card Mounted or no such file");
    return;
  }
  playSuccessTone();
}

void keysFStoSD() {
  if ((SD_present == true) && (FFat.exists(keys_filename))){
    File sourceFile = FFat.open(keys_filename);
    File destFile = SD.open(keys_filename, FILE_WRITE);
    static uint8_t buf[1];
    while ( sourceFile.read( buf, 1) ) {
      destFile.write( buf, 1 );
    }
    destFile.close();
    sourceFile.close();
    Serial.println("Keys file successfuly copied from FFat to SD");
  } else {
    Serial.println("");
    Serial.println("No SD Card Mounted or no such file");
    return;
  }
  ESP.restart();
}

void addKeyMode() {
  setPixPurple();
  playAddModeTone();
    Serial.println("Add Key mode - Waiting for key");
  add_count = 0;
  add_mode = true;
  while (add_count < (addKeyDur * 100) && add_mode) {
    noInterrupts();
    wiegand.flush();
    interrupts();
    if (add_count % 100 == 0) {
      //playBipTone();
      playGeigerTone();
    }
    if (Serial.available()) {
      serialCmd = Serial.readStringUntil('\n');
      serialCmd.trim();
      Serial.print("Serial key input: ");
      Serial.println(serialCmd);
      scannedKey = serialCmd;
      writeKey(scannedKey);
    }
  
    delay(10);
    add_count++;
  }
  if (scannedKey == "") {
    add_mode = false;
    Serial.println("No new key added");
  } else {
    Serial.print("scannedKey: ");
  }
  setPixBlue();
  return;
}

// --- Neopixel Functions --- Neopixel Functions --- Neopixel Functions --- Neopixel Functions --- Neopixel Functions --- Neopixel Functions ---

//Onboard pixel uses G,R,B

void setPixRed() {
  pixel.setPixelColor(0, pixel.Color(0,25,0));
  pixel.show();
}

void setPixAmber() {
  pixel.setPixelColor(0, pixel.Color(10,25,0));
  pixel.show();
}

void setPixGreen() {
  pixel.setPixelColor(0, pixel.Color(25,0,0));
  pixel.show();
}

void setPixPurple() {
  pixel.setPixelColor(0, pixel.Color(0,10,25));
  pixel.show();
}

void setPixBlue() {
  pixel.setPixelColor(0, pixel.Color(0,0,25));
  pixel.show();
}

// --- Wifi Functions --- Wifi Functions --- Wifi Functions --- Wifi Functions --- Wifi Functions --- Wifi Functions --- Wifi Functions ---

int connectWifi() {

  WiFi.mode(WIFI_STA); //Optional
  Serial.print("Connecting to SSID " + (String)config.wifi_ssid);
  //Serial.print(" with password ");
  //Serial.print(config.wifi_password);
  int count = 0;
  lastWifiConnectAttempt = millis();
  WiFi.begin(config.wifi_ssid, config.wifi_password);
  
  while(WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(1000);
    count++;
    if (count > 10) {
      Serial.print("Unable to connect to SSID ");
      Serial.println(config.wifi_ssid);
      WiFi.disconnect();
      return 1;
    }
  }
  Serial.print("\nSuccessfully connected to SSID ");
  Serial.println(config.wifi_ssid);
  if (strcmp(config.mqtt_enabled, "true") == 0) {
    startMQTTConnection();
  }
  return 0;
}

// --- Action Functions --- Action Functions --- Action Functions --- Action Functions --- Action Functions --- Action Functions ---

void unlock(int secs) {
  int loops = 0;
  int count = 0;
  if (garage_mode) {
    Serial.println("Toggle garage door");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "Toggle garage door");
    }
    setPixGreen();
    digitalWrite(lockRelay_pin, HIGH);
    playUnlockTone();
    delay(garageDur);
    digitalWrite(lockRelay_pin, LOW);
    setPixBlue();
  return;
  }
  if (failSecure) {
    digitalWrite(lockRelay_pin, HIGH);
  } else {
    digitalWrite(lockRelay_pin, LOW);
  }
  playUnlockTone();
  Serial.print("Unlock - ");
  Serial.print(secs);
  Serial.println(" Seconds");
  setPixGreen();
  if (MQTTclient.connected()) {
    mqttPublish(config.mqtt_stat_topic, "unlocked");
  }
  while (loops < secs) {
    loops++;
    delay(1000);
  }
  Serial.println("Lock");
  setPixBlue();
    if (failSecure) {
    digitalWrite(lockRelay_pin, LOW);
  } else {
    digitalWrite(lockRelay_pin, HIGH);
  }
  if (MQTTclient.connected()) {
    MQTTclient.publish(config.mqtt_stat_topic, "locked", true);
  }
}

void garage_toggle() {
  if (garage_mode) {
    Serial.println("Toggle garage door");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "Toggle garage door");
    }
    setPixGreen();
    digitalWrite(lockRelay_pin, HIGH);
    playUnlockTone();
    delay(garageDur);
    digitalWrite(lockRelay_pin, LOW);
    setPixBlue();
  }
  return;
}

void garage_open() {
  if (garage_mode && (doorOpen == false)) {
    Serial.println("Opening garage door");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "Opening garage door");
    }
    setPixGreen();
    digitalWrite(lockRelay_pin, HIGH);
    playUnlockTone();
    delay(garageDur);
    digitalWrite(lockRelay_pin, LOW);
    setPixBlue();
  } else if  (garage_mode && (doorOpen)) {
    Serial.println("Door is already open!");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "Door is already open!");
    }
  } else if (garage_mode == false) {
    Serial.println("Unit is not in garage mode.");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "Unit is not in garage mode.");
    }
  }
  return;
}

void garage_close() {
  if ((garage_mode) && (doorOpen)) {
    Serial.println("Closing garage door");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "Closing garage door");
    }
    setPixGreen();
    digitalWrite(lockRelay_pin, HIGH);
    playUnlockTone();
    delay(garageDur);
    digitalWrite(lockRelay_pin, LOW);
    setPixBlue();
  } else if  (garage_mode && (doorOpen == false)) {
    Serial.println("Door is already closed!");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "Door is already closed!");
    }
  } else if (garage_mode == false) {
    Serial.println("Unit is not in garage mode.");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "Unit is not in garage mode.");
    }
  }
  return;
}

// --- Button Functions --- Button Functions --- Button Functions --- Button Functions --- Button Functions --- Button Functions ---

int checkExit() {
  long count = 0;
  if (digitalRead(exitButton_pin) == LOW) {
    while (digitalRead(exitButton_pin) == LOW) {
      count++;
      delay(10);
      if (count > 300) {
        addKeyMode();
        return 0;
      }
    }
    if (count > 5 && count < 501){
      Serial.print("Exit Button pressed");
      // Serial.print(" for ");
      // Serial.print(count);
      // Serial.print(" centiseconds");
      Serial.println("");
      if (MQTTclient.connected()) {
        String msg_str = ("Exit Button pressed"); // for " + String(count) + " centiseconds");
        char* msg_char = new char[msg_str.length() + 1];
        msg_str.toCharArray(msg_char, msg_str.length() + 1);
        mqttPublish(config.mqtt_stat_topic, msg_char);
      }
      unlock(exitButDur);
      return 0;
    }
  }
  return 0;
}

void checkAUX() {
  if (digitalRead(AUXButton_pin) == LOW) {
    long count = 0;
    Serial.println("AUX Button Pressed");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "AUX button pressed");
    }
    while (digitalRead(AUXButton_pin) == LOW && (count < 500)) {
      count++;
      delay(10);
    }
    if (count < 499) {
      Serial.print("Hardware revision: ");
      Serial.println(hwRev);
      Serial.print("Code version: ");
      Serial.println(codeVersion);
      Serial.print("SD Card present: ");
      if (digitalRead(SD_CD_PIN)) {
        Serial.println("false");
      } else {
        Serial.println("true");
      }
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      char IP[] = "xxx.xxx.xxx.xxx";
      IPAddress ip = WiFi.localIP();
      ip.toString().toCharArray(IP, 16);
      mqttPublish(config.mqtt_addr_topic, IP);
      Serial.println("Uptime: " + uptime_formatter::getUptime());
      publishUptime();
    }
    if (count > 499) {
      setPixPurple();
      Serial.println("Release button now to upload config (SD->FFAT)");
      playUploadTone();
    }
    while ((digitalRead(AUXButton_pin) == LOW) && (count < 1000)) {
      count++;
      delay(10);
    }
    if (count > 999) {
      setPixAmber();
      Serial.println("Release button now to wipe authorized keys list.)");
      playPurgeTone();
    }
    while (digitalRead(AUXButton_pin) == LOW && (count < 1500)) {
      count++;
      delay(10);
    }
    if (count > 1499) {
      setPixRed();
      Serial.println("Release button now to perform factory reset.)");
      playFactoryTone();
    }
    
    if ((count > 499)&&(count < 1000)) {
      Serial.print("Uploading config file ");
      Serial.print(config_filename);
      Serial.println(" from SD card to FFat");
      delay(1000);
      configSDtoFFat();
      delay(1000);
    } else if ((count > 999)&&(count < 1500)) {
      Serial.println("Purging stored keys... ");
      deleteFile(FFat, keys_filename);
      Serial.println("Keys purged!");
      delay(1000);
    } else if (count > 1499) {
      Serial.println("Factory resetting device... ");
      deleteFile(FFat, keys_filename);
      deleteFile(FFat, config_filename);
      Serial.println("Factory reset complete. Restarting.");
      ESP.restart();
      delay(3000);
    }
    setPixBlue();
    return;
  }
}

void checkBell() {
  if (digitalRead(bellButton_pin) == LOW) {
    Serial.println("");
    Serial.print("Bell Pressed - ");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "bell pressed");
    }
    ringBell();
  }
}

//Check magnetic sensor for open/closed door state (Optional)
void checkMagSensor() {
  if (doorOpen == true && digitalRead(magSensor_pin) == LOW) {
    //send not that door has closed
    doorOpen = false;
    Serial.println("Door Closed");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "door_closed");
    }
    //Serial.print("doorOpen == ");
    //Serial.println(doorOpen);
  } else if (doorOpen == false && digitalRead(magSensor_pin) == HIGH) {
    doorOpen = true;
    Serial.println("Door Opened");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "door_opened");
    }
    //Serial.print("doorOpen == ");
    //Serial.println(doorOpen);
  }
}

// --- Buzzer Functions --- Buzzer Functions --- Buzzer Functions --- Buzzer Functions --- Buzzer Functions --- Buzzer Functions ---

boolean playNote(note_t note, int octave, int dur) {
  if ((digitalRead(exitButton_pin) == LOW)) {
    return true;
  } else {
    ledcWriteNote(buzzer_pin, note, octave);
    delay(dur*100);
    ledcWriteTone(buzzer_pin, 0);
    return false;
  }
}

void playBipTone() {
  if (digitalRead(DS03) == HIGH) {
    ledcWriteTone(buzzer_pin, 100);
    delay(100);
    ledcWriteTone(buzzer_pin, 0);
  }
}

void playUnlockTone() {
  if (digitalRead(DS03) == HIGH) {
    for (int i = 0; i <= 1; i++) {
      ledcWriteTone(buzzer_pin, 5000);
      delay(50);
      ledcWriteTone(buzzer_pin, 0);
      delay(100);
    }
  }
}

void playUnauthorizedTone() {
  if (digitalRead(DS03) == HIGH) {
    ledcWriteTone(buzzer_pin, 700);
    delay(200);
    ledcWriteTone(buzzer_pin, 400);
    delay(600);
    ledcWriteTone(buzzer_pin, 0);
  }
}

void playPurgeTone() {
  if (digitalRead(DS03) == HIGH) {
    ledcWriteTone(buzzer_pin, 500);
    delay(1000);
    ledcWriteTone(buzzer_pin, 0);
  }
}

void playFactoryTone() {
  if (digitalRead(DS03) == HIGH) {
    ledcWriteTone(buzzer_pin, 7000);
    delay(1000);
    ledcWriteTone(buzzer_pin, 0);
  }
}

void playAddModeTone() {
  if (digitalRead(DS03) == HIGH) {
    for (int i = 0; i <= 2; i++) {
      ledcWriteTone(buzzer_pin, 6500);
      delay(80);
      ledcWriteTone(buzzer_pin, 0);
      delay(80);
    }
  }
}

void playUploadTone() {
  if (digitalRead(DS03) == HIGH) {
    for (int i = 0; i <= 4; i++) {
      ledcWriteTone(buzzer_pin, 6500);
      delay(80);
      ledcWriteTone(buzzer_pin, 0);
      delay(80);
    }
  }
}

void playSuccessTone() {
  if (digitalRead(DS03) == HIGH) {
    ledcWriteTone(buzzer_pin, 2000);
    delay(80);
    ledcWriteTone(buzzer_pin, 4000);
    delay(80);
    ledcWriteTone(buzzer_pin, 6000);
    delay(80);
    ledcWriteTone(buzzer_pin, 8000);
    delay(80);
    ledcWriteTone(buzzer_pin, 0);
  }
}

void ringBell() {
  playGreensleves();
}

//todo - not working, fix later
boolean playSeq(int noteBeg, note_t note, int octave, int dur) {
  if ((digitalRead(exitButton_pin) == LOW)) {
    seqTmr = 0;
    return true;
  } else if (noteBeg == seqTmr) {

    Serial.print("noteBeg = ");
    Serial.print(noteBeg);
    Serial.print("   seqTmr = ");
    Serial.println(seqTmr);

    int loopTmr = seqTmr;
    Serial.print("time to play ");
    Serial.print(note);
    Serial.print(" for ");
    Serial.println(dur);
    while (seqTmr < (loopTmr+dur)) {
      Serial.print(seqTmr);
      Serial.print(" should be less than ");
      Serial.println(loopTmr + dur);
      ledcWriteNote(buzzer_pin, note, octave);
      delay(dur * 10);
      ledcWriteTone(buzzer_pin, 0);
      seqTmr = (seqTmr + dur);
    }
  } else {
  Serial.print(seqTmr);
  Serial.println(" is not a time to play a note, incrementing");
  delay(10);
  seqTmr++;
  return false;
  }
}

void playTwinkle() {
  seqTmr = 0;
  playSeq(0,NOTE_C,5,26);
  playSeq(4,NOTE_C,5,26);
  playSeq(8,NOTE_G,5,26);
  playSeq(12,NOTE_G,5,26);
  playSeq(16,NOTE_A,5,26);
}

void playBowserTheme() {
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_C, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Eb, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_C, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Eb, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_C, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_C, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_C, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_C, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_F, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_Fs, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_F, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_E, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_F, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_E, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_Eb, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_E, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_F, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_Fs, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_F, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_E, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_F, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_E, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_Eb, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_E, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_C, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Eb, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_C, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Eb, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_G, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_C, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_C, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_C, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_D, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_Cs, 6, 1)) return;
  if (playNote(NOTE_Fs, 5, 1)) return;
  if (playNote(NOTE_C, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_F, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_Fs, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_F, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_E, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_F, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_E, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_Eb, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_E, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_F, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_Fs, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_F, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_E, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_F, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_E, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_Eb, 6, 1)) return;
  if (playNote(NOTE_Bb, 5, 1)) return;
  if (playNote(NOTE_E, 6, 1)) return;
}

void playGreensleves() {
  if (playNote(NOTE_A, 4, 4)) return;
  if (playNote(NOTE_C, 5, 8)) return;
  if (playNote(NOTE_D, 5, 4)) return;
  if (playNote(NOTE_E, 5, 6)) return;
  if (playNote(NOTE_F, 5, 2)) return;
  if (playNote(NOTE_E, 5, 4)) return;
  if (playNote(NOTE_D, 5, 8)) return;
  if (playNote(NOTE_B, 4, 4)) return;
  if (playNote(NOTE_G, 4, 6)) return;
  if (playNote(NOTE_A, 4, 2)) return;
  if (playNote(NOTE_B, 4, 4)) return;
  if (playNote(NOTE_C, 5, 8)) return;
  if (playNote(NOTE_A, 4, 4)) return;
  if (playNote(NOTE_A, 4, 6)) return;
  if (playNote(NOTE_Gs, 4, 2)) return;
  if (playNote(NOTE_A, 4, 4)) return;
  if (playNote(NOTE_B, 4, 8)) return;
  if (playNote(NOTE_Gs, 4, 4)) return;
  if (playNote(NOTE_E, 4, 8)) return;
  if (playNote(NOTE_A, 4, 4)) return;
  if (playNote(NOTE_C, 5, 8)) return;
  if (playNote(NOTE_D, 5, 4)) return;
  if (playNote(NOTE_E, 5, 6)) return;
  if (playNote(NOTE_F, 5, 2)) return;
  if (playNote(NOTE_E, 5, 4)) return;
  if (playNote(NOTE_D, 5, 8)) return;
  if (playNote(NOTE_B, 4, 4)) return;
  if (playNote(NOTE_G, 4, 6)) return;
  if (playNote(NOTE_A, 4, 2)) return;
  if (playNote(NOTE_B, 4, 4)) return;
  if (playNote(NOTE_C, 5, 6)) return;
  if (playNote(NOTE_B, 4, 2)) return;
  if (playNote(NOTE_A, 4, 4)) return;
  if (playNote(NOTE_Gs, 4, 6)) return;
  if (playNote(NOTE_Fs, 4, 2)) return;
  if (playNote(NOTE_Gs, 4, 4)) return;
  if (playNote(NOTE_A, 4, 20)) return;
}

void playGeigerTone() {
  ledcWrite(buzzer_pin, 0);
  for (int i=1; i<20; i++) {
    ledcWriteTone(buzzer_pin, i * 100);
    delay(10);    
  }
  ledcWrite(buzzer_pin, 0);
}

void playRandomTone() {
  if (digitalRead(DS03) == HIGH) {
    Serial.println("Ringing bell");
    for (int i = 0; i <= 3; i++) {
      for (int i = 0; i <= 25; i++) {
        ledcWriteTone(buzzer_pin, random(500, 10000));
        delay(100);
        if (digitalRead(exitButton_pin) == LOW) {
          return;
        }
      }
      ledcWriteTone(buzzer_pin, 0);
      delay(1000);
    }
    ledcWriteTone(buzzer_pin, 0);
  }
}

// --- MQTT Functions --- MQTT Functions --- MQTT Functions --- MQTT Functions --- MQTT Functions --- MQTT Functions ---

void startMQTTConnection() {
  MQTTclient.setServer(config.mqtt_server, String(config.mqtt_port).toInt());
  MQTTclient.setCallback(MQTTcallback);
  delay(100);
  if (MQTTclient.connected()) {
    MQTTclient.publish(config.mqtt_stat_topic, "locked", true);
  }
  lastMQTTConnectAttempt = millis();
}

void MQTTcallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  Serial.print("Message arrived TOPIC:[");
  Serial.print(topic);
  Serial.print("] PAYLOAD:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  executeCommand((char *)payload);
  return;
}

void maintainConnnectionMQTT() {
  if (!MQTTclient.connected()) {
    if (millis() - lastMQTTConnectAttempt > mqttReconnectInterval) {
      lastMQTTConnectAttempt = millis();
      // Attempt to reconnect
      Serial.print("Attempting reconnection to MQTT broker ");
      Serial.print(config.mqtt_server);
      Serial.println("... ");
      mqttConnect();
    }
  }
}

boolean mqttConnect() {
  if ((!strcmp(config.mqtt_enabled, "true") == 0 || (WiFi.status() != WL_CONNECTED))) {
    return false;
  }
  if (strcmp(config.mqtt_auth, "true") == 0) {
    if (MQTTclient.connect(config.mqtt_client_name, config.mqtt_user, config.mqtt_password)) {
      Serial.print("Connected to MQTT broker ");
      Serial.print(config.mqtt_server);
      Serial.print(" as ");
      Serial.println(config.mqtt_client_name);
      mqttPublish(config.mqtt_stat_topic, "Connected to MQTT Broker");
      MQTTclient.subscribe(config.mqtt_cmnd_topic);
      char IP[] = "xxx.xxx.xxx.xxx";
      IPAddress ip = WiFi.localIP();
      ip.toString().toCharArray(IP, 16);
      Serial.print("Publishing IP address to topic: ");
      Serial.println(config.mqtt_addr_topic);
      mqttPublish(config.mqtt_addr_topic, IP);
      return MQTTclient.connected();
    }
  } else {
    if (MQTTclient.connect(config.mqtt_client_name)) {
      Serial.print("Connected to MQTT broker ");
      Serial.print(config.mqtt_server);
      Serial.print(" as ");
      Serial.println(config.mqtt_client_name);
      mqttPublish(config.mqtt_stat_topic, "Connected to MQTT Broker");
      MQTTclient.subscribe(config.mqtt_cmnd_topic);
      char IP[] = "xxx.xxx.xxx.xxx";
      IPAddress ip = WiFi.localIP();
      ip.toString().toCharArray(IP, 16);
      Serial.print("Publishing IP address to topic: ");
      Serial.println(config.mqtt_addr_topic);
      mqttPublish(config.mqtt_addr_topic, IP); 
      return MQTTclient.connected();
    }
  }
  Serial.print("Unable to connect to MQTT broker ");
  Serial.println(config.mqtt_server);
  return false;
}

void checkMqtt() {
  if (MQTTclient.connected()) {
    MQTTclient.loop();
    unsigned long now = millis();
    if (now - lastMsg > 2000) {
      lastMsg = now;
    }
  }
}

boolean mqttPublish(char* topic, char* payload) {
  if (MQTTclient.connected()) {
    MQTTclient.publish(topic, payload);
    return true;
  } else {
    return false;
  }
}

// --- Hardware Revision Functions --- Hardware Revision Functions --- Hardware Revision Functions ---

void detectHardwareRevision() {
  if ((float)(analogRead(attSensor_pin)/4095*3.3) == 0.00) {
    hwRev = 20240812;
    Serial.println("Hardware revision 20240812 detected");
  } else if ((float)(analogRead(attSensor_pin)/4095*3.3) == 0.29) {
    hwRev = 20250000;
    Serial.println("Hardware revision 2025xxxx detected");
  }
}

// --- Serial Commands --- Serial Commands --- Serial Commands --- Serial Commands --- Serial Commands ---

void checkSerialCmd() {
  if (Serial.available()) {
    serialCmd = Serial.readStringUntil('\n');
    serialCmd.trim();
    Serial.print("Command: ");
    Serial.println(serialCmd);
    executeCommand(serialCmd);
  }
}

boolean executeCommand(String command) {
  if (command.equals("list_commands")) {
    listCmnds();
  } else if (command.equals("list_ffat")) {
    listDir(FFat, "/", 0);
  } else if (command.equals("list_sd")) {
    listDir(SD, "/", 0);
  } else if (command.equals("list_keys")) {
    outputKeys();
  } else if (command.equals("purge_keys")) {
    deleteFile(FFat, keys_filename);
  } else if (command.equals("purge_config")) {
    deleteFile(FFat, config_filename);
  } else if (command.equals("add_key_mode")) {
    addKeyMode();
  } else if (command.equals("ring_bell")) {
    ringBell();
  } else if (command.equals("copy_keys_sd_to_ffat")) {
    keysSDtoFFat();
  } else if (command.equals("copy_config_sd_to_ffat")) {
    configSDtoFFat();
  } else if (command.equals("show_config")) {
    readFile(FFat, config_filename);
  } else if (command.equals("show_version")) {
    Serial.println(codeVersion);
  } else if (command.equals("restart")) {
    Serial.println("Restarting device...");
    ESP.restart();
  } else if (command.equals("reboot")) {
    Serial.println("Restarting device...");
    ESP.restart();
  } else if (command.equals("unlock")) {
    unlock(5);
  } else if (command.equals("uptime")) {
    publishUptime();
    printUptime();
  } else if ((command.equals("garage_toggle")&& garage_mode)) {
    Serial.println("Toggling garage door");
    mqttPublish(config.mqtt_stat_topic, "Toggling garage door");
    garage_toggle();
  } else if ((command.equals("garage_open")&& garage_mode)) {
    Serial.println("Toggling garage door");
    mqttPublish(config.mqtt_stat_topic, "Opening garage door");
    garage_open();
  } else if ((command.equals("garage_close")&& garage_mode)) {
    Serial.println("Closing garage door");
    mqttPublish(config.mqtt_stat_topic, "Closing garage door");
    garage_close();
  } else {
    Serial.println("bad command");
    return false;
  }
  return true;
}

void listCmnds() {
  if (MQTTclient.connected()) {
    MQTTclient.publish(config.mqtt_stat_topic, "add_key_mode\ncopy_config_sd_to_ffat\ncopy_keys_sd_to_ffat\ngarage_close\ngarage_open\ngarage_toggle\nlist_ffat\nlist_keys\nlist_sd\npurge_config\npurge_keys\nrestart\nring_bell\nshow_config\nshow_version\nunlock\nuptime\n");
  }
  Serial.printf("add_key_mode\ncopy_config_sd_to_ffat\ncopy_keys_sd_to_ffat\ngarage_close\ngarage_open\ngarage_toggle\nlist_ffat\nlist_keys\nlist_sd\npurge_config\npurge_keys\nrestart\nring_bell\nshow_config\nshow_version\nunlock\nuptime\n");
}

// --- Web Functions --- Web Functions --- Web Functions --- Web Functions --- Web Functions --- Web Functions ---

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += webServer.uri();
  message += "\nMethod: ";
  message += (webServer.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += webServer.args();
  message += "\n";
  for (uint8_t i = 0; i < webServer.args(); i++) {
    message += " " + webServer.argName(i) + ": " + webServer.arg(i) + "\n";
  }
  webServer.send(404, "text/plain", message);
}

// Download list of allowed keys
void downloadKeysHTTP() {
  int result = FFat_file_download(keys_filename);
  sendHTMLHeader();
  siteButtons();
  if (result == 0) {
    pageContent += F("<br/> <textarea readonly>Downloading keys file</textarea>");
  } else {
    pageContent += F("<br/> <textarea readonly>Unable to download keys file</textarea>");
  }
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
}

// Download copy of config file (config.json)
void downloadConfigHTTP() {
  int result = FFat_file_download(config_filename);
  sendHTMLHeader();
  siteButtons();
  if (result == 0) {
    pageContent += F("<br/> <textarea readonly>Downloading ");
    pageContent += F(config_filename); 
    pageContent += F("</textarea>");
  } else {
    pageContent += F("<br/> <textarea readonly>Unable to download ");
    pageContent += F(config_filename);
    pageContent += F("</textarea>");
  }
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
}

// Output a list of allowed keys to serial
void outputKeys() {
  Serial.print("Reading file: ");
  Serial.println(keys_filename);
  File outFile = FFat.open(keys_filename);
  while (outFile.available()) {
    Serial.write(outFile.read());
  }
  sendHTMLHeader();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Keys output to serial.</textarea>");
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
  outFile.close();
  Serial.println("\n");
}

// Restart unit
void restartESPHTTP() {
  webServer.sendHeader("Location", "/",true);  
  webServer.send(302, "text/plain", "");
  Serial.println("Garage Door toggled HTTP");
  Serial.println("Restarting ESP...");
  Serial.println("\n");
  ESP.restart();
}

// Display allowed keys in webUI
void displayKeys() {
  pageContent += F("<br/> <table class='keyTable'>");
  char buffer[64];
  int count = 0;
  File keyFile = FFat.open(keys_filename);
  while (keyFile.available()) {
    int l = keyFile.readBytesUntil('\n', buffer, sizeof(buffer));
    buffer[l] = 0;
    pageContent += F("<tr><td class='keyCell'>");
    pageContent += F(buffer);
    pageContent += F("</td><td class='keyDelCell'>");
    pageContent += F("<a class='keyDelLink' href='/remKey/");
    pageContent += F(buffer);
    pageContent += F("'>DELETE</a>");
    pageContent += F("</td></tr>");
    count++;
  }
  if (count < 1) {
    pageContent += F("<tr><td class='keyCell'>");
    pageContent += F("[NO SAVED KEYS]");
    pageContent += F("</td></tr>");
  }
  pageContent += F("</table>");
  keyFile.close();
  pageContent += F("<form action='/addFormKey/' method='get'>");
  pageContent += F("<input type='text' id='key' name='key' class='addKeyInput'></input>");
  pageContent += F("<input type='submit' value='ADD' class='addKeyButton' required>");
  pageContent += F("</form>");
  
}

// Output config to serial
void outputConfig() {
  Serial.print("Reading file: ");
  Serial.println(config_filename);
  File outFile = FFat.open(config_filename);
  while (outFile.available()) {
    Serial.write(outFile.read());
  }
  sendHTMLHeader();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Config output to serial.</textarea>");
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
  outFile.close();
  Serial.println("\n");
}

// Display config in webUI
void displayConfig() {
  sendHTMLHeader();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>");
  char buffer[64];
  File dispFile = FFat.open(config_filename);
  while (dispFile.available()) {
    int l = dispFile.readBytesUntil('\n', buffer, sizeof(buffer));
    buffer[l] = 0;
    pageContent += F(buffer);
  }
  pageContent += F("</textarea>");
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
  dispFile.close();
}

void MainPage() {
  sendHTMLHeader();
  siteButtons();
  displayKeys();
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
}

void unlockHTTP() {
  webServer.sendHeader("Location", "/",true);  
  webServer.send(302, "text/plain", "");
  Serial.println("Unlocked via HTTP");
  if (MQTTclient.connected()) {
    mqttPublish(config.mqtt_stat_topic, "HTTP Unlock");
  }  
  unlock(httpDur);
}

void garageToggleHTTP() {
  webServer.sendHeader("Location", "/",true);  
  webServer.send(302, "text/plain", "");
  Serial.println("Garage Door toggled HTTP");
  if (MQTTclient.connected()) {
    mqttPublish(config.mqtt_stat_topic, "Garage Door toggled HTTP");
  }  
  garage_toggle();
}

void garageOpenHTTP() {
  webServer.sendHeader("Location", "/",true);  
  webServer.send(302, "text/plain", "");
  if (doorOpen == false) {
    Serial.println("Garage Door opened HTTP");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "Garage Door opened HTTP");
    }  
    garage_open();
  }
}

void garageCloseHTTP() {
  webServer.sendHeader("Location", "/",true);  
  webServer.send(302, "text/plain", "");
  if (doorOpen) {
    Serial.println("Garage Door closed via HTTP");
    if (MQTTclient.connected()) {
      mqttPublish(config.mqtt_stat_topic, "Garage Door closed via HTTP");
    }
    garage_close();
  }
}

void configSDtoFFatHTTP() {
  configSDtoFFat();
  sendHTMLHeader();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Copied configuration from SD to FFat</textarea>");
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
}

void keysSDtoFFatHTTP() {
  webServer.sendHeader("Location", "/",true);  
  webServer.send(302, "text/plain", "");
  keysSDtoFFat();
}

void purgeKeysHTTP() {
  deleteFile(FFat, keys_filename);
  webServer.sendHeader("Location", "/",true);  
  webServer.send(302, "text/plain", "");
}

void addKeyModeHTTP() {
  webServer.sendHeader("Location", "/",true);  
  webServer.send(302, "text/plain", "");
  addKeyMode();
}

void purgeConfigHTTP() {
  webServer.sendHeader("Location", "/",true);  
  webServer.send(302, "text/plain", "");
  deleteFile(FFat, config_filename);
}

int FFat_file_download(String filename) {
  if (FFat_present) {
    File download = FFat.open(filename);
    if (download) {
      webServer.sendHeader("Content-Type", "text/text");
      webServer.sendHeader("Content-Disposition", "attachment; filename=" + filename);
      webServer.sendHeader("Connection", "close");
      webServer.streamFile(download, "application/octet-stream");
      download.close();
      Serial.print("Downloading file ");
      Serial.println(filename);
      return 0;
    }
  }
  return 1;
}

void ringBellHTTP() {
  webServer.sendHeader("Location", "/",true);
  webServer.send(302, "text/plain", "");
  ringBell();
}

void displayAddressingHTTP() {
  sendHTMLHeader();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>");
  pageContent += F("IP Address: ");
  //pageContent += F(String(WiFi.localIP()));
  pageContent += F("<br/>");
  pageContent += F("Subnet Mask: ");
  //pageContent += F(String(WiFi.subnetMask()));
  pageContent += F("<br/>");
  pageContent += F("Default Gateway: ");
  //pageContent += F(String(WiFi.gatewayIP()));
  pageContent += F("</textarea>");
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
}

void outputAddressingHTTP() {
  Serial.println("Current IP Addressing:");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Subnet Mask: ");
  Serial.println(WiFi.subnetMask());
  Serial.print("Defalt Gateway: ");
  Serial.println(WiFi.gatewayIP());
  sendHTMLHeader();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Addressing output to serial.</textarea>");
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
  Serial.println("\n");
}

int parseSDAddressingFile () {
  //TODO
}

void saveAddressingStaticHTTP() {
  //TODO
}

void downloadAddressingStaticHTTP() {
  int result = FFat_file_download(addressing_filename);
  sendHTMLHeader();
  siteButtons();
  if (result == 0) {
    pageContent += F("<br/> <textarea readonly>Downloading addressing file</textarea>");
  } else {
    pageContent += F("<br/> <textarea readonly>Unable to download addressing file</textarea>");
  }
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
}

void addressingStaticSDtoFFatHTTP() {
  //addressingSDtoFFat();
  sendHTMLHeader();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Copied static addressing from SD to FFat</textarea>");
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
}

void purgeAddressingStaticHTTP() {
  deleteFile(FFat, addressing_filename);
  sendHTMLHeader();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>static addressing purged</textarea>");
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
}

// Output a list of FS files to serial
void outputFSHTTP() {
  listDir(FFat, "/", 0);
  sendHTMLHeader();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>FS files output to serial.</textarea>");
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
}

// Display a list of FS files in webpage
void displayFSHTTP() {
  sendHTMLHeader();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>WIP</textarea>");
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
}

// Output a list of SD files to serial
void outputSDFSHTTP() {
  listDir(SD, "/", 0);
  sendHTMLHeader();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>SD files output to serial.</textarea>");
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
}

// Display a list of SD files in webpage
void displaySDFSHTTP() {
  sendHTMLHeader();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>WIP</textarea>");
  siteModes();
  siteFooter();
  sendHTMLContent();
  sendHTMLStop();
}

void sendHTMLHeader() {
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", "");
  siteHeader();
  webServer.sendContent(pageContent);
  pageContent = "";
}

void sendHTMLContent() {
  webServer.sendContent(pageContent);
  pageContent = "";
}

void sendHTMLStop() {
  webServer.sendContent("");
  webServer.client().stop();
}

void siteHeader() {
  pageContent  = F("<!DOCTYPE html>");
  pageContent += F("<html>");
  pageContent += F("<head >");
  pageContent += F("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  pageContent += F("<style>");

  //Orange Theme
  pageContent += F("div {width: 350px; margin: 20px auto; text-align: center; border: 3px solid #ff3200; background-color: #555555; left: auto; right: auto;}");
  pageContent += F(".header {font-family: Arial, Helvetica, sans-serif; font-size: 20px; color: #ff3200;}");
  pageContent += F(".smalltext {font-family: Arial, Helvetica, sans-serif; font-size: 13px; color: #ff3200;}");
  pageContent += F(".keyTable {background-color: #303030; font-size: 11px; width: 300px; resize: vertical; margin-left: auto; margin-right: auto; border: 1px solid #ff3200; border-collapse: collapse;}");
  pageContent += F(".keyCell {height: 15px; color: #ff3200;}");
  pageContent += F(".keyDelCell {height: 15px; width: 45px; background-color: #ff3200; color: black;}");
  pageContent += F(".keyDelCell:hover {height: 15px; width: 45px; background-color: #ef2200; color: black; border: 1px solid #000000}");
  pageContent += F(".keyDelLink {font-family: Arial, Helvetica, sans-serif; font-size: 10px; color: black; text-decoration: none;}");
  pageContent += F(".addKeyInput {height 17px; width: 245px;border: 1px solid #ff3200; font-family:Arial, Helvetica, sans-serif; font-size: 10px; color: black;}");
  pageContent += F(".addKeyButton {height 30px; width: 49px; background-color: #ff3200; border: none; font-family:Arial, Helvetica, sans-serif; font-size: 10px; color: black; border: 1px solid #ff3200;}");
  pageContent += F(".addKeyButton:hover {height 30px; width: 49px; background-color: #ef2200; border: none; font-family: Arial, Helvetica, sans-serif; font-size: 10px; color: black; border: 1px solid #ff3200;}");
  pageContent += F("tr, td {border: 1px solid #ff3200;}");
  pageContent += F("button {width: 300px; background-color: #ff3200; border: none; text-decoration: none;}");
  pageContent += F("button:hover {width: 300px; background-color: #ef2200; border: none; text-decoration: none;}");
  pageContent += F("h1 {font-family: Arial, Helvetica, sans-serif; color: #ff3200;}");
  pageContent += F("h3 {font-family: Arial, Helvetica, sans-serif; color: #ff3200;}");
  pageContent += F("a {font-family: Arial, Helvetica, sans-serif; font-size: 10px; color: #ff3200;}");
  pageContent += F("textarea {background-color: #303030; font-size: 11px; width: 300px; height: 150px; resize: vertical; color: #ff3200;}");
  pageContent += F("body {background-color: #303030; text-align: center;}");
  pageContent += F("</style>");
  pageContent += F("</head>");
  pageContent += F("<body><div><h1>DL32 MENU</h1>");
  pageContent += F("<h3>");
  pageContent += F(config.mqtt_client_name);
  pageContent += F("</h3>");
}

void siteModes() {
  int modeCount = 0;
  pageContent += F("<br/>");
  pageContent += F("<a class='smalltext'>");
  if (forceOffline) {
    pageContent += F(" [Forced Offline Mode] ");
    modeCount++;
  }
  if (failSecure == false) {
    pageContent += F(" [Failsafe Mode] ");
    modeCount++;
  }
  if (digitalRead(DS03) == LOW) {
    pageContent += F(" [Silent Mode] ");
    modeCount++;
  }
  if (garage_mode) {
    pageContent += F(" [Garage Mode] ");
    modeCount++;
  }
  pageContent += F("</a>");
  if (modeCount > 0) {
    pageContent += F("<br/>");
  }
}

void siteButtons() {
  pageContent += F("<a class='header'>Device Control</a>");
  if (garage_mode) {
    pageContent += F("<a href='/garageToggleHTTP'><button>Toggle</button></a>");
    pageContent += F("<br/>");
    if (doorOpen == false) {
      pageContent += F("<a href='/garageOpenHTTP'><button>Open</button></a>");
    }
    if (doorOpen) {
      pageContent += F("<a href='/garageCloseHTTP'><button>Close</button></a>");
    }
  } else {
    pageContent += F("<a href='/unlockHTTP'><button>Unlock</button></a>");
  }
  pageContent += F("<br/>");
  pageContent += F("<a href='/ringBellHTTP'><button>Ring Bell</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/restartESPHTTP'><button>Restart DL32</button></a>");
  pageContent += F("<br/> <br/>");
 
  pageContent += F("<a class='header'>System</a>");
  pageContent += F("<a href='/downloadConfigHTTP'><button>Download config file</button></a>");
  pageContent += F("<br/>");
  //pageContent += F("<a href='/outputConfig'><button>Output config to Serial</button></a>");
  //pageContent += F("<br/>");
  pageContent += F("<a href='/displayConfig'><button>Display config in page</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/configSDtoFFatHTTP'><button>Upload config SD to DL32</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/purgeConfigHTTP'><button>Purge configuration</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/update'><button>Update firmware</button></a>");
  pageContent += F("<br/><br/>");

  pageContent += F("<a class='header'>Key Management</a>");
  pageContent += F("<a href='/downloadKeysHTTP'><button>Download key file</button></a>");
  pageContent += F("<br/>");
  //pageContent += F("<a href='/outputKeys'><button>Output keys to Serial</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/displayKeys'><button>Display keys in page</button></a>");
  //pageContent += F("<br/>");
  pageContent += F("<a href='/keysSDtoFFatHTTP'><button>Upload keys SD to DL32</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/addKeyModeHTTP'><button>Enter add key mode</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/purgeKeysHTTP'><button>Purge stored keys</button></a>");
  pageContent += F("<br/>");

  //pageContent += F("<a class='header'>Filesystem Operations</a>");
  //pageContent += F("<a href='/outputFSHTTP'><button>Output FFat Contents to Serial</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/displayFSHTTP'><button style='background-color: #999999; color: #777777';>Display FFat contents in page</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/outputSDFSHTTP'><button>Output SD FS Contents to Serial</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/displaySDFSHTTP'><button style='background-color: #999999; color: #777777';>Display SD FS contents in page</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a class='header'>IP Addressing</a>");
  //pageContent += F("<a href='/displayAddressingHTTP'><button style='background-color: #999999; color: #777777';>Show IP addressing</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/outputAddressingHTTP'><button>Output IP addressing to Serial</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/saveAddressingStaticHTTP'><button style='background-color: #999999; color: #777777';>Save current addressing as static</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/downloadAddressingStaticHTTP'><button style='background-color: #999999; color: #777777';>Download static addressing file</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/addressingStaticSDtoFFatHTTP'><button style='background-color: #999999; color: #777777';>Upload static addressing SD to DL32</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/purgeAddressingStaticHTTP'><button style='background-color: #999999; color: #777777';>Purge static addressing</button></a>");
  //pageContent += F("<br/>");
}

void siteFooter() {
  IPAddress ip_addr = WiFi.localIP();
  pageContent += F("<a class='smalltext'>");
  pageContent += F("IP: ");
  pageContent += (String(ip_addr[0]) + "." + String(ip_addr[1]) + "." + String(ip_addr[2]) + "." + String(ip_addr[3]));
  pageContent += F("</a>");
  pageContent += F("&nbsp;&nbsp;&nbsp;&nbsp;");
  pageContent += F("<a class='smalltext'>");
  pageContent += F("ver: ");
  pageContent += (String(codeVersion));
  pageContent += F("&nbsp;&nbsp;&nbsp;&nbsp;");
  pageContent += F("</a>");
  pageContent += F("<a class='smalltext' href='https://github.com/Mark-Roly/DL32' target='_blank' rel='noopener noreferrer'>");
  pageContent += F("github");
  pageContent += F("</a>");
  pageContent += F("<br/><br/></div>");
  pageContent += F("</body></html>");
}

void echoUri() {
  webServer.send(200, "text//plain", webServer.uri() );
}

void startWebServer() {
  webServer.on("/downloadKeysHTTP", downloadKeysHTTP);
  webServer.on("/outputKeys", outputKeys);
  webServer.on("/unlockHTTP", unlockHTTP);
  webServer.on("/garageToggleHTTP", garageToggleHTTP);
  webServer.on("/garageOpenHTTP", garageOpenHTTP);
  webServer.on("/garageCloseHTTP", garageCloseHTTP);
  webServer.on("/ringBellHTTP", ringBellHTTP);
  webServer.on("/downloadConfigHTTP", downloadConfigHTTP);
  webServer.on("/addKeyModeHTTP", addKeyModeHTTP);
  webServer.on("/purgeKeysHTTP", purgeKeysHTTP);
  webServer.on("/displayConfig", displayConfig);
  webServer.on("/outputConfig", outputConfig);
  webServer.on("/outputFSHTTP", outputFSHTTP);
  webServer.on("/displayFSHTTP", displayFSHTTP);
  webServer.on("/outputSDFSHTTP", outputSDFSHTTP);
  webServer.on("/displaySDFSHTTP", displaySDFSHTTP);
  webServer.on("/purgeConfigHTTP", purgeConfigHTTP);
  webServer.on("/restartESPHTTP", restartESPHTTP);
  webServer.on("/configSDtoFFatHTTP", configSDtoFFatHTTP);
  webServer.on("/keysSDtoFFatHTTP", keysSDtoFFatHTTP);
  webServer.on("/displayAddressingHTTP", displayAddressingHTTP);
  webServer.on("/outputAddressingHTTP", outputAddressingHTTP);
  webServer.on("/saveAddressingStaticHTTP", saveAddressingStaticHTTP);
  webServer.on("/downloadAddressingStaticHTTP", downloadAddressingStaticHTTP);
  webServer.on("/addressingStaticSDtoFFatHTTP", addressingStaticSDtoFFatHTTP);
  webServer.on("/purgeAddressingStaticHTTP", purgeAddressingStaticHTTP);
  webServer.on(UriRegex("/addKey/([0-9a-zA-Z]{4,8})"), HTTP_GET, [&]() {
    writeKey(webServer.pathArg(0));
    MainPage();
  });
  webServer.on(UriRegex("/addKey/[?](1)key[=](1)([0-9a-zA-Z]{4,16})"), HTTP_GET, [&]() {
    Serial.println(webServer.pathArg(0));
    writeKey(webServer.pathArg(0));
    MainPage();
  });
  webServer.on(UriRegex("/remKey/([0-9a-zA-Z]{3,16})"), HTTP_GET, [&]() {
    removeKey(webServer.pathArg(0));
    MainPage();
  });
  webServer.on(UriRegex("/serial/([0-9a-zA-Z_-]{3,10})"), HTTP_GET, [&]() {
    Serial.print("URL Command entered: ");
    Serial.print(webServer.pathArg(0));
    executeCommand(webServer.pathArg(0));
    MainPage();
  });
  webServer.on("/", MainPage);
  webServer.on(UriRegex("/addFormKey/.{0,20}"), HTTP_GET, [&]() {
    Serial.print("Writing key ");
    Serial.print(webServer.arg("key"));
    Serial.println(" from webUI input.");
    writeKey(webServer.arg("key"));
    MainPage();
  });

  ElegantOTA.begin(&webServer); // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);

  webServer.begin();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Web Server started at http://");
    Serial.println(WiFi.localIP());
  }
}

// --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP ---

void setup() {
  delay(500);
  Serial.begin(115200);
  delay(500);
  Serial.println("Initializing WDT...");
  secondTick.attach(1, ISRwatchdog);
  fatfs_setup();
  sd_setup();
  checkSDPresent(0);
  detectHardwareRevision();
  Serial.print("DL32 firmware version ");
  Serial.println(codeVersion);
  Serial.println("configuring GPIO...");
  Serial.flush();
  pinMode(buzzer_pin, OUTPUT);
  pinMode(lockRelay_pin, OUTPUT);
  pinMode(exitButton_pin, INPUT_PULLUP);
  pinMode(wiegand_0_pin, INPUT);
  pinMode(wiegand_1_pin, INPUT);
  pinMode(AUXButton_pin, INPUT_PULLUP);
  pinMode(bellButton_pin, INPUT_PULLUP);
  pinMode(magSensor_pin, INPUT_PULLUP);
  pinMode(SD_CS_PIN, OUTPUT);
  pinMode(SD_CD_PIN, INPUT_PULLUP);
  pinMode(DS01, INPUT_PULLUP);
  pinMode(DS02, INPUT_PULLUP);
  pinMode(DS03, INPUT_PULLUP);
  pinMode(DS04, INPUT_PULLUP);
  digitalWrite(buzzer_pin, LOW);

  // Should load default config if run for the first time
  Serial.println(F("Loading configuration..."));
  loadFSJSON(config_filename, config);

  // Check Dip Switch states
  if (digitalRead(DS01) == LOW) {
    Serial.print("DIP Switch #1 ON");
    Serial.println(" - Forced offline mode");
    forceOffline = true;
  }
  if (digitalRead(DS02) == LOW) {
    failSecure = false;
    Serial.print("DIP Switch #2 ON");
    Serial.println(" - FailSafe Strike mode");
  }
  if (digitalRead(DS03) == LOW) {
    Serial.print("DIP Switch #3 ON");
    Serial.println(" - Silent mode");
  }
  if (digitalRead(DS04) == LOW) {
    Serial.print("DIP Switch #4 ON");
    Serial.println(" - Garage mode");
    garage_mode = true;
  }
  if (strcmp(config.wifi_enabled, "false") == 0) {
    Serial.print("Wifi disabled");
    Serial.println(" - Forced offline mode");
    forceOffline = true;
  }

  if (failSecure) {
    digitalWrite(lockRelay_pin, LOW);
  } else {
    digitalWrite(lockRelay_pin, HIGH);
  }

  if (forceOffline == false) {
    connectWifi();
    startWebServer();
    if (strcmp(config.mqtt_enabled, "true") == 0) {
      Serial.print("Attempting connection to MQTT broker ");
      Serial.print(config.mqtt_server);
      Serial.println("... ");
      mqttConnect();
    }
  } else {
    Serial.println("Running in offline mode.");
  }
    
  pixel.begin();

  // instantiate listeners and initialize Wiegand reader, configure pins
  wiegand.onReceive(receivedData, "Key read: ");
  wiegand.onReceiveError(receivedDataError, "Key read error: ");
  wiegand.begin(Wiegand::LENGTH_ANY, true);
  attachInterrupt(digitalPinToInterrupt(wiegand_0_pin), pinStateChanged, CHANGE);
  attachInterrupt(digitalPinToInterrupt(wiegand_1_pin), pinStateChanged, CHANGE);
  pinStateChanged();
  ledcAttachChannel(buzzer_pin, freq, resolution, channel);
  setPixBlue();
  Serial.println("Ready.");
}

// --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP ---

void loop() {
  if (forceOffline == false) {
    if (WiFi.status() == WL_CONNECTED) {
      if (disconCount > 0) {
        Serial.println("Wifi available - Restarting");
        ESP.restart();
      }
      //Online Functions
      webServer.handleClient();
      ElegantOTA.loop();
      if (strcmp(config.mqtt_enabled, "true") == 0) {
        maintainConnnectionMQTT();
        checkMqtt();
      }
      disconCount = 0;
    } else {
      if (millis() > (lastWifiConnectAttempt + wifiReconnectInterval)) {
        disconCount = 0;

        Serial.print("millis: ");
        Serial.println(millis());

        Serial.print("lastWifiConnectAttempt: ");
        Serial.println(lastWifiConnectAttempt);

        Serial.print("wifiReconnectInterval: ");
        Serial.println(wifiReconnectInterval);

        Serial.println("WiFi reconnection attempt...");
         connectWifi();
      }
      else if (disconCount == 0) {
        Serial.println("Disconnected from WiFi");
      }
      disconCount++;
    }
    checkMqtt();
  }
  
  // Offline Functions
  checkKey();
  checkMagSensor();
  checkSerialCmd();
  checkExit();
  checkAUX();
  checkBell();
  checkSDPresent(1);
  watchdogCount = 0;
  delay(10);
}