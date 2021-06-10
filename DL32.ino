/*
  DL32 v1.1 by Mark Booth
  For use with DL32 hardware revision 1.1
  Last updated 10/06/2021
*/

// Include Libraries
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wiegand.h>
#include <NeoPixelBus.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include "FS.h"
#include "SPIFFS.h"
#include <SD.h>

//1.0 Pins - Uncomment if using board revision 1.0
//#define buzzer 5
//#define bellButton 18
//#define lockRelay 23
//#define exitButton 19
//#define progButton 12
//#define wiegand0 25
//#define wiegand1 26
//#define neopin 21
//#define NUMPIXELS 1

//1.1 Pins - Uncomment if using board revision 1.1
#define buzzer 14
#define bellButton 33
#define lockRelay 27
#define exitButton 32
#define progButton 12
#define wiegand0 25
#define wiegand1 26
#define neopin 21
#define magSensor 22

//Durations for each unlock type (eg: unlock for 10 seconds if unclocked via MQTT)
#define exitButDur 5
#define httpDur 5
#define tagDur 10
#define mqttDur 10
#define addKeyDur 10
#define unrecognizedKeyDur 5

//Number of neopixels used
#define NUMPIXELS 1

//debug
#define codeVersion 20210610
#define verboseMode 0

//Hardcoded MQTT data (To be moved to JSON in SPIFFS)
const char* mqtt_server = "192.168.1.20"; // mqtt server IP address
const char* mqtt_cmnd_topic = "cmnd/lock32"; // topic for commands
const char* mqtt_stat_topic = "stat/lock32"; // topic for status
const char* mqtt_client_name = "lock32_client"; // mqtt client name

long lastMQTTReconnectAttempt = 0;
long lastMsg = 0;
long disconCount = 0;
char msg[50];
int value = 0;
int idlcol = 4;
String pageContent = "";
boolean SPIFFS_present = false;
boolean SD_present = false;
boolean doorOpen = false;
boolean doorSensorConnected = false;

NeoPixelBus<NeoRgbFeature, Neo800KbpsMethod> APA106(NUMPIXELS, neopin);
RgbColor red(16, 0, 0);
RgbColor green(0, 16, 0);
RgbColor blue(0, 0, 16);
RgbColor amber(12, 8, 0);
RgbColor purple(8, 0, 8);
RgbColor yellow (8, 8, 0);
RgbColor black(0);

HslColor hslRed(red);
HslColor hslGreen(green);
HslColor hslBlue(blue);
HslColor hslAmber(amber);
HslColor hslPurple(purple);
HslColor hslYellow(yellow);
HslColor hslBlack(black);

WiFiClient esp32Client;
PubSubClient MQTTclient(esp32Client);
WIEGAND wg;
WebServer DL32server(80);

// --- Wifi Functions --- Wifi Functions --- Wifi Functions --- Wifi Functions --- Wifi Functions --- Wifi Functions ---

int connectWifi() {
  if (!loadFSJSON()) {
    Serial.println("Failed to load config");
  } else {
    Serial.println("Config loaded");

    // Connect to WiFi network using details stored on SPIFFS
    File configFile = SPIFFS.open("/DL32.json", "r");

    size_t size = configFile.size();

    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& json = jsonBuffer.parseObject(buf.get());

    const char* FS_ssid = json["ssid"];
    const char* FS_wifi_password = json["wifi_password"];

    configFile.close();

    Serial.print("Connecting to ");
    Serial.println(FS_ssid);
    //Serial.print("with password ");
    //Serial.println(FS_wifi_password);

    WiFi.begin(FS_ssid, FS_wifi_password);

    int retries = 0;
    while ((WiFi.status() != WL_CONNECTED) && (retries < 10)) {
      retries++;
      delay(500);
      Serial.print(F("."));
    }
  }
  Serial.println("");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Connected to WIFI SSID "));
    Serial.print(WiFi.SSID());
    Serial.print(F(" with IP: "));
    Serial.println(WiFi.localIP());
    idlcol = 3;
    startWebServer();
    startMQTTConnection();
  }
  else {
    Serial.println(F("Not connected to Network."));
    idlcol = 4;
    setLEDColor(idlcol);
  }
}

// --- LED Functions --- LED Functions --- LED Functions --- LED Functions --- LED Functions --- LED Functions ---

// 1=Red, 2=Green, 3=Blue, 4=Amber, 5=Purple else=Off
void setLEDColor(int colour) {
  if (colour == 1) {
    APA106.SetPixelColor(0, hslRed);
    APA106.Show();
    //Serial.println("LED Red");
    return;
  } else if (colour == 2) {
    APA106.SetPixelColor(0, hslGreen);
    APA106.Show();
    //Serial.println("LED Green");
    return;
  } else if (colour == 3) {
    APA106.SetPixelColor(0, hslBlue);
    APA106.Show();
    //Serial.println("LED Blue");
    return;
  } else if (colour == 4) {
    APA106.SetPixelColor(0, hslAmber);
    APA106.Show();
    //Serial.println("LED Amber");
    return;
  } else if (colour == 5) {
    APA106.SetPixelColor(0, hslPurple);
    APA106.Show();
    //Serial.println("LED Purple");
    return;
  } else if (colour == 6) {
    APA106.SetPixelColor(0, hslYellow);
    APA106.Show();
    //Serial.println("LED Yellow");
    return;
  } else {
    //Serial.println("LED Off");
    return;
  }
}

