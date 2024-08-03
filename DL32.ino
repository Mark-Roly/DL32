/*

  DL32 v3 by Mark Booth
  For use with Wemos S3 and DL32 S3 rev 20231220
  Last updated 04/08/2024

  https://github.com/Mark-Roly/DL32/

  Upload settings:
    Upload speed: 921600
    USB Mode: Hardware CDC and JTAG
    USB CDC on Boot: Enabled
    USB Firmware MSC on boot: disabled
    USB DFU on boot: disabled
    Upload Mode: UART0/Hardware CDC
    CPU frequency 240Mhz
    Partition scheme: Default 4mb with ffat
    
*/

// Include Libraries
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <WebServer.h>
#include <SPI.h>
#include <Wiegand.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Ticker.h>
#include "FFat.h"
#include "FS.h"
#include "SD.h"

// 3.0 Pins - Uncomment if using S3 Wemos board revision
#define buzzer_pin 14
#define neopix_pin 11
#define ob_neopix_pin 47
#define lockRelay_pin 1
#define AUXButton_pin 6
#define exitButton_pin 21
#define bellButton_pin 17
#define magSensor_pin 15
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
#define GH06 9
#define SD_CS_PIN 34
#define SD_CLK_PIN 38
#define SD_MOSI_PIN 36
#define SD_MISO_PIN 35

// Define struct for storing configuration
struct Config {
  char ssid[32];
  char wifi_password[32];
  char mqtt_enabled[8];
  char mqtt_server[32];
  char mqtt_port[8];
  char mqtt_topic[32];
  char mqtt_cmnd_topic[32];
  char mqtt_stat_topic[32];
  char mqtt_keys_topic[32];
  char mqtt_client_name[32];
  char mqtt_auth[8];
  char mqtt_user[32];
  char mqtt_password[32];
};

// sd card pins:
// 3.0 SD card Pins
// CD DAT3 CS 34
// CMD DI DIN MOSI 36
// CLK SCLK 38
// DAT0 D0 MISO 35

// Durations for each unlock type (eg: unlock for 10 seconds if unlocked via MQTT)
#define exitButDur 5
#define httpDur 5
#define keypadDur 15 //tenths-of-seconds
#define keyDur 5
#define mqttDur 10
#define addKeyDur 10
#define unrecognizedKeyDur 5
#define WDT_TIMEOUT 60

// Number of neopixels used
#define NUMPIXELS 1

#define codeVersion 20240804

long lastMsg = 0;
long disconCount = 0;
char msg[50];
int value = 0;
int add_count = 0;
String scannedKey = "";

boolean validKeyRead = false;
boolean forceOffline = false;
boolean invalidKeyRead = false;
boolean SD_present = false;
boolean FFat_present = false;
boolean doorOpen = true;
boolean add_mode = false;
String serialCmd;

String pageContent = "";
const char* config_filename = "/dl32.json";
const char* keys_filename = "/keys.txt";

// buzzer settings
int freq = 2000;
int channel = 0;
int resolution = 8;

// integer for watchdog counter
volatile int watchdogCount = 0;