// --- SPIFFS Functions --- SPIFFS Functions --- SPIFFS Functions --- SPIFFS Functions --- SPIFFS Functions --- SPIFFS Functions ---

bool loadFSJSON() {
  File configFile = SPIFFS.open("/DL32.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file is too large");
    return false;
  }

  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  if (json.success()) {
    Serial.println("SPIFFS JSON successfully parsed");
    configFile.close();
    return true;
  }
  else {
    Serial.println("SPIFFS JSON parse FAILED!");
    configFile.close();
    return false;
  }
  return true;
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  File name: ");
      Serial.print(file.name());
      Serial.print(" File size: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
  Serial.println("");
}

void configSDtoSPIFFS() {
  if ((SD_present == true) && (SD.exists("/DL32.json"))) {
    File sourceFile = SD.open("/DL32.json");
    File destFile = SPIFFS.open("/DL32.json", FILE_WRITE);
    static uint8_t buf[1];
    while ( sourceFile.read( buf, 1) ) {
      destFile.write( buf, 1 );
    }
    destFile.close();
    sourceFile.close();
  } else {
    Serial.println("");
    Serial.println("No SD Card Mounted or no such file");
  }
}

void addressingSDtoSPIFFS() {
  if ((SD_present == true) && (SD.exists("/addressing.json"))) {
    File sourceFile = SD.open("/addressing.json");
    File destFile = SPIFFS.open("/addressing.json", FILE_WRITE);
    static uint8_t buf[1];
    while ( sourceFile.read( buf, 1) ) {
      destFile.write( buf, 1 );
    }
    destFile.close();
    sourceFile.close();
  } else {
    Serial.println("");
    Serial.println("No SD Card Mounted or no such file");
  }
}

void keysSDtoSPIFFS() {
  if ((SD_present == true) && (SD.exists("/keys.txt"))) {
    File sourceFile = SD.open("/keys.txt");
    File destFile = SPIFFS.open("/keys.txt", FILE_WRITE);
    static uint8_t buf[1];
    while ( sourceFile.read( buf, 1) ) {
      destFile.write( buf, 1 );
    }
    destFile.close();
    sourceFile.close();
  } else {
    Serial.println("");
    Serial.println("No SD Card Mounted or no such file");
  }
}

void keysSPIFFStoSD() {
  if ((SD_present == true) && (SPIFFS.exists("/keys.txt"))){
    File sourceFile = SPIFFS.open("/keys.txt");
    File destFile = SD.open("/keys.txt", FILE_WRITE);
    static uint8_t buf[1];
    while ( sourceFile.read( buf, 1) ) {
      destFile.write( buf, 1 );
    }
    destFile.close();
    sourceFile.close();
  } else {
    Serial.println("");
    Serial.println("No SD Card Mounted or no such file");
  }
}

void appendFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Appending to file: %s\r\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("- failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("- message appended");
  } else {
    Serial.println("- append failed");
  }
}

void deleteFile(fs::FS &fs, const char * path) {
  Serial.printf("Deleting file: %s\r\n", path);
  if (fs.remove(path)) {
    Serial.println("- file deleted");
  } else {
    Serial.println("- delete failed");
  }
}

//Function to add new key to allowed list
void addKeyMode() {
  Serial.println("");
  Serial.println("Add Key mode - Waiting for key");
  setLEDColor(5);
  int count = 0;
  unsigned long code;
  uint8_t wait = 10;
  while (count < (addKeyDur * 100)) {
    if (wg.available()) {
      code = wg.getCode();
      Serial.println("");
      Serial.print("CARD UID: ");
      Serial.print(wg.getCode());
      Serial.print(", Type W");
      Serial.println(wg.getWiegandType());
      File keysFile = SPIFFS.open("/keys.txt", "a");
      if (!keysFile) {
        Serial.println("Failed to open keys.txt");
        return;
      }
      keysFile.println(code);
      keysFile.close();
      //appendFile(SPIFFS, "/keys.txt", "\r\n");
      Serial.print("Card added.");
      Serial.println(code);
      count = 1000;
    }
    count++;
    delay(10);
  }
  Serial.println("Reading file: keys.txt");
  File outFile = SPIFFS.open("/keys.txt");
  while (outFile.available()) {
    Serial.write(outFile.read());
  }
  setLEDColor(3);
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}

// --- Lock Functions --- Lock Functions --- Lock Functions --- Lock Functions --- Lock Functions --- Lock Functions ---

// Unlock the doorstrike for 'int' seconds
void unlock(int secs) {
  File configFile = SPIFFS.open("/DL32.json", "r");
  size_t size = configFile.size();
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  const char* mqtt_stat_topic = json["mqtt_stat_topic"];
  configFile.close();
  int loops = 0;
  int count = 0;
  Serial.print("Unlock - ");
  Serial.print(secs);
  Serial.println(" Seconds");
  digitalWrite(lockRelay, HIGH);
  MQTTclient.publish(mqtt_stat_topic, "unlocked");
  setLEDColor(2);
  unlockBeep();
  //If "secs" is zero, leave unlocked indefinitely
  if (secs == 0) {
    persistUnlockBeep();
    Serial.println("Unlocked indefinateley");
    return;
  }
  while (loops < secs) {
    if (digitalRead(exitButton) == LOW) {
      count++;
      Serial.println("Button being held");
    }
    delay(1000);
    loops++;
  }
  Serial.println("Lock");
  digitalWrite(lockRelay, LOW);
  MQTTclient.publish(mqtt_stat_topic, "locked");
  setLEDColor(idlcol);
  if (count == exitButDur) {
    addBeep();
    addKeyMode();
    shortBeep();
  }
}
//Check magnetic sensor for open/closed door state (Optional)
void checkMagSensor() {
  if (doorSensorConnected == true) {
    if (doorOpen == true && digitalRead(magSensor) == LOW) {
      //send not that door has closed
      doorOpen = false;
      Serial.println("Door Opened");
      idlcol = 6;
    } else if (doorOpen == false && digitalRead(magSensor) == HIGH) {
      doorOpen = true;
      Serial.println("Door Closed");
      idlcol = 3;
    }
  }
}

// --- Button Functions --- Button Functions --- Button Functions --- Button Functions --- Button Functions --- Button Functions ---

//Unlock button is pressed - Unlock for 5sec, enter programming mode if held for 30sec
int checkExit() {
  if (digitalRead(exitButton) == LOW) {
    File configFile = SPIFFS.open("/DL32.json", "r");
    size_t size = configFile.size();
    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& json = jsonBuffer.parseObject(buf.get());
    const char* mqtt_stat_topic = json["mqtt_stat_topic"];
    configFile.close();
    Serial.println("");
    Serial.println("Button Pressed");
    MQTTclient.publish(mqtt_stat_topic, "button pressed");
    unlock(exitButDur);
    return 1;
  }
}

//Programming button
//  - Held for 5sec from boot - copy config/keys from SD to SPIFFS
//  - Held for 10sec from boot - Purge all keys from keys.txt
void checkProgBoot() {
  int count = 0;
  while (digitalRead(progButton) == LOW) {
    if (count == 0) {
      Serial.println("");
      Serial.print("Program Button - ");
    }
    else if (count == 5) {
      addBeep();
    }
    else if (count > 10) {
      Serial.println("KEY PURGE INITIATED");
      purgeBeep();
      deleteFile(SPIFFS, "/keys.txt");
      delay(2000);
      writeFile(SPIFFS, "/keys.txt", "");
      Serial.println("KEY PURGE COMPLETED");
      return;
    }
    delay(1000);
    count++;
  }
  if (count >= 5) {
    Serial.println("Copying configuation file and keys from SD to SPIFFS");
    Serial.println("");
    configSDtoSPIFFS();
    keysSDtoSPIFFS();
    Serial.println("Restarting...");
    ESP.restart();
    return;
  }
}

// While running, hold bottom button for 5sec to add tag
void checkAdd() {
  int count = 0;
  while (digitalRead(progButton) == LOW) {
    if (count == 5) {
      addBeep();
    }
    delay(1000);
    count++;
  }
  if (count >= 5) {
    addKeyMode();
    return;
  }
}

// Bell button is pressed - Play Tone
void checkBell() {
  if (digitalRead(bellButton) == LOW) {
    File configFile = SPIFFS.open("/DL32.json", "r");
    size_t size = configFile.size();
    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& json = jsonBuffer.parseObject(buf.get());
    const char* mqtt_stat_topic = json["mqtt_stat_topic"];
    configFile.close();
    Serial.println("");
    Serial.print("Bell Pressed - ");
    MQTTclient.publish(mqtt_stat_topic, "bell pressed");
    bellBeep();
  }
}

//Check if the programming button (at bottom of unit) is being pressed.
void checkProg() {
  if (digitalRead(progButton) == LOW) {
    Serial.println("");
    Serial.println("Prog Pressed");
    beepIP();
  }
}

// Used for listening for exit events during delay
int exitListenDelay(int milisecs) {
  int count = 0;
  while (count < milisecs) {
    if (checkExit() == 1) {
      return 1;
    }
    delay(milisecs);
    count++;
  }
  return 0;
}

// --- RFID Functions --- RFID Functions --- RFID Functions --- RFID Functions --- RFID Functions --- RFID Functions ---