// Define onboard and offvoard neopixels
Adafruit_NeoPixel pixel = Adafruit_NeoPixel(NUMPIXELS, neopix_pin, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel ob_pixel = Adafruit_NeoPixel(NUMPIXELS, ob_neopix_pin, NEO_GRB + NEO_KHZ800);

// instantiate objects for Configuration struct, wifi client, webserver, mqtt client, qiegand reader and WDT timer
Config config;
WiFiClient esp32Client;
WebServer webServer(80);
PubSubClient MQTTclient(esp32Client);
Wiegand wiegand;
Ticker secondTick;

// counter for mqtt reconnection attempts
long lastMQTTReconnectAttempt = 0;

// --- Watchdog Functions --- Watchdog Functions --- Watchdog Functions --- Watchdog Functions --- Watchdog Functions --- Watchdog Functions ---

void ISRwatchdog() {
  watchdogCount++;
  if (watchdogCount == WDT_TIMEOUT) {
    Serial.println("Watchdog invoked!");
    ESP.restart();
  }
}

// --- Wiegand Functions --- Wiegand Functions --- Wiegand Functions --- Wiegand Functions --- Wiegand Functions --- Wiegand Functions --- Wiegand Functions ---

// function that checks if a provided key is present in the authorized list
boolean keyAuthorized(String key) {
  File keysFile = FFat.open(keys_filename, "r");
  int charMatches = 0;
  char tagBuffer[11];
  Serial.print("Checking key: ");
  Serial.println(key);
  while (keysFile.available()) {
    int cardDigits = (keysFile.readBytesUntil('\n', tagBuffer, sizeof(tagBuffer))-1);
    //Serial.print("card digits = ");
    //Serial.println(String(cardDigits));
    tagBuffer[cardDigits] = 0;
    charMatches = 0;
    for (int loopCount = 0; loopCount < (cardDigits); loopCount++) {
      //Serial.print("comparing ");
      //Serial.print(key[loopCount]);
      //Serial.print(" with ");
      //Serial.println(tagBuffer[loopCount]);
      if (key[loopCount] == tagBuffer[loopCount]) {
        charMatches++;
      }
    }
    //Serial.print("charMatches: ");
    //Serial.println(charMatches);
    //Serial.print("cardDigits: ");
    //Serial.println(cardDigits);
    if (charMatches == cardDigits) {
      //Serial.print(tagBuffer);
      //Serial.print(" - ");
      //Serial.println("MATCH");
      //matchedCards++;
      return true;
    } else {
      //Serial.println("NO MATCH");
    }
  }
  keysFile.close();
  return false;
}

// Polled function to check if a key has been recently read
void checkKey() {
  noInterrupts();
  wiegand.flush();
  String keypadBuffer;
  int keypadCounter = 0;
  interrupts();
  if (scannedKey == "") {
    return;
  } else if ((scannedKey.substring(1)) == "A") {
    ringBell();
    scannedKey = "";
    return;
  }
  
  //function to recognize keypad input
  if ((scannedKey.length()) == 2) {
    Serial.print("Keypad entry: ");
    keypadBuffer += scannedKey.substring(1);
    scannedKey = "";
    while ((keypadCounter < keypadDur) && (keypadBuffer.length() < 12)) {
      noInterrupts();
      wiegand.flush();
      interrupts();
      delay(100);
      keypadCounter++;
      if ((scannedKey.length()) == 2) {
        keypadCounter = 0;
        if ((scannedKey.substring(1)) == "B") {
          keypadCounter = keypadDur;
        } else {
          keypadBuffer += scannedKey.substring(1);
        }
        scannedKey = "";
      }
    }
    Serial.println(keypadBuffer);
    scannedKey = keypadBuffer;
  }

  if (MQTTclient.connected()) {
    MQTTclient.publish(config.mqtt_keys_topic, scannedKey.c_str());  
  } 

  bool match_found = keyAuthorized(scannedKey);
  if ((match_found == false) and (add_mode)) {
    appendlnFile(FFat, keys_filename, scannedKey.c_str());
    add_mode = false;
    Serial.print("Added key ");
    Serial.print(scannedKey);
    Serial.println(" to authorized list");
  } else if (match_found and (add_mode)) {
    add_mode = false;
    Serial.print("Key ");
    Serial.print(scannedKey);
    Serial.println(" is already authorized!");
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

// IRQ function for read cards
void receivedData(uint8_t* data, uint8_t bits, const char* message) {
  String key = "";
  add_count = (addKeyDur * 100);
  //Print value in HEX
  uint8_t bytes = (bits+7)/8;
  for (int i=0; i<bytes; i++) {
    String FirstNum = (String (data[i] >> 4, 16));
    String SecondNum = (String (data[i] & 0xF, 16));
    key = (key + FirstNum + SecondNum);
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
  SD_present = true;
}

// --- FATFS Functions --- FATFS Functions --- FATFS Functions --- FATFS Functions --- FATFS Functions --- FATFS Functions --- FATFS Functions ---

void fatfs_setup() {
  if(!FFat.begin(true)){
    Serial.println("FFAT filesystem mount failed");
    return;
  }
  Serial.println("FFAT filesystem successfully mounted");
  FFat_present = true;
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
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

void readFile(fs::FS &fs, const char * path){
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

void writeFile(fs::FS &fs, const char * path, const char * message){
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

void appendFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Appending to file: %s... ", path);

    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("- failed to open file for appending");
        return;
    }
    if(file.print(message)){
        Serial.println("message appended");
    } else {
        Serial.println("append failed");
    }
    file.close();
}

void appendlnFile(fs::FS &fs, const char * path, const char * message){
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

void renameFile(fs::FS &fs, const char * path1, const char * path2){
  Serial.printf("Renaming file %s to %s... ", path1, path2);
  if (fs.rename(path1, path2)) {
    Serial.println("file renamed");
  } else {
    Serial.println("rename failed");
  }
}

void deleteFile(fs::FS & fs, const char * path) {
  Serial.printf("Deleting file: %s...", path);
  if (fs.remove(path)) {
    Serial.println("file deleted");
  } else {
    Serial.println("delete failed");
  }
}

void createDir(fs::FS & fs, const char * path) {
  Serial.printf("Creating Dir: %s\n", path);
  if (fs.mkdir(path)) {
    Serial.println("Dir created");
  } else {
    Serial.println("mkdir failed");
  }
}

void removeDir(fs::FS & fs, const char * path) {
  Serial.printf("Removing Dir: %s\n", path);
  if (fs.rmdir(path)) {
    Serial.println("Dir removed");
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
  strlcpy(config.ssid, doc["ssid"] | "null_ssid", sizeof(config.ssid));
  strlcpy(config.wifi_password, doc["wifi_password"] | "null_wifi_password", sizeof(config.wifi_password));
  strlcpy(config.mqtt_enabled, doc["mqtt_enabled"] | "false", sizeof(config.mqtt_enabled));
  strlcpy(config.mqtt_server, doc["mqtt_server"] | "null_mqtt_server", sizeof(config.mqtt_server));
  strlcpy(config.mqtt_port, doc["mqtt_port"] | "1883", sizeof(config.mqtt_port));
  strlcpy(config.mqtt_topic, doc["mqtt_topic"] | "DEFAULT_dl32s3", sizeof(config.mqtt_topic));
  
  strlcpy(config.mqtt_stat_topic, doc["mqtt_topic"] | "DEFAULT_dl32s3", sizeof(config.mqtt_stat_topic));
  strlcpy(config.mqtt_cmnd_topic, doc["mqtt_topic"] | "DEFAULT_dl32s3", sizeof(config.mqtt_cmnd_topic));
  strlcpy(config.mqtt_keys_topic, doc["mqtt_topic"] | "DEFAULT_dl32s3", sizeof(config.mqtt_keys_topic));
  strcat(config.mqtt_stat_topic, "/stat");
  strcat(config.mqtt_cmnd_topic, "/cmnd");
  strcat(config.mqtt_keys_topic, "/keys");
  
  strlcpy(config.mqtt_client_name, doc["mqtt_client_name"] | "DEFAULT_dl32s3", sizeof(config.mqtt_client_name));
  strlcpy(config.mqtt_auth, doc["mqtt_auth"] | "true", sizeof(config.mqtt_auth));
  strlcpy(config.mqtt_user, doc["mqtt_user"] | "mqtt", sizeof(config.mqtt_user));
  strlcpy(config.mqtt_password, doc["mqtt_password"] | "null_mqtt_password", sizeof(config.mqtt_password));
  file.close();
}

void configSDtoFFat() {
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
    Serial.println("");
    Serial.println("No SD Card Mounted or no such file");
    return;
  }
  Serial.println("Config file successfuly copied from SD to FFat");
}

void addressingSDtoFS() {
  if ((SD_present == true) && (SD.exists("/addressing.json"))) {
    File sourceFile = SD.open("/addressing.json");
    File destFile = FFat.open("/addressing.json", FILE_WRITE);
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
    Serial.println("No SD Card Mounted or no such file");
    return;
  }
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
}

int addKeyMode() {
  playAddModeTone();
  Serial.println("Add Key mode - Waiting for key");
  add_count = 0;
  add_mode = true;
  while (add_count < (addKeyDur * 100) && add_mode) {
    noInterrupts();
    wiegand.flush();
    interrupts();
    delay(10);
    add_count++;
  }
  if (scannedKey == "") {
    add_mode = false;
    Serial.println("No new key added");
  }
  return 1;
}

// --- Neopixel Functions --- Neopixel Functions --- Neopixel Functions --- Neopixel Functions --- Neopixel Functions --- Neopixel Functions ---

void setPixRed() {
  pixel.setPixelColor(0, pixel.Color(10,0,0));
  pixel.show();
}

void setPixAmber() {
  pixel.setPixelColor(0, pixel.Color(10,3,0));
  pixel.show();
}

void setPixGreen() {
  pixel.setPixelColor(0, pixel.Color(0,10,0));
  pixel.show();
}

void setPixPurple() {
  pixel.setPixelColor(0, pixel.Color(10,0,10));
  pixel.show();
}

void setPixBlue() {
  pixel.setPixelColor(0, pixel.Color(0,0,10));
  pixel.show();
}

void setOBPixRed() {
  ob_pixel.setPixelColor(0, pixel.Color(0,10,0));
  ob_pixel.show();
}

void setOBPixAmber() {
  ob_pixel.setPixelColor(0, pixel.Color(10,3,0));
  ob_pixel.show();
}

void setOBPixGreen() {
  ob_pixel.setPixelColor(0, pixel.Color(10,0,0));
  ob_pixel.show();
}

void setOBPixBlue() {
  ob_pixel.setPixelColor(0, pixel.Color(0,0,10));
  ob_pixel.show();
}

// --- Wifi Functions --- Wifi Functions --- Wifi Functions --- Wifi Functions --- Wifi Functions --- Wifi Functions --- Wifi Functions ---

int connectWifi() {

  WiFi.mode(WIFI_STA); //Optional
  Serial.print("Connecting to SSID ");
  Serial.print(config.ssid);
  //Serial.print(" with password ");
  //Serial.print(config.wifi_password);
  int count = 0;
  WiFi.begin(config.ssid, config.wifi_password);
  
  while(WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(1000);
    count++;
    if (count > 10) {
      Serial.print("Unable to connect to SSID ");
      Serial.println(config.ssid);
      return 1;
    }    
  }

  Serial.print("\nConnected to SSID ");
  Serial.println(config.ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  if (strcmp(config.mqtt_enabled, "true") == 0) {
    startMQTTConnection();
  }
  return 0;
}

// --- Command Functions --- Command Functions --- Command Functions --- Command Functions --- Command Functions --- Command Functions ---

void unlock(int secs) {
  int loops = 0;
  int count = 0;
  Serial.print("Unlock - ");
  Serial.print(secs);
  Serial.println(" Seconds");
  digitalWrite(lockRelay_pin, HIGH);
  setPixGreen();
  if (MQTTclient.connected()) {
    MQTTclient.publish(config.mqtt_stat_topic, "unlocked");
  }
  playUnlockTone();
  while (loops < secs) {
    loops++;
    delay(1000);
  }
  Serial.println("Lock");
  setPixBlue();
  digitalWrite(lockRelay_pin, LOW);
  if (MQTTclient.connected()) {
    MQTTclient.publish(config.mqtt_stat_topic, "locked");
  }
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
      Serial.println("Exit Button Pressed");
      if (MQTTclient.connected()) {
        MQTTclient.publish(config.mqtt_stat_topic, "button pressed");
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
      MQTTclient.publish(config.mqtt_stat_topic, "AUX button pressed");
    }
    while (digitalRead(AUXButton_pin) == LOW && (count < 500)) {
      count++;
      delay(10);
    }
    if (count > 499) {
      setPixPurple();
      Serial.print("Uploading config file ");
      Serial.print(config_filename);
      Serial.print(" from SD card to FFat, restarting...");
      delay(1000);
      configSDtoFFat();
      delay(1000);
      ESP.restart();
    }
    //Serial.println(count);
    return;
  }
}

void checkBell() {
  if (digitalRead(bellButton_pin) == LOW) {
    Serial.println("");
    Serial.print("Bell Pressed - ");
    if (MQTTclient.connected()) {
      MQTTclient.publish(config.mqtt_stat_topic, "bell pressed");
    }
    ringBell();
  }
}

//Check magnetic sensor for open/closed door state (Optional)
void checkMagSensor() {
  if (doorOpen == true && digitalRead(magSensor_pin) == LOW) {
    //send not that door has closed
    doorOpen = false;
    Serial.println("Door Opened");
  } else if (doorOpen == false && digitalRead(magSensor_pin) == HIGH) {
    doorOpen = true;
    Serial.println("Door Closed");
  }
}

// --- Buzzer Functions --- Buzzer Functions --- Buzzer Functions --- Buzzer Functions --- Buzzer Functions --- Buzzer Functions ---

void playUnlockTone() {
  if (digitalRead(DS03) == HIGH) {
    ledcWriteTone(buzzer_pin, 5000);
    delay(100);
    ledcWriteTone(buzzer_pin, 0);
    delay(100);
    ledcWriteTone(buzzer_pin, 5000);
    delay(100);
    ledcWriteTone(buzzer_pin, 0);
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

void playAddModeTone() {
  if (digitalRead(DS03) == HIGH) {
    ledcWriteTone(buzzer_pin, 6500);
    delay(80);
    ledcWriteTone(buzzer_pin, 0);
    delay(80);
    ledcWriteTone(buzzer_pin, 6500);
    delay(80);
    ledcWriteTone(buzzer_pin, 0);
    delay(80);
    ledcWriteTone(buzzer_pin, 6500);
    delay(80);
    ledcWriteTone(buzzer_pin, 0);
  }
}

void ringBell() {
  if (digitalRead(DS03) == HIGH) {
    Serial.println("Ringing bell");
    for (int i = 0; i <= 3; i++) {
      for (int i = 0; i <= 25; i++) {
        ledcWriteTone(buzzer_pin, random(500, 10000));
        delay(75);
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
  lastMQTTReconnectAttempt = 0;
}

boolean mqttReconnect() {
  if (strcmp(config.mqtt_auth, "true") == 0) {
    if (MQTTclient.connect(config.mqtt_client_name, config.mqtt_user, config.mqtt_password)) {
      Serial.print("Connected to MQTT broker ");
      Serial.print(config.mqtt_server);
      Serial.print(" as ");
      Serial.println(config.mqtt_client_name);
      MQTTclient.publish(config.mqtt_stat_topic, "Connected to MQTT Broker");
      MQTTclient.subscribe(config.mqtt_cmnd_topic);
    }
  } else {
    if (MQTTclient.connect(config.mqtt_client_name)) {
      Serial.print("Connected to MQTT broker ");
      Serial.print(config.mqtt_server);
      Serial.print(" as ");
      Serial.println(config.mqtt_client_name);
      MQTTclient.publish(config.mqtt_stat_topic, "Connected to MQTT Broker");
      MQTTclient.subscribe(config.mqtt_cmnd_topic);
    }
  }
  return MQTTclient.connected();
}

void MQTTcallback(char* topic, byte* payload, unsigned int length) {
  Serial.println("");
  Serial.print("Message arrived TOPIC:[");
  Serial.print(topic);
  Serial.print("] PAYLOAD:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Unlock if first letter of payload is "u"
  if ((char)payload[0] == 'u') {
    Serial.println("Unlocked via MQTT");
    MQTTclient.publish(config.mqtt_stat_topic, "lock opened via MQTT");
    unlock(mqttDur);
  } else if ((char)payload[0] == 'b') {
    Serial.println("Bell via MQTT");
    MQTTclient.publish(config.mqtt_stat_topic, "Bell rung via MQTT");
    ringBell();
  } else {
    Serial.println("");
    Serial.print("Message not recognized: ");
  }
}

void maintainConnnectionMQTT() {
  if (!MQTTclient.connected()) {
    long now = millis();
    if (now - lastMQTTReconnectAttempt > 5000) {
      lastMQTTReconnectAttempt = now;
      // Attempt to reconnect
      if (mqttReconnect()) {
        lastMQTTReconnectAttempt = 0;
      }
    }
  }
}

void checkMqtt() {
  if (MQTTclient.connected()) {
    MQTTclient.loop();
    long now = millis();
    if (now - lastMsg > 2000) {
      lastMsg = now;
      ++value;
    }
  }
}

// --- Serial Functions --- Serial Functions --- Serial Functions --- Serial Functions --- Serial Functions ---

void checkSerialCmd() {
  if (Serial.available()) {
    serialCmd = Serial.readStringUntil('\n');
    serialCmd.trim();
    if (serialCmd.equals("list_ffat")) {
      listDir(FFat, "/", 0);
    } else if (serialCmd.equals("list_sd")) {
      listDir(SD, "/", 0);
    } else if (serialCmd.equals("read_config")) {
      readFile(FFat, config_filename);
    } else if (serialCmd.equals("restart")) {
      Serial.println("Restarting device...");
      ESP.restart();
    } else if (serialCmd.equals("reboot")) {
      Serial.println("Restarting device...");
      ESP.restart();
    } else if (serialCmd.equals("unlock")) {
      unlock(5);
    } else {
      Serial.println("bad command");
    }
    Serial.print("Command: ");
    Serial.println(serialCmd);
  }
}

// --- Web Functions --- Web Functions --- Web Functions --- Web Functions --- Web Functions --- Web Functions ---

void handleRoot() {
  webServer.send(200, "text/plain", "hello from ESP32!");
  Serial.print("Message arrived TOPIC:[");
}

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

// Download list of allowed keys (keys.txt)
void DownloadKeysHTTP() {
  int result = FFat_file_download(keys_filename);
  SendHTML_Header();
  siteButtons();
  if (result == 0) {
    pageContent += F("<br/> <textarea readonly>Downloading keys.txt</textarea>");
  } else {
    pageContent += F("<br/> <textarea readonly>Unable to download keys.txt</textarea>");
  }
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

// Download copy of config file (config.json)
void DownloadConfigHTTP() {
  int result = FFat_file_download(config_filename);
  SendHTML_Header();
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
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

// Output a list of allowed keys to serial
void OutputKeys() {
  Serial.println("Reading file: keys.txt");
  File outFile = FFat.open(keys_filename);
  while (outFile.available()) {
    Serial.write(outFile.read());
  }
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Keys output to serial.</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
  outFile.close();
  Serial.println("\n");
}

// Output a list of allowed keys to serial
void restartESPHTTP() {
  Serial.println("Restarting ESP...");
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Restarting ESP...</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
  Serial.println("\n");
  ESP.restart();
}

// Display allowed keys in webUI
void DisplayKeys() {
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>");
  char buffer[64];
  File dispFile = FFat.open(keys_filename);
  while (dispFile.available()) {
    int l = dispFile.readBytesUntil('\n', buffer, sizeof(buffer));
    buffer[l] = 0;
    pageContent += F(buffer);
  }
  pageContent += F("</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
  dispFile.close();
}

// Output config to serial
void OutputConfig() {
  Serial.print("Reading file: ");
  Serial.println(config_filename);
  File outFile = FFat.open(config_filename);
  while (outFile.available()) {
    Serial.write(outFile.read());
  }
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Config output to serial.</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
  outFile.close();
  Serial.println("\n");
}

// Display config in webUI
void DisplayConfig() {
  SendHTML_Header();
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
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
  dispFile.close();
}

void MainPage() {
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly></textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void UnlockHTTP() {
  Serial.println("Unlocked via HTTP");
  if (MQTTclient.connected()) {
    MQTTclient.publish(config.mqtt_stat_topic, "HTTP Unlock");
  }  
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Door Unlocked</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
  unlock(httpDur);
}

void configSDtoFFatHTTP() {
  configSDtoFFat();
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Copied configuration from SD to FFat</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void keysSDtoFFatHTTP() {
  keysSDtoFFat();
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Copied keys from SD to FFat</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void purgeKeysHTTP() {
  deleteFile(FFat, keys_filename);
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Keys Purged</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void purgeConfigHTTP() {
  deleteFile(FFat, config_filename);
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Config Purged</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

int FFat_file_download(String filename) {
  if (FFat_present) {
    File download = FFat.open("/" + filename);
    if (download) {
      webServer.sendHeader("Content-Type", "text/text");
      webServer.sendHeader("Content-Disposition", "attachment; filename=" + filename);
      webServer.sendHeader("Connection", "close");
      webServer.streamFile(download, "application/octet-stream");
      download.close();
      return 0;
    }
  }
  return 1;
}

void RingBellHTTP() {
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Ringing bell</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
  ringBell();
}

void displayAddressingHTTP() {
  SendHTML_Header();
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
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void outputAddressingHTTP() {
  Serial.println("Current IP Addressing:");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Subnet Mask: ");
  Serial.println(WiFi.subnetMask());
  Serial.print("Defalt Gateway: ");
  Serial.println(WiFi.gatewayIP());
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Addressing output to serial.</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
  Serial.println("\n");
}

void saveAddressingStaticHTTP() {
  //TODO
}

void downloadAddressingStaticHTTP() {
  int result = FFat_file_download("addressing.json");
  SendHTML_Header();
  siteButtons();
  if (result == 0) {
    pageContent += F("<br/> <textarea readonly>Downloading addressing.json</textarea>");
  } else {
    pageContent += F("<br/> <textarea readonly>Unable to download addressing.json</textarea>");
  }
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void addressingStaticSDtoFFatHTTP() {
  //addressingSDtoFFat();
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Copied static addressing from SD to FFat</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void purgeAddressingStaticHTTP() {
  deleteFile(FFat, "/addressing.json");
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>static addressing purged</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

// Output a list of FS files to serial
void OutputFSHTTP() {
  listDir(FFat, "/", 0);
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>FS files output to serial.</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

// Display a list of FS files in webpage
void DisplayFSHTTP() {
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>WIP</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

// Output a list of SD files to serial
void OutputSDFSHTTP() {
  listDir(SD, "/", 0);
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>SD files output to serial.</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

// Display a list of SD files in webpage
void DisplaySDFSHTTP() {
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>WIP</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void SendHTML_Header() {
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", "");
  siteHeader();
  webServer.sendContent(pageContent);
  pageContent = "";
}

void SendHTML_Content() {
  webServer.sendContent(pageContent);
  pageContent = "";
}

void SendHTML_Stop() {
  webServer.sendContent("");
  webServer.client().stop();
}

void siteHeader() {
  pageContent  = F("<!DOCTYPE html>");
  pageContent += F("<html>");
  pageContent += F("<head>");
  pageContent += F("<style>");

  //Orange Theme
  pageContent += F("div {width: 350px; margin: 20px auto; text-align: center; border: 3px solid #ff3200; background-color: #555555; left: auto; right: auto;}");
  pageContent += F(".header {font-family: Arial, Helvetica, sans-serif; font-size: 20px; color: #ff3200;}");
  pageContent += F(".smalltext {font-family: Arial, Helvetica, sans-serif; font-size: 10px; color: #ff3200;}");
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
}

void siteButtons() {
  pageContent += F("<a class='header'>Device Control</a>");
  pageContent += F("<a href='/UnlockHTTP'><button>HTTP Unlock</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/RingBellHTTP'><button>Ring Bell</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/restartESPHTTP'><button>Restart DL32</button></a>");
  pageContent += F("<br/> <br/>");
  pageContent += F("<a class='header'>Key List</a>");
  pageContent += F("<a href='/DownloadKeysHTTP'><button>Download key file</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/OutputKeys'><button>Output keys to Serial</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/DisplayKeys'><button>Display keys in page</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/keysSDtoFFatHTTP'><button>Upload keys SD to DL32</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/purgeKeysHTTP'><button>Purge stored keys</button></a>");
  pageContent += F("<br/> <br/>");
  pageContent += F("<a class='header'>Config File</a>");
  pageContent += F("<a href='/DownloadConfigHTTP'><button>Download config file</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/OutputConfig'><button>Output config to Serial</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/DisplayConfig'><button>Display config in page</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/configSDtoFFatHTTP'><button>Upload config SD to DL32</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/purgeConfigHTTP'><button>Purge configuration</button></a>");
  pageContent += F("<br/> <br/>");
  pageContent += F("<a class='header'>Filesystem Operations</a>");
  pageContent += F("<a href='/OutputFSHTTP'><button>Output FFat Contents to Serial</button></a>");
  pageContent += F("<br/>");
  //pageContent += F("<a href='/DisplayFSHTTP'><button style='background-color: #999999; color: #777777';>Display FFat contents in page</button></a>");
  //pageContent += F("<br/>");
  pageContent += F("<a href='/OutputSDFSHTTP'><button>Output SD FS Contents to Serial</button></a>");
  pageContent += F("<br/>");
  //pageContent += F("<a href='/DisplaySDFSHTTP'><button style='background-color: #999999; color: #777777';>Display SD FS contents in page</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a class='header'>IP Addressing</a>");
  //pageContent += F("<a href='/displayAddressingHTTP'><button style='background-color: #999999; color: #777777';>Show IP addressing</button></a>");
  //pageContent += F("<br/>");
  pageContent += F("<a href='/outputAddressingHTTP'><button>Output IP addressing to Serial</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/saveAddressingStaticHTTP'><button style='background-color: #999999; color: #777777';>Save current addressing as static</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/downloadAddressingStaticHTTP'><button style='background-color: #999999; color: #777777';>Download static addressing file</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/addressingStaticSDtoFFatHTTP'><button style='background-color: #999999; color: #777777';>Upload static addressing SD to DL32</button></a>");
  //pageContent += F("<br/>");
  //pageContent += F("<a href='/purgeAddressingStaticHTTP'><button style='background-color: #999999; color: #777777';>Purge static addressing</button></a>");
  pageContent += F("<br/>");
}

void siteFooter() {
  IPAddress ip_addr = WiFi.localIP();
  pageContent += F("<br/>");
  pageContent += F("<a class='smalltext'>");
  pageContent += F("IP: ");
  pageContent += (String(ip_addr[0]) + "." + String(ip_addr[1]) + "." + String(ip_addr[2]) + "." + String(ip_addr[3]));
  pageContent += F("</a>");
  pageContent += F("&nbsp;&nbsp;");
  pageContent += F("<a class='smalltext'>");
  pageContent += F("ver: ");
  pageContent += (String(codeVersion));
  pageContent += F("</a>");
  pageContent += F(" <br/></div>");
  pageContent += F("</body></html>");
}

void startWebServer() {
  webServer.on("/DownloadKeysHTTP", DownloadKeysHTTP);
  webServer.on("/DisplayKeys", DisplayKeys);
  webServer.on("/OutputKeys", OutputKeys);
  webServer.on("/UnlockHTTP", UnlockHTTP);
  webServer.on("/RingBellHTTP", RingBellHTTP);
  webServer.on("/DownloadConfigHTTP", DownloadConfigHTTP);
  webServer.on("/purgeKeysHTTP", purgeKeysHTTP);
  webServer.on("/DisplayConfig", DisplayConfig);
  webServer.on("/OutputConfig", OutputConfig);
  webServer.on("/OutputFSHTTP", OutputFSHTTP);
  webServer.on("/DisplayFSHTTP", DisplayFSHTTP);
  webServer.on("/OutputSDFSHTTP", OutputSDFSHTTP);
  webServer.on("/DisplaySDFSHTTP", DisplaySDFSHTTP);
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
  webServer.on("/", MainPage);
  webServer.begin();
  Serial.print("Web Server started at http://");
  Serial.println(WiFi.localIP());
}

// --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP ---

void setup() {
  delay(500);
  Serial.begin(115200);
  Serial.println("Configuring WDT...");
  secondTick.attach(1, ISRwatchdog);
  fatfs_setup();
  sd_setup();
  Serial.print("DL32 firmware version ");
  Serial.println(codeVersion);
  Serial.println("Initializing...");
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
  pinMode(DS01, INPUT_PULLUP);
  pinMode(DS02, INPUT_PULLUP);
  pinMode(DS03, INPUT_PULLUP);
  pinMode(DS04, INPUT_PULLUP);
  digitalWrite(buzzer_pin, LOW);
  digitalWrite(lockRelay_pin, LOW);

  // Check Dip Switch states
  if (digitalRead(DS01) == LOW) {
    Serial.print("Dip Switch #1 ON");
    Serial.println(" - Forced offline mode");
    forceOffline = true;
  }
  if (digitalRead(DS02) == LOW) {
    Serial.print("Dip Switch #2 ON");
    Serial.println(" - OTA mode (NYI)");
  }
  if (digitalRead(DS03) == LOW) {
    Serial.print("Dip Switch #3 ON");
    Serial.println(" - Silent mode");
  }
  if (digitalRead(DS04) == LOW) {
    Serial.print("Dip Switch #4 ON");
    Serial.println(" - Garage mode");
  }

  // Should load default config if run for the first time
  Serial.println(F("Loading configuration..."));
  loadFSJSON(config_filename, config);

  if (forceOffline == false) {
    connectWifi();
    startWebServer();
  }
    
  ob_pixel.begin(); //Setting onboard neopixel throws RMT errors for some reason
  pixel.begin();

  // instantiate listeners and initialize Wiegand reader, configure pins
  wiegand.onReceive(receivedData, "Card read: ");
  wiegand.onReceiveError(receivedDataError, "Card read error: ");
  wiegand.begin(Wiegand::LENGTH_ANY, true);
  attachInterrupt(digitalPinToInterrupt(wiegand_0_pin), pinStateChanged, CHANGE);
  attachInterrupt(digitalPinToInterrupt(wiegand_1_pin), pinStateChanged, CHANGE);
  pinStateChanged();
  ledcAttachChannel(buzzer_pin, freq, resolution, channel);
  setOBPixAmber();
  setPixBlue();
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
      maintainConnnectionMQTT();
      checkMqtt();
      disconCount = 0;
    } else {
      if (disconCount > 100000) {
        disconCount = 0;
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
  
  //Offline Functions
  checkKey();
  checkMagSensor();
  checkSerialCmd();
  checkExit();
  checkAUX();
  checkBell();
  //checkWg();
  watchdogCount = 0;
  delay(10);
}