// Check for presence of RFID card
void checkCard() {
  int matchedCards = 0;
  if (wg.available()) {
    unsigned long cardCode;
    cardCode = wg.getCode();
    if (cardCode < 10) {
      return;      
    }
    Serial.println("");
    Serial.print("CARD UID: ");
    Serial.print(wg.getCode());
    Serial.print(", Type W");
    Serial.println(wg.getWiegandType());
    char tagBuffer[11];
    char entryBuffer[11];
    sprintf(entryBuffer, "%ld", cardCode);

    File keysFile = SPIFFS.open("/keys.txt", "r");
    int charMatches = 0;
    while (keysFile.available()) {
      int cardDigits = keysFile.readBytesUntil('\n', tagBuffer, sizeof(tagBuffer));
      tagBuffer[cardDigits] = 0;
      charMatches = 0;
      for (int loopCount = 0; loopCount < sizeof(tagBuffer); loopCount++) {
        if (entryBuffer[loopCount] == tagBuffer[loopCount]) {
          charMatches++;
        }
      }
      if (charMatches == cardDigits - 1) {
        Serial.print(tagBuffer);
        Serial.print(" - ");
        Serial.println("MATCH");
        matchedCards++;
      } else {
        Serial.print(tagBuffer);
        Serial.print(" - ");
        Serial.println("NO MATCH");
      }
    }
    keysFile.close();
    if (matchedCards > 0) {
      unlock(tagDur);
    } else {
      invalidTag();
    }
  }
}

// Tag is unrecognised - Red LED, 4x beeps
void invalidTag() {
  Serial.println("Invalid Tag Detected!");
  digitalWrite(lockRelay, LOW);
  setLEDColor(1);
  deniedBeep();
  delay(unrecognizedKeyDur * 1000);
  setLEDColor(idlcol);
  Serial.println("");
}

// --- Buzzer Functions --- Buzzer Functions --- Buzzer Functions --- Buzzer Functions --- Buzzer Functions --- Buzzer Functions ---

// Play long beep
void longBeep() {
  digitalWrite(buzzer, HIGH);
  delay(1500);
  digitalWrite(buzzer, LOW);
}

// Play short beep
void shortBeep() {
  digitalWrite(buzzer, HIGH);
  delay(250);
  digitalWrite(buzzer, LOW);
}

// Play unlock tone
void unlockBeep() {
  digitalWrite(buzzer, HIGH);
  delay(100);
  digitalWrite(buzzer, LOW);
  delay(100);
  digitalWrite(buzzer, HIGH);
  delay(100);
  digitalWrite(buzzer, LOW);
}

// Play unlock tone
void persistUnlockBeep() {
  digitalWrite(buzzer, HIGH);
  delay(1000);
  digitalWrite(buzzer, LOW);
}

// Play Doorbell tone
void bellBeep() {
  Serial.println("Ringing");
  for (int i = 0; i <= 4; i++) {
    APA106.SetPixelColor(0, hslYellow);
    APA106.Show();
    digitalWrite(buzzer, HIGH);
    delay(50);
    digitalWrite(buzzer, LOW);
    delay(20);
    digitalWrite(buzzer, HIGH);
    delay(40);
    digitalWrite(buzzer, LOW);
    delay(20);
    digitalWrite(buzzer, HIGH);
    delay(40);
    digitalWrite(buzzer, LOW);
    delay(20);
    digitalWrite(buzzer, HIGH);
    delay(40);
    digitalWrite(buzzer, LOW);
    delay(20);
    digitalWrite(buzzer, HIGH);
    delay(40);
    digitalWrite(buzzer, LOW);
    setLEDColor(idlcol);

    if (exitListenDelay(15) == 1) {
      break;
    }

    APA106.SetPixelColor(0, hslYellow);
    APA106.Show();
    digitalWrite(buzzer, HIGH);
    delay(50);
    digitalWrite(buzzer, LOW);
    delay(20);
    digitalWrite(buzzer, HIGH);
    delay(40);
    digitalWrite(buzzer, LOW);
    delay(20);
    digitalWrite(buzzer, HIGH);
    delay(40);
    digitalWrite(buzzer, LOW);
    delay(20);
    digitalWrite(buzzer, HIGH);
    delay(40);
    digitalWrite(buzzer, LOW);
    delay(20);
    digitalWrite(buzzer, HIGH);
    delay(40);
    digitalWrite(buzzer, LOW);
    setLEDColor(idlcol);
    if (exitListenDelay(50) == 1) {
      break;
    }
  }
}

//beep the IP Address
void beepIP() {
  if (WiFi.status() == WL_CONNECTED) {
    File configFile = SPIFFS.open("/DL32.json", "r");
    size_t size = configFile.size();
    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& json = jsonBuffer.parseObject(buf.get());
    const char* mqtt_stat_topic = json["mqtt_stat_topic"];
    configFile.close();
    MQTTclient.publish(mqtt_stat_topic, "Outputting IP Address: ");
    MQTTclient.publish(mqtt_stat_topic, WiFi.localIP().toString().c_str());
    Serial.print("Outputting IP Address ");
    Serial.print(WiFi.localIP());
    Serial.println(" via beep patterns");
    delay(1000);
    unsigned ip = WiFi.localIP()[3];
    unsigned h = (ip / 100U) % 10;
    unsigned t = (ip / 10U) % 10;
    unsigned o = (ip / 1U) % 10;
    int count = 0;
    int octet = 0;
    while (octet < 4) {
      APA106.SetPixelColor(0, hslYellow);
      APA106.Show();
      Serial.println("LED Yellow");
      ip = WiFi.localIP()[octet];
      h = (ip / 100U) % 10;
      t = (ip / 10U) % 10;
      o = (ip / 1U) % 10;
      count = 0;
      Serial.print("Beeping out ");
      if (h > 0) {
        Serial.print(h);
      }
      while (count < h) {
        digitalWrite(buzzer, HIGH);
        Serial.print(".");
        delay(10);
        digitalWrite(buzzer, LOW);
        delay(200);
        count++;
      }
      if (h > 0) {
        delay(2500);
      }
      if (t > 0 || h > 0) {
        Serial.print(t);
      }
      count = 0;
      while (count < t) {
        digitalWrite(buzzer, HIGH);
        Serial.print(".");
        delay(10);
        digitalWrite(buzzer, LOW);
        delay(200);
        count++;
      }
      if (t > 0 || h > 0) {
        delay(2500);
      }
      Serial.print(o);
      count = 0;
      while (count < o) {
        digitalWrite(buzzer, HIGH);
        Serial.print(".");
        delay(10);
        digitalWrite(buzzer, LOW);
        delay(200);
        count++;
      }
      if (o > 0) {
        delay(1000);
        APA106.SetPixelColor(0, hslGreen);
        APA106.Show();
        Serial.println("LED Green");
        delay(4000);
      }
      Serial.println("");
      octet++;
    }
    setLEDColor(idlcol);
  } else {
    Serial.println("Not connected to Wifi - Cannot beep IP");
  }
}

// Play denied tone
void deniedBeep() {
  digitalWrite(buzzer, HIGH);
  delay(300);
  digitalWrite(buzzer, LOW);
  delay(100);
  digitalWrite(buzzer, HIGH);
  delay(300);
  digitalWrite(buzzer, LOW);
  delay(100);
  digitalWrite(buzzer, HIGH);
  delay(300);
  digitalWrite(buzzer, LOW);
  delay(100);
  digitalWrite(buzzer, HIGH);
  delay(300);
  digitalWrite(buzzer, LOW);
}

// Play add tone
void addBeep() {
  digitalWrite(buzzer, HIGH);
  delay(300);
  digitalWrite(buzzer, LOW);
  delay(100);
  digitalWrite(buzzer, HIGH);
  delay(300);
  digitalWrite(buzzer, LOW);
}

// Play purge tone
void purgeBeep() {
  digitalWrite(buzzer, HIGH);
  setLEDColor(1);
  delay(100);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(1000);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(100);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(800);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(100);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(500);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(100);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(300);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(100);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(200);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(100);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(100);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(100);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(50);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(50);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, LOW);
  APA106.SetPixelColor(0, hslBlue);
  APA106.Show();
  delay(20);
  digitalWrite(buzzer, HIGH);
  APA106.SetPixelColor(0, hslRed);
  APA106.Show();
  delay(3000);
  digitalWrite(buzzer, LOW);
  setLEDColor(idlcol);
}

// --- MQTT Functions --- MQTT Functions --- MQTT Functions --- MQTT Functions --- MQTT Functions --- MQTT Functions ---

// Initiate connection to MQTT Broker
void startMQTTConnection() {
  int mqtt_port = 0;
  File configFile = SPIFFS.open("/DL32.json", "r");
  size_t size = configFile.size();
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  mqtt_port = json["mqtt_port"];
  const char* mqtt_server = json["mqtt_server"];
  configFile.close();
  MQTTclient.setServer("192.168.1.20", 1883);
  MQTTclient.setCallback(MQTTcallback);
  delay(100);
  lastMQTTReconnectAttempt = 0;
}

//Reconnect to MQTT broker if disconnected
boolean mqttReconnect() {
  File configFile = SPIFFS.open("/DL32.json", "r");
  size_t size = configFile.size();
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  const char* mqtt_client_name = json["mqtt_client_name"];
  const char* mqtt_cmnd_topic = json["mqtt_cmnd_topic"];
  const char* mqtt_stat_topic = json["mqtt_stat_topic"];
  const char* mqtt_username = json["mqtt_username"];
  const char* mqtt_password = json["mqtt_password"];
  configFile.close();
  //MQTT CREDS HARDCODED - TO BE UPDATED TO USE CONFIG FILE AT LATER POINT
  if (MQTTclient.connect(mqtt_client_name, mqtt_username, mqtt_password)) {
    Serial.print("Connected to MQTT Broker as client ");
    Serial.println(mqtt_client_name);
    MQTTclient.publish(mqtt_stat_topic, "Connected to MQTT Broker");
    MQTTclient.subscribe(mqtt_cmnd_topic);
  } else {
    idlcol = 4;
  }
  return MQTTclient.connected();
}

void MQTTcallback(char* topic, byte* payload, unsigned int length) {
  File configFile = SPIFFS.open("/DL32.json", "r");
  size_t size = configFile.size();
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  const char* mqtt_stat_topic = json["mqtt_stat_topic"];
  configFile.close();
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
    MQTTclient.publish(mqtt_stat_topic, "lock opened via MQTT");
    unlock(mqttDur);
  } else if ((char)payload[0] == 'b') {
    Serial.println("Bell via MQTT");
    MQTTclient.publish(mqtt_stat_topic, "Bell rung via MQTT");
    bellBeep();
  } else if ((char)payload[0] == 'p') {
    Serial.println("Unlocked persistently via MQTT");
    MQTTclient.publish(mqtt_stat_topic, "lock opened persistently via MQTT");
    unlock(0);
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

//Check MQTT for new messages
void checkMQTT() {
  if (MQTTclient.connected()) {
    MQTTclient.loop();
    long now = millis();
    if (now - lastMsg > 2000) {
      lastMsg = now;
      ++value;
    }
  }
}

// --- Web Functions --- Web Functions --- Web Functions --- Web Functions --- Web Functions --- Web Functions ---

void handleRoot() {
  DL32server.send(200, "text/plain", "hello from ESP32!");
  Serial.print("Message arrived TOPIC:[");
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += DL32server.uri();
  message += "\nMethod: ";
  message += (DL32server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += DL32server.args();
  message += "\n";
  for (uint8_t i = 0; i < DL32server.args(); i++) {
    message += " " + DL32server.argName(i) + ": " + DL32server.arg(i) + "\n";
  }
  DL32server.send(404, "text/plain", message);
}

// Download list of allowed keys (keys.txt)
void DownloadKeysHTTP() {
  SPIFFS_file_download("keys.txt");
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Downloading keys file keys.txt</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

// Download copy of config file (config.json)
void DownloadConfigHTTP() {
  SPIFFS_file_download("DL32.json");
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Downloading config file DL32.json</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

// Output a list of allowed keys to serial
void OutputKeys() {
  Serial.println("Reading file: keys.txt");
  File outFile = SPIFFS.open("/keys.txt");
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
  File dispFile = SPIFFS.open("/keys.txt");
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
  Serial.println("Reading file: DL32.json");
  File outFile = SPIFFS.open("/DL32.json");
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
  File dispFile = SPIFFS.open("/DL32.json");
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
  File configFile = SPIFFS.open("/DL32.json", "r");
  size_t size = configFile.size();
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  const char* mqtt_stat_topic = json["mqtt_stat_topic"];
  configFile.close();
  Serial.println("Unlocked via HTTP");
  MQTTclient.publish(mqtt_stat_topic, "HTTP Unlock");
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Door Unlocked</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
  unlock(httpDur);
}

void configSDtoSPIFFSHTTP() {
  configSDtoSPIFFS();
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Copied configuration from SD to SPIFFS</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void keysSDtoSPIFFSHTTP() {
  keysSDtoSPIFFS();
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Copied keys from SD to SPIFFS</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void purgeKeysHTTP() {
  deleteFile(SPIFFS, "/keys.txt");
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Keys Purged</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void purgeConfigHTTP() {
  deleteFile(SPIFFS, "/DL32.json");
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Config Purged</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void SPIFFS_file_download(String filename) {
  if (SPIFFS_present) {
    File download = SPIFFS.open("/" + filename);
    if (download) {
      DL32server.sendHeader("Content-Type", "text/text");
      DL32server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
      DL32server.sendHeader("Connection", "close");
      DL32server.streamFile(download, "application/octet-stream");
      download.close();
    }
  }
}

void RingBellHTTP() {
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Ringing bell</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
  bellBeep();
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

}

void downloadAddressingStaticHTTP() {
  SPIFFS_file_download("addressing.json");
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Downloading static addressing file addressing.json</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void addressingStaticSDtoSPIFFSHTTP() {
  addressingSDtoSPIFFS();
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>Copied static addressing from SD to SPIFFS</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}

void purgeAddressingStaticHTTP() {
  deleteFile(SPIFFS, "/addressing.json");
  SendHTML_Header();
  siteButtons();
  pageContent += F("<br/> <textarea readonly>static addressing purged</textarea>");
  siteFooter();
  SendHTML_Content();
  SendHTML_Stop();
}


// Output a list of FS files to serial
void OutputFSHTTP() {
  listDir(SPIFFS, "/", 0);
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
  DL32server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  DL32server.sendHeader("Pragma", "no-cache");
  DL32server.sendHeader("Expires", "-1");
  DL32server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  DL32server.send(200, "text/html", "");
  siteHeader();
  DL32server.sendContent(pageContent);
  pageContent = "";
}

void SendHTML_Content() {
  DL32server.sendContent(pageContent);
  pageContent = "";
}

void SendHTML_Stop() {
  DL32server.sendContent("");
  DL32server.client().stop();
}

void siteHeader() {
  pageContent  = F("<!DOCTYPE html>");
  pageContent += F("<html>");
  pageContent += F("<head>");
  pageContent += F("<style>");

  //Green Theme
  //  pageContent += F("div {width: 350px; margin: 20px auto; text-align: center; border: 3px solid #C2F444; background-color: #555555; left: auto; right: auto;}");
  //  pageContent += F(".header {font-family: Arial, Helvetica, sans-serif; font-size: 20px; color: #C2F444;}");
  //  pageContent += F("button {width: 300px; background-color: #C2F444; border: none; text-decoration: none;}");
  //  pageContent += F("button:hover {width: 300px; background-color: #B2E434; border: none; text-decoration: none;}");
  //  pageContent += F("h1 {font-family: Arial, Helvetica, sans-serif; color: #C2F444;}");
  //  pageContent += F("h3 {font-family: Arial, Helvetica, sans-serif; color: #C2F444;}");
  //  pageContent += F("a {font-family: Arial, Helvetica, sans-serif; font-size: 10px; color: #C2F444;}");
  //  pageContent += F("textarea {background-color: #303030; font-size: 11px; width: 300px; height: 150px; resize: vertical; color: #C2F444;}");
  //  pageContent += F("body {background-color: #303030; text-align: center;}");

  //Blue Theme
  //  pageContent += F("div {width: 350px; margin: 20px auto; text-align: center; border: 3px solid #4286f4; background-color: #555555; left: auto; right: auto;}");
  //  pageContent += F(".header {font-family: Arial, Helvetica, sans-serif; font-size: 20px; color: #4286f4;}");
  //  pageContent += F("button {width: 300px; background-color: #4286f4; border: none; text-decoration: none;}");
  //  pageContent += F("button:hover {width: 300px; background-color: #3276e4; border: none; text-decoration: none;}");
  //  pageContent += F("h1 {font-family: Arial, Helvetica, sans-serif; color: #4286f4;}");
  //  pageContent += F("h3 {font-family: Arial, Helvetica, sans-serif; color: #4286f4;}");
  //  pageContent += F("a {font-family: Arial, Helvetica, sans-serif; font-size: 10px; color: #4286f4;}");
  //  pageContent += F("textarea {background-color: #303030; font-size: 11px; width: 300px; height: 150px; resize: vertical; color: #4286f4;}");
  //  pageContent += F("body {background-color: #303030; text-align: center;}");

  //Orange Theme
  pageContent += F("div {width: 350px; margin: 20px auto; text-align: center; border: 3px solid #ff3200; background-color: #555555; left: auto; right: auto;}");
  pageContent += F(".header {font-family: Arial, Helvetica, sans-serif; font-size: 20px; color: #ff3200;}");
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
  pageContent += F("<a class='header'>Device Control<a/>");
  pageContent += F("<a href='/UnlockHTTP'><button>HTTP Unlock</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/RingBellHTTP'><button>Ring Bell</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/restartESPHTTP'><button>Restart DL32</button></a>");
  pageContent += F("<br/> <br/>");
  pageContent += F("<a class='header'>Key List<a/>");
  pageContent += F("<a href='/DownloadKeysHTTP'><button>Download key file</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/OutputKeys'><button>Output keys to Serial</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/DisplayKeys'><button>Display keys in page</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/keysSDtoSPIFFSHTTP'><button>Upload keys SD to DL32</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/purgeKeysHTTP'><button>Purge stored keys</button></a>");
  pageContent += F("<br/> <br/>");
  pageContent += F("<a class='header'>Config File<a/>");
  pageContent += F("<a href='/DownloadConfigHTTP'><button>Download config file</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/OutputConfig'><button>Output config to Serial</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/DisplayConfig'><button>Display config in page</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/configSDtoSPIFFSHTTP'><button>Upload config SD to DL32</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/purgeConfigHTTP'><button>Purge configuration</button></a>");
  pageContent += F("<br/> <br/>");
  pageContent += F("<a class='header'>Filesystem Operations<a/>");
  pageContent += F("<a href='/OutputFSHTTP'><button>Output SPIFFS Contents to Serial</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/DisplayFSHTTP'><button style='background-color: #999999; color: #777777';>Display SPIFFS contents in page</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/OutputSDFSHTTP'><button>Output SD FS Contents to Serial</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/DisplaySDFSHTTP'><button style='background-color: #999999; color: #777777';>Display SD FS contents in page</button></a>");
  pageContent += F("<br/><br/>");
  pageContent += F("<a class='header'>IP Addressing<a/>");
  pageContent += F("<a href='/displayAddressingHTTP'><button style='background-color: #999999; color: #777777';>Show IP addressing</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/outputAddressingHTTP'><button>Output IP addressing to Serial</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/saveAddressingStaticHTTP'><button style='background-color: #999999; color: #777777';>Save current addressing as static</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/downloadAddressingStaticHTTP'><button>Download static addressing file</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/addressingStaticSDtoSPIFFSHTTP'><button>Upload static addressing SD to DL32</button></a>");
  pageContent += F("<br/>");
  pageContent += F("<a href='/purgeAddressingStaticHTTP'><button>Purge static addressing</button></a>");
  pageContent += F("<br/>");
}

void siteFooter() {
  pageContent += F(" <br/><br/> </div>");
  pageContent += F("</body></html>");
}

void startWebServer() {
  DL32server.on("/DownloadKeysHTTP", DownloadKeysHTTP);
  DL32server.on("/DisplayKeys", DisplayKeys);
  DL32server.on("/OutputKeys", OutputKeys);
  DL32server.on("/UnlockHTTP", UnlockHTTP);
  DL32server.on("/RingBellHTTP", RingBellHTTP);
  DL32server.on("/DownloadConfigHTTP", DownloadConfigHTTP);
  DL32server.on("/purgeKeysHTTP", purgeKeysHTTP);
  DL32server.on("/DisplayConfig", DisplayConfig);
  DL32server.on("/OutputConfig", OutputConfig);
  DL32server.on("/OutputFSHTTP", OutputFSHTTP);
  DL32server.on("/DisplayFSHTTP", DisplayFSHTTP);
  DL32server.on("/OutputSDFSHTTP", OutputSDFSHTTP);
  DL32server.on("/DisplaySDFSHTTP", DisplaySDFSHTTP);
  DL32server.on("/purgeConfigHTTP", purgeConfigHTTP);
  DL32server.on("/restartESPHTTP", restartESPHTTP);
  DL32server.on("/configSDtoSPIFFSHTTP", configSDtoSPIFFSHTTP);
  DL32server.on("/keysSDtoSPIFFSHTTP", keysSDtoSPIFFSHTTP);
  DL32server.on("/displayAddressingHTTP", displayAddressingHTTP);
  DL32server.on("/outputAddressingHTTP", outputAddressingHTTP);
  DL32server.on("/saveAddressingStaticHTTP", saveAddressingStaticHTTP);
  DL32server.on("/downloadAddressingStaticHTTP", downloadAddressingStaticHTTP);
  DL32server.on("/addressingStaticSDtoSPIFFSHTTP", addressingStaticSDtoSPIFFSHTTP);
  DL32server.on("/purgeAddressingStaticHTTP", purgeAddressingStaticHTTP);
  DL32server.on("/", MainPage);
  DL32server.begin();
  Serial.print("Web Server started at http://");
  Serial.println(WiFi.localIP());
}


// --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP --- SETUP ---

void setup() {

  delay(500);

  Serial.begin(115200);
  while (!Serial);
  Serial.println();
  Serial.print("DL32 firmware version 1.1.");
  Serial.println(codeVersion);
  Serial.println("Initializing...");
  Serial.flush();
  pinMode(buzzer, OUTPUT);
  pinMode(lockRelay, OUTPUT);
  pinMode(exitButton, INPUT_PULLUP);
  pinMode(progButton, INPUT_PULLUP);
  pinMode(bellButton, INPUT_PULLUP);
  pinMode(magSensor, INPUT_PULLUP);
  digitalWrite(buzzer, LOW);
  digitalWrite(lockRelay, LOW);
  wg.begin(wiegand0, wiegand1); // Init wiegand interface
  APA106.Begin();
  APA106.Show();
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount SPIFFS");
    Serial.println("Gate9a");
  } else {
    Serial.println("SPIFFS initialised.");
    SPIFFS_present = true;
    listDir(SPIFFS, "/", 0);
  }
  if (!SD.begin()) {
    Serial.println("Failed to mount SD");
  } else {
    Serial.println(F("SD initialised."));
    SD_present = true;
    listDir(SD, "/", 0);
  }
  checkProgBoot();
  connectWifi();
  maintainConnnectionMQTT();
}

// --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP --- LOOP ---

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (disconCount > 0) {
      Serial.println("Wifi available - Restarting");
      ESP.restart();
    }
    //Online Functions
    DL32server.handleClient();
    maintainConnnectionMQTT();
    checkMQTT();
    disconCount = 0;
  } else {
    idlcol = 4;
    if (disconCount > 10000) {
      disconCount = 0;
      Serial.println("WiFi reconnection attempt...");
      connectWifi();
    }
    else if (disconCount == 0) {
      Serial.println("Disconnected from WiFi");
    }
    disconCount++;
  }
  //Offline Functions
  checkProg();
  checkMagSensor();
  checkAdd();
  checkExit();
  checkBell();
  checkCard();
  setLEDColor(idlcol);
  delay(25);
}
