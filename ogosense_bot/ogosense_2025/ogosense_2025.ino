/*
  MIT License
Version 1.0 2018-01-22
Version 2.0 2025-03-15

Copyright (c) 2017 kaebmoo gmail com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
 * Hardware
 * Wemos D1 mini, Pro
 * Wemos SHT30 Shield use D1, D2 pin
 * Wemos Relay Shield use D6, D7
 * dot matrix LED // use D5, D7
 *
 *
 */

#define HIGH_TEMPERATURE 30.0
#define LOW_TEMPERATURE 25.0
#define HIGH_HUMIDITY 60
#define LOW_HUMIDITY 55
#define OPTIONS 1
#define COOL_MODE 1
#define MOISTURE_MODE 0


#ifdef SOILMOISTURE
  #define soilMoistureLevel 50
#endif

#ifdef ESP32
  #include <WiFi.h>
#else
  #include <ESP8266WiFi.h>         // สำหรับ ESP8266 (Wemos D1 mini pro)
  #include <ESP8266WebServer.h>
#endif
#include <WiFiManager.h>         // WiFiManager เวอร์ชัน 2.0.17
#include <ThingSpeak.h>
#include <ESP8266HTTPClient.h>   // HTTP Client สำหรับ ESP8266
#include <ArduinoJson.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <WEMOS_SHT3X.h>              // ไลบรารี SHT3x สำหรับเซนเซอร์ SHT30
#include <SPI.h>
#include <EEPROM.h>
#include <Timer.h>  //https://github.com/JChristensen/Timer
#include <Ticker.h>  //Ticker Library
// #include <ElegantOTA.h>

#include "ogosense.h"

#ifdef ESP8266
  X509List cert(TELEGRAM_CERTIFICATE_ROOT);
#endif

WiFiClient client;  // ใช้กับ thinkspeak
WiFiClientSecure clientSecure; // ใช้กับ telegram
UniversalTelegramBot bot(telegramToken, clientSecure);

String allowedChatIDs[MAX_ALLOWED_CHATIDS] = {"REDACTED", "REDACTED", "", "", ""};
int numAllowedChatIDs = 2; // จำนวน Chat IDs ที่กำหนดไว้

 #ifdef MATRIXLED
  #include <MLEDScroll.h>
  MLEDScroll matrix;
#endif

#ifdef NETPIE
  #include <MicroGear.h>
#endif


#if defined(SECONDRELAY) && !defined(MATRIXLED)
  const int RELAY1 = D7;
  const int RELAY2 = D6;
#else
  const int RELAY1 = D7;
#endif

#ifdef SLEEP
  // sleep for this many seconds
  const int sleepSeconds = 300;
#endif

const int buzzer=D5;                        // Buzzer control port, default D5
const int analogReadPin = A0;               // read for set options use R for voltage divide
const int LED = D4;                         // output for LED (toggle) buildin LED board

// ค่าตั้งต้นสำหรับควบคุม Relay (สามารถปรับผ่านคำสั่งจาก Chat)
float lowTemp  = LOW_TEMPERATURE;   // เมื่ออุณหภูมิต่ำกว่าค่านี้ให้ปิด Relay
float highTemp = HIGH_TEMPERATURE;   // เมื่ออุณหภูมิสูงกว่าค่านี้ให้เปิด Relay
float lowHumidity = LOW_HUMIDITY;
float highHumidity = HIGH_HUMIDITY;

// ค่า sensor readings
float temperature = 0.0;
float humidity    = 0.0;

float temperature_sensor_value, fTemperature;
int humidity_sensor_value;

// ===== Control Settings =====
bool AUTO = true;        // AUTO or Manual Mode ON/OFF relay, AUTO is depend on temperature, humidity; Manual is depend on command
int options = OPTIONS; // 1 ค่า default (สามารถเปลี่ยนผ่านคำสั่ง /setmode) // options : 0 = humidity only, 1 = temperature only, 2 = temperature & humidity

// โหมดสำหรับ temperature และ humidity
int COOL = COOL_MODE;    // 1 ค่า default // COOL: 1 = COOL mode , 0 = HEAT mode 
int MOISTURE = MOISTURE_MODE; // 0 ค่า default // MOISTURE: 1 = moisture mode, 0 = dehumidifier mode 
bool tempon = false;     // flag ON/OFF
bool humion = false;     // flag ON/OFF

SHT3X sht30(0x45);  // address sensor use D1, D2 pin

const long interval = 1000;
int ledState = LOW;
unsigned long previousMillis = 0;
unsigned long ota_progress_millis = 0;
const unsigned long onPeriod = 60L * 60L * 1000L;       // ON relay period minute * second * milli second
const unsigned long standbyPeriod = 300L * 1000L;       // delay start timer for relay

Timer t_relay, t_delayStart, t_checkFirmware;         // timer for ON period and delay start
Timer t_relay2, t_delayStart2;
Timer t_sendDatatoThinkSpeak;
Timer t_blink;           // สร้าง object สำหรับ timer

Ticker blinker;
Ticker t_getTelegramMessage;
Ticker t_readSensor;

volatile bool shouldReadSensor = false;
volatile bool shouldCheckTelegram = false;
bool RelayEvent = false;
int afterStart = -1;
int afterStop = -1;
/*
In this firmware:

- **afterStart** is used to store the timer ID for the "on period" of the relay. 
When the sensor conditions trigger the relay to turn on, 
the code schedules a timer (using `t_relay.after(onPeriod, turnoff)`) 
and saves its identifier in `afterStart`. This timer will eventually trigger 
the `turnoff()` function to turn off the relay after the defined period.

- **afterStop** is used to store the timer ID for the "delay period" after 
the relay is turned off. Once the relay is turned off (or when sensor conditions change), 
the code schedules another timer (using `t_delayStart.after(standbyPeriod, delayStart)`) 
and saves its identifier in `afterStop`. This ensures there is a standby interval during 
which the relay cannot be turned on immediately again, helping to prevent rapid toggling.

In summary, **afterStart** manages how long the relay stays on, 
and **afterStop** ensures a delay before the relay can be activated again.
*/

bool Relay2Event = false;
int afterStart2 = -1;
int afterStop2 = -1;

// ===== Function Prototypes =====
int readSensorData();
void controlRelay();
void sendDataToThingSpeak();
void printConfig();
void getConfig();
void handleNewMessages(int numNewMessages);
void getTelegramMessage();
void turnRelayOn();
void turnRelayOff();
void buzzer_sound();
void blink();


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  #ifdef ESP8266
    configTime(0, 0, "pool.ntp.org");         // get UTC time via NTP
    clientSecure.setTrustAnchors(&cert);      // Add root certificate for api.telegram.org
  #endif
  delay(10);
  Serial.println();
  Serial.println("Starting");

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED, OUTPUT);

  pinMode(analogReadPin, INPUT);
  pinMode(RELAY1, OUTPUT);

  pinMode(RELAY1, OUTPUT);
  digitalWrite(RELAY1, LOW);

  #ifdef SECONDRELAY
    pinMode(RELAY2, OUTPUT);
    digitalWrite(RELAY2, LOW);
  #endif

  // t_blink.every(1000, blink); // 1 second   // กระพริบ LED ทุก 1 วินาที
  blinker.attach(1, blink); //Use attach_ms if you need time in ms

  autoWifiConnect();  // ต่อ WiFi โดยใช้ WiFiManager

  getConfig();        // อ่านค่า config จาก EEPROM
  printConfig();      // แสดงค่า config ที่อ่านได้
  readSensorData();   // อ่านค่า sensor และแสดงผลลัพธ์
  buzzer_sound();     // เสียง buzzer

  #ifdef THINGSPEAK
    ThingSpeak.begin(client);
  #endif
  
  t_readSensor.attach(5, setReadSensorFlag);  // 5 seconds  // อ่านค่า sensor ทุก 5 วินาที
  t_getTelegramMessage.attach(1, checkTelegramFlag);  // อ่านข้อความจาก Telegram ทุก 1 วินาที
  t_sendDatatoThinkSpeak.every(60L * 1000L, sendDataToThingSpeak);  // ส่งข้อมูลไป ThingSpeak ทุก 1 นาที
  
}

void loop() {
  // put your main code here, to run repeatedly:
  // t_blink.update();  // flash LED

  if (shouldReadSensor) {
    shouldReadSensor = false;
    controlRelay(); // เรียกใน task context ป้องกัน core dump
  }

  // ตรวจสอบคำสั่งจาก Telegram
  if (shouldCheckTelegram) {
    shouldCheckTelegram = false;
    getTelegramMessage(); // เรียกใน context ของ loop แทนการทำงานใน interrupt context
  }

  t_relay.update();
  t_delayStart.update();

  t_relay2.update();
  t_delayStart2.update();

  t_sendDatatoThinkSpeak.update();

}

void setReadSensorFlag() {
  shouldReadSensor = true;
}

void checkTelegramFlag() {
  shouldCheckTelegram = true;
}

// Handle what happens when you receive new messages
void handleNewMessages(int numNewMessages)
{
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i=0; i<numNewMessages; i++) {
    // เรียก yield เพื่อคืน control ให้ system และป้องกัน watchdog reset
    yield();

    String chat_id = String(bot.messages[i].chat_id);
    // ตรวจสอบว่า chat_id อยู่ในรายชื่อ allowedChatIDs หรือไม่
    bool authorized = false;
    for (int j = 0; j < numAllowedChatIDs; j++) {
      if (chat_id == String(allowedChatIDs[j])) {
        authorized = true;
        break;
      }
    }
    if (!authorized) {
      Serial.println("Unauthorized chat: " + chat_id);
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }


    // Print the received message
    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;

    if (text == "/start") {
      String welcome = String(deviceName) + "\n" + "Welcome to OgoSense!, " + from_name + ".\n";
      bot.sendMessage(chat_id, welcome, "");
      continue;
    }
    // หาตำแหน่งช่องว่างแรก
    int firstSpace = text.indexOf(' ');
    if (firstSpace == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: ต้องมี /[command] <id>");
        continue;
    }

    String params = text.substring(firstSpace + 1); // ตัดส่วนหลังช่องว่างแรก เช่น "555" หรือ "555 x" หรือ "555 x y"
    int space1 = params.indexOf(' '); // หาตำแหน่งช่องว่างแรกใน params

    // ตรวจสอบ device ID
    int cmdID;
    if (space1 == -1) {
        cmdID = params.toInt(); // กรณีมีแค่ device ID เช่น "555"
    } else {
        cmdID = params.substring(0, space1).toInt(); // กรณีมีพารามิเตอร์เพิ่ม เช่น "555 x" หรือ "555 x y"
    }
    if (cmdID != DEVICE_ID) {
        // bot.sendMessage(chat_id, "Device ID ไม่ตรงกับเครื่องนี้");
        continue;
    }
    else if (text.startsWith("/status")) {        
        String statusMsg = "Device ID: " + String(DEVICE_ID) + " (" + String(deviceName) + ")\n";
        statusMsg += "🌡 Temperature: " + String(sht30.cTemp) + "°C\n";
        statusMsg += "💧 Humidity: " + String(sht30.humidity) + "%\n";
        bool relayState = (0 != (*portOutputRegister(digitalPinToPort(RELAY1)) & digitalPinToBitMask(RELAY1)));
        statusMsg += "💡 Relay: " + String(relayState ? "ON" : "OFF");
        bot.sendMessage(chat_id, statusMsg);
    }
    else if (text.startsWith("/settemp")) {
        int firstSpace = text.indexOf(' ');
        if (firstSpace == -1) {
          bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /settemp <id> <lowTemp> <highTemp>");
          continue;
        }
        String params = text.substring(firstSpace + 1);
        int space1 = params.indexOf(' ');
        int space2 = params.indexOf(' ', space1 + 1);
        if (space1 == -1 || space2 == -1) {
          bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /settemp <id> <lowTemp> <highTemp>");
          continue;
        }
        if (!isNumeric(params.substring(space1 + 1, space2)) || !isNumeric(params.substring(space2 + 1))) {
          bot.sendMessage(chat_id, "ค่าที่ป้อนต้องเป็นตัวเลข เช่น /settemp <id> <lowTemp> <highTemp>");
          continue;
        }
        float lt = params.substring(space1 + 1, space2).toFloat();
        float ht = params.substring(space2 + 1).toFloat();
        lowTemp = lt;
        highTemp = ht;
        saveConfig();
        bot.sendMessage(chat_id, String(deviceName) + ": \nตั้งค่า Temperature สำเร็จ: Low=" + String(lowTemp) +
                        "°C, High=" + String(highTemp) + "°C");
    }
    else if (text.startsWith("/sethum")) {
        int firstSpace = text.indexOf(' ');
        if (firstSpace == -1) {
            bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /sethum <id> <lowHumidity> <highHumidity>");
            continue;
        }
        String params = text.substring(firstSpace + 1);
        int space1 = params.indexOf(' ');
        int space2 = params.indexOf(' ', space1 + 1);
        if (space1 == -1 || space2 == -1) {
            bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /sethum <id> <lowHumidity> <highHumidity>");
            continue;
        }
        if (!isNumeric(params.substring(space1 + 1, space2)) || !isNumeric(params.substring(space2 + 1))) {
          bot.sendMessage(chat_id, "ค่าที่ป้อนต้องเป็นตัวเลข เช่น /sethum <id> <lowHumidity> <highHumidity>");
          continue;
        }
        float lh = params.substring(space1 + 1, space2).toFloat();
        float hh = params.substring(space2 + 1).toFloat();
        lowHumidity = lh;
        highHumidity = hh;
        saveConfig();
        bot.sendMessage(chat_id, String(deviceName) + ": \nตั้งค่า Humidity สำเร็จ: Low=" + String(lowHumidity) +
                        "%, High=" + String(highHumidity) + "%");
    }
    // คำสั่ง /setmode <device_id> <mode>
    // mode รับเป็น "auto" หรือ "manual"
    else if (text.startsWith("/setmode")) {
        int firstSpace = text.indexOf(' ');
        if (firstSpace == -1) {
            bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setmode <id> <mode>");
            continue;
        }
        String params = text.substring(firstSpace + 1);
        int spaceIndex = params.indexOf(' ');
        if (spaceIndex == -1) {
            bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setmode <id> <mode>");
            continue;
        }

        String modeStr = params.substring(spaceIndex + 1);
        modeStr.trim();
        if (modeStr.equalsIgnoreCase("auto")) {
            // สมมุติว่า mode auto คือ 1
            // deviceMode หรือ AUTO flag
            AUTO = true;
        } else if (modeStr.equalsIgnoreCase("manual")) {
            AUTO = false;
        } else {
            bot.sendMessage(chat_id, "Mode ไม่ถูกต้อง (ควรเป็น auto หรือ manual)", "");
            continue;
        }
        saveConfig();
        bot.sendMessage(chat_id, String(deviceName) + ": ตั้งค่า Mode สำเร็จ: " + String(AUTO ? "Auto" : "Manual"));
    }
    // คำสั่ง /setoption <device_id> <option>
    // option ควรอยู่ในช่วง 0-4
    else if (text.startsWith("/setoption")) {
        int firstSpace = text.indexOf(' ');
        if (firstSpace == -1) {
            bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setoption <id> <option>");
            continue;
        }
        String params = text.substring(firstSpace + 1);
        int spaceIndex = params.indexOf(' ');
        if (spaceIndex == -1) {
            bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setoption <id> <option>");
            continue;
        }
        String optStr = params.substring(spaceIndex + 1);
        if (!isNumeric(optStr)) {
          bot.sendMessage(chat_id, "ค่าที่ป้อนต้องเป็นตัวเลข 0-4");
          continue;
        }
        int opt = optStr.toInt();
        if (opt < 0 || opt > 4) {
            bot.sendMessage(chat_id, "Option ต้องอยู่ในช่วง 0-4", "");
            continue;
        }
        options = opt;
        saveConfig();
        bot.sendMessage(chat_id, String(deviceName) + ": ตั้งค่า Option สำเร็จ: Option=" + String(options));
    }
    else if (text.startsWith("/relay")) {
        int firstSpace = text.indexOf(' ');
        if (firstSpace == -1) {
            bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /relay <id> <state>");
            continue;
        }
        String params = text.substring(firstSpace + 1);
        int spaceIndex = params.indexOf(' ');
        if (spaceIndex == -1) {
            bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /relay <id> <state>");
            continue;
        }
        String stateStr = params.substring(spaceIndex + 1);
        if (!isNumeric(stateStr)) {
          bot.sendMessage(chat_id, "state ต้องเป็นตัวเลข 0 หรือ 1");
          continue;
        }
        int state = stateStr.toInt();
        
        // คำสั่งนี้ใช้ได้เฉพาะใน Manual mode เท่านั้น
        if (AUTO) {
            bot.sendMessage(chat_id, String(deviceName) + ": \nเครื่องอยู่ใน Auto mode ไม่สามารถสั่ง Relay ด้วยคำสั่งนี้", "");
        } else {
            if (state == 1) {
                turnRelayOn();
                RelayEvent = true;
                bot.sendMessage(chat_id, String(deviceName) + ": Relay ถูกเปิดแล้ว", "");
            } else if (state == 0) {
                turnRelayOff();
                if (afterStart != -1) {
                    t_relay.stop(afterStart);
                }
                if (afterStop != -1) {
                    t_delayStart.stop(afterStop);
                }
                RelayEvent = false;
                afterStart = -1;
                afterStop = -1;
                bot.sendMessage(chat_id, String(deviceName) + ": Relay ถูกปิดแล้ว", "");
            } else {
                bot.sendMessage(chat_id, "ค่าของ state ต้องเป็น 1 (เปิด) หรือ 0 (ปิด)", "");
            }
        }
    }
    else if (text.startsWith("/setname")) {
      String params = text.substring(text.indexOf(' ') + 1);
      int spaceIndex = params.indexOf(' ');
      if (spaceIndex == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setname <id> <name>");
        continue;
      }
      String newName = params.substring(spaceIndex + 1);
      if (newName.length() > DEVICE_NAME_MAX_BYTES) {
        bot.sendMessage(chat_id, "ชื่อยาวเกินไป (สูงสุด " + String(DEVICE_NAME_MAX_BYTES) + " ตัวอักษร)");
        continue;
      }
      newName.toCharArray(deviceName, DEVICE_NAME_MAX_BYTES + 1);
      saveConfig();  
      bot.sendMessage(chat_id, "เปลี่ยนชื่ออุปกรณ์เป็น: " + newName);
    }
    else if (text.startsWith("/setwritekey")) {
      String params = text.substring(text.indexOf(' ') + 1);
      int spaceIndex = params.indexOf(' ');
      if (spaceIndex == -1) {
        bot.sendMessage(chat_id, "รูปแบบ: /setwritekey <id> <key>");
        continue;
      }
      String key = params.substring(spaceIndex + 1);
      if (key.length() > 16) {
        bot.sendMessage(chat_id, "Write API Key ต้องไม่เกิน 16 ตัวอักษร");
        continue;
      }
      key.toCharArray(writeAPIKey, 17);
      saveConfig();  // ✅
      bot.sendMessage(chat_id, "ตั้งค่า Write API Key สำเร็จ");
    }
    else if (text.startsWith("/setreadkey")) {
      String params = text.substring(text.indexOf(' ') + 1);
      int spaceIndex = params.indexOf(' ');
      if (spaceIndex == -1) {
        bot.sendMessage(chat_id, "รูปแบบ: /setreadkey <id> <key>");
        continue;
      }
      String key = params.substring(spaceIndex + 1);
      if (key.length() > 16) {
        bot.sendMessage(chat_id, "Read API Key ต้องไม่เกิน 16 ตัวอักษร");
        continue;
      }
      key.toCharArray(readAPIKey, 17);
      saveConfig();  // ✅
      bot.sendMessage(chat_id, "ตั้งค่า Read API Key สำเร็จ");
    }
    else if (text.startsWith("/setchannel")) {
      String params = text.substring(text.indexOf(' ') + 1);
      int spaceIndex = params.indexOf(' ');
      if (spaceIndex == -1) {
        bot.sendMessage(chat_id, "รูปแบบ: /setchannel <id> <channel_id>");
        continue;
      }
      String idStr = params.substring(spaceIndex + 1);
      if (!isNumeric(idStr)) {
        bot.sendMessage(chat_id, "Channel ID ต้องเป็นตัวเลข");
        continue;
      }
      channelID = idStr.toInt();
      saveConfig();  // ✅
      bot.sendMessage(chat_id, "ตั้งค่า Channel ID เป็น " + String(channelID));
    }
    else if (text.startsWith("/addchatid")) {
      String params = text.substring(text.indexOf(' ') + 1);
      int spaceIndex = params.indexOf(' ');
      if (spaceIndex == -1) {
        bot.sendMessage(chat_id, "รูปแบบ: /addchatid <id> <chatid>");
        continue;
      }
      String newChatID = params.substring(spaceIndex + 1); 
      newChatID.trim();
      if (!isNumeric(newChatID)) {
        bot.sendMessage(chat_id, "Chat ID ต้องเป็นตัวเลข");
        continue;
      }
      if (numAllowedChatIDs >= MAX_ALLOWED_CHATIDS) {
        bot.sendMessage(chat_id, "จำนวน Chat ID เต็มแล้ว");
        continue;
      }
      allowedChatIDs[numAllowedChatIDs] = newChatID;
      numAllowedChatIDs++;
      saveConfig();

      int addedIndex = numAllowedChatIDs - 1;
      String msg = "เพิ่ม Chat ID ลำดับ " + String(addedIndex + 1) + " สำเร็จ: " + newChatID;

      bot.sendMessage(chat_id, msg);

    }
    else if (text.startsWith("/updatechatid")) {
      String params = text.substring(text.indexOf(' ') + 1);

      int space1 = params.indexOf(' ');
      int space2 = params.indexOf(' ', space1 + 1);
      int space3 = params.indexOf(' ', space2 + 1);

      if (space1 == -1 || space2 == -1 || space3 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /updatechatid <id> <index(3-5)> <old_chatid> <new_chatid>");
        continue;
      }

      int idx = params.substring(space1 + 1, space2).toInt();
      String oldIDStr = params.substring(space2 + 1, space3); 
      oldIDStr.trim();
      String newIDStr = params.substring(space3 + 1); 
      newIDStr.trim();

      if (idx < 3 || idx > 5 || !isNumeric(oldIDStr) || !isNumeric(newIDStr)) {
        bot.sendMessage(chat_id, "ระบุ index 3-5 และ Chat ID เป็นตัวเลขเท่านั้น");
        continue;
      }

      int arrayIndex = idx - 1;
      int eepromChatAddr = (idx == 3) ? EEPROM_ADDR_CHATID_2 :
                          (idx == 4) ? EEPROM_ADDR_CHATID_3 :
                                      EEPROM_ADDR_CHATID_4;

      // ✅ ประกาศ currentID ตรงนี้
      unsigned long currentID = EEPROMReadlong(eepromChatAddr);

      if (currentID != oldIDStr.toInt()) {
        bot.sendMessage(chat_id, "ไม่สามารถเปลี่ยนค่าได้: ค่าเดิมไม่ตรงกับที่เก็บไว้");
        continue;
      }

      allowedChatIDs[arrayIndex] = newIDStr;
      saveConfig();

      bot.sendMessage(chat_id, "อัปเดต Chat ID ลำดับ " + String(idx) + " สำเร็จ");
    }

    else if (text.startsWith("/removechatid")) {
      String params = text.substring(text.indexOf(' ') + 1);
      int space1 = params.indexOf(' ');
      int space2 = params.indexOf(' ', space1 + 1);

      if (space1 == -1 || space2 == -1) {
        bot.sendMessage(chat_id, "❌ รูปแบบคำสั่งไม่ถูกต้อง:\n/removechatid <id> <index(3-5)> <old_chatid>");
        return;
      }

      int idx = params.substring(space1 + 1, space2).toInt();
      String oldIDStr = params.substring(space2 + 1);
      oldIDStr.trim();

      if (idx < 3 || idx > 5 || !isNumeric(oldIDStr)) {
        bot.sendMessage(chat_id, "❌ โปรดระบุ index 3-5 และ Chat ID ที่เป็นตัวเลขเท่านั้น");
        return;
      }

      int arrayIndex = idx - 1;
      int eepromChatAddr = (idx == 3) ? EEPROM_ADDR_CHATID_2 :
                          (idx == 4) ? EEPROM_ADDR_CHATID_3 :
                                        EEPROM_ADDR_CHATID_4;

      unsigned long currentID = EEPROMReadlong(eepromChatAddr);
      if (currentID != oldIDStr.toInt()) {
        bot.sendMessage(chat_id, "❌ ไม่สามารถลบได้: Chat ID เดิมไม่ตรงกับที่เก็บไว้");
        return;
      }

      allowedChatIDs[arrayIndex] = "";
      saveConfig();

      // รีคำนวณจำนวน Chat ID ที่ใช้งานจริง
      numAllowedChatIDs = 2;
      for (int i = 2; i < MAX_ALLOWED_CHATIDS; i++) {
        if (allowedChatIDs[i].length() > 0) numAllowedChatIDs++;
      }

      bot.sendMessage(chat_id, "✅ ลบ Chat ID ลำดับ " + String(idx) + " สำเร็จ");
    }
    else if (text.startsWith("/info")) {
      int firstSpace = text.indexOf(' ');
      if (firstSpace == -1) {
          bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /info <id> <secret>");
          return;
      } 

      String params = text.substring(firstSpace + 1);
      int spaceIndex = params.indexOf(' ');
      if (spaceIndex == -1) {
          bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /info <id> <secret>");
          return;
      }

      String secret = params.substring(spaceIndex + 1);
      secret.trim();
      if (!secret.equals(INFO_SECRET)) {
          bot.sendMessage(chat_id, "Secret code ไม่ถูกต้อง", "");
          return;
      }

      char infoMsg[1000];  // ใช้ static buffer ขนาดพอเหมาะ
      snprintf(infoMsg, sizeof(infoMsg),
        "----- Device Info -----\n"
        "Device Name: %s\n"
        "Device ID: %d\n"
        "IP Address: %s\n"
        "MAC Address: %s\n"
        "Temperature Set Points: Low = %.2f°C, High = %.2f°C\n"
        "Humidity Set Points: Low = %.2f%%, High = %.2f%%\n"
        "Mode: %s\n"
        "Option: %d (%s)\n"
        "COOL: %d (%s)\n"
        "MOISTURE: %d (%s)\n"
        "Write API Key: %s\n"
        "Read API Key: %s\n"
        "Channel ID: %lu\n",
        deviceName,
        DEVICE_ID,
        WiFi.localIP().toString().c_str(),
        WiFi.macAddress().c_str(),
        lowTemp, highTemp,
        lowHumidity, highHumidity,
        AUTO ? "AUTO" : "Manual",
        options, 
          (options == 0) ? "Humidity only" :
          (options == 1) ? "Temperature only" :
          (options == 2) ? "Temperature & Humidity" :
          (options == 3) ? "Soil Moisture mode" :
          (options == 4) ? "Additional mode" : "Unknown",
        COOL,
          (COOL == 1) ? "COOL mode: Relay ON เมื่อ Temp >= High" :
                        "HEAT mode: Relay ON เมื่อ Temp <= Low",
        MOISTURE,
          (MOISTURE == 1) ? "Moisture mode: Relay ON เมื่อ Humidity <= Low" :
                            "Dehumidifier mode: Relay ON เมื่อ Humidity >= High",
        writeAPIKey,
        readAPIKey,
        channelID
      );

      // ต่อเติม Allowed Chat IDs แยกบรรทัดต่างหาก
      strncat(infoMsg, "Allowed Chat IDs: ", sizeof(infoMsg) - strlen(infoMsg) - 1);
      for (int j = 0; j < numAllowedChatIDs; j++) {
        strncat(infoMsg, allowedChatIDs[j].c_str(), sizeof(infoMsg) - strlen(infoMsg) - 1);
        if (j < numAllowedChatIDs - 1) {
          strncat(infoMsg, ", ", sizeof(infoMsg) - strlen(infoMsg) - 1);
        }
      }
      strncat(infoMsg, "\n-----------------------", sizeof(infoMsg) - strlen(infoMsg) - 1);

      // ส่งข้อความกลับ
      bot.sendMessage(chat_id, infoMsg);
    } //info
    else if (text.startsWith("/help")) {
        int spaceIndex = text.indexOf(' ');
        if (spaceIndex == -1) {
            bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /help <id>");
            continue;
        }
        bot.sendMessage(chat_id, HELP_MESSAGE);
    } //help

  } // for loop message

}

void getTelegramMessage()
{
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  while(numNewMessages) {
    Serial.println("got response");
    // เรียก yield เพื่อคืน control ให้ system และป้องกัน watchdog reset
    yield();
    handleNewMessages(numNewMessages);
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
  // https://randomnerdtutorials.com/telegram-control-esp32-esp8266-nodemcu-outputs/
}

bool isNumeric(const String& str) {
  if (str.length() == 0) return false;

  bool hasDot = false;
  for (int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);

    // อนุญาตให้มีเครื่องหมายลบเฉพาะตัวแรก
    if (i == 0 && c == '-') continue;

    if (c == '.') {
      if (hasDot) return false;  // มีจุดซ้ำ
      hasDot = true;
    } else if (!isDigit(c)) {
      return false;
    }
  }

  return true;
}

#ifdef THINGSPEAK
void sendDataToThingSpeak()
{
  float celsiusTemperature = 0;
  float rhHumidity = 0;

  readSensorData();

  celsiusTemperature = temperature;
  rhHumidity = humidity;

  Serial.print(celsiusTemperature);
  Serial.print(", ");
  Serial.print(rhHumidity);
  Serial.println();
  Serial.println("Sending data to ThingSpeak : ");

  ThingSpeak.setField( 1, celsiusTemperature );
  ThingSpeak.setField( 2, rhHumidity );
  ThingSpeak.setField( 3, digitalRead(RELAY1));

  int httpResponseCode = ThingSpeak.writeFields(channelID, writeAPIKey);
  
  if (httpResponseCode == 200) {
    Serial.println("ส่งข้อมูล ThingSpeak สำเร็จ");
  } else {
    Serial.println("ส่งข้อมูล ThingSpeak ล้มเหลว: " + String(httpResponseCode));
  }
  Serial.println();

}
#endif

void turnRelayOn()
{
  digitalWrite(RELAY1, HIGH);
  Serial.println("RELAY1 ON");
  digitalWrite(LED_BUILTIN, LOW);  // turn on
  buzzer_sound();
}

void turnRelayOff()
{
  digitalWrite(RELAY1, LOW);
  Serial.println("RELAY1 OFF");
  digitalWrite(LED_BUILTIN, HIGH);  // turn off
  buzzer_sound();
}

#ifdef SECONDRELAY
void turnRelay2On()
{
  digitalWrite(RELAY2, HIGH);
  Serial.println("RELAY2 ON");
  digitalWrite(LED_BUILTIN, LOW);  // turn on
  buzzer_sound();
}

void turnRelay2Off()
{
  digitalWrite(RELAY2, LOW);
  Serial.println("RELAY2 OFF");
  digitalWrite(LED_BUILTIN, HIGH);  // turn off
  buzzer_sound();
}
#endif

void turnoff()
{
  afterStop = t_delayStart.after(standbyPeriod, delayStart);   // 10 * 60 * 1000 = 10 minutes
  t_relay.stop(afterStart);
  if (standbyPeriod >= 5000) {
    turnRelayOff();
    Serial.println("Timer Stop: RELAY1 OFF");
  }
  afterStart = -1;
}

void delayStart()
{
  t_delayStart.stop(afterStop);
  RelayEvent = false;
  afterStop = -1;
  Serial.println("Timer Delay Relay #1 End.");
}

void turnoffRelay2()
{
  afterStop2 = t_delayStart2.after(standbyPeriod, delayStart2);   // 10 * 60 * 1000 = 10 minutes
  t_relay2.stop(afterStart2);
  if (standbyPeriod >= 5000) {
    #ifdef SECONDRELAY
    turnRelay2Off();
    #endif
    Serial.println("Timer Stop: RELAY2 OFF");
  }
  afterStart2 = -1;
}

void delayStart2()
{
  t_delayStart2.stop(afterStop2);
  Relay2Event = false;
  afterStop2 = -1;
  Serial.println("Timer Delay Relay #2 End.");
}

void controlRelay()
{
  /*
   *  read data from temperature & humidity sensor
   *  set action by options
   *  options:
   *  4 = temperature or humidity (temperature relay2)
   *  3 = soil moisture
   *  2 = temperature & humidity
   *  1 = temperature
   *  0 = humidity
   *
  */

  int sensorStatus;

  sensorStatus = readSensorData();  // 0 is OK

  if (AUTO) {
    Serial.print("\tOptions : ");
    Serial.println(options);

    if(sensorStatus == 0) {
      // Moisture mode
      // กำหนด flag เพื่อให้ relay เปิด หรือ ปิด 
      if (MOISTURE == 1) {  // ความชื้นต่ำให้เปิด relay
        if (humidity_sensor_value < lowHumidity) {
          humion = true;
        }
        else if (humidity_sensor_value > highHumidity) { // ความชื้นสูงให้ปิด relay
          humion = false;
        }
      }
      // Dehumidifier mode
      else if (MOISTURE == 0){ // ความชื้นสูงให้เปิด relay หรือให้เปิด heater
        if (humidity_sensor_value > highHumidity) {
          humion = true;
        }
        else if (humidity_sensor_value < lowHumidity) { // ความชื้นต่ำให้ปิด relay 
          humion = false;
        }
      }
      // cool mode
      if(COOL == 1) {
        if (temperature_sensor_value > highTemp) { // ร้อนให้เปิด relay
          tempon = true;
        }
        else if (temperature_sensor_value < lowTemp) { // เย็นให้ปิด relay
          tempon = false;
        }
      }
      // heater mode
      else if (COOL == 0){
        if (temperature_sensor_value < lowTemp) {  // เย็นให้เปิด relay or heater
          tempon = true;
        }
        else if (temperature_sensor_value > highTemp) { // ร้อนให้ปิด relay or heater
          tempon = false;
        }
      }
      // จบเงื่อนไข

      // ตรวจสอบ option เพื่อทำงานตามอุณหภูมิ หรือ ความชื้น หรือทั้งสองอย่าง 
      if (options == 2) { // ทำงานสองเงื่อนไขพร้อมกัน 
        Serial.println("Option: Temperature & Humidity");
        if (tempon == true && humion == true) {
          if (RelayEvent == false) {
            afterStart = t_relay.after(onPeriod, turnoff);
            Serial.println("On Timer Start.");
            RelayEvent = true;
            turnRelayOn();
          }
        }
        else if (tempon == false && humion == false) {
          if (afterStart != -1) {
            t_relay.stop(afterStart);
            afterStart = -1;
          }
          Serial.println("OFF");
          if (digitalRead(RELAY1) == HIGH) {
            turnRelayOff();
          }

          // delay start
          if (RelayEvent == true && afterStop == -1) {
              afterStop = t_delayStart.after(standbyPeriod, delayStart);   // 10 * 60 * 1000 = 10 minutes
              Serial.println("Timer Delay Start");
          }
        }
      }
      if (options == 1 || options == 4) {
        Serial.println("Option: Temperature");
        if (tempon == true) {
          if (options == 1) {
            if (RelayEvent == false) {
              afterStart = t_relay.after(onPeriod, turnoff);
              Serial.println("On Timer Relay #1 Start.");
              RelayEvent = true;
              turnRelayOn();
            }
          }
          else {
            if (Relay2Event == false) {
              afterStart2 = t_relay2.after(onPeriod, turnoffRelay2);
              Serial.println("On Timer Relay #2 Start.");
              Relay2Event = true;
              #ifdef SECONDRELAY
              turnRelay2On();
              #endif
            }
          }

        }
        else if (tempon == false) {
          if (options == 1) {
            if (afterStart != -1) {
              t_relay.stop(afterStart);
              afterStart = -1;
            }
            Serial.println("OFF");
            if (digitalRead(RELAY1) == HIGH) {
              turnRelayOff();
            }

            // delay start
            if (RelayEvent == true && afterStop == -1) {
                afterStop = t_delayStart.after(standbyPeriod, delayStart);   // 10 * 60 * 1000 = 10 minutes
                Serial.println("Timer Delay Relay #1 Start");
            }
          }
          else {
            if (afterStart2 != -1) {
              t_relay2.stop(afterStart2);
              afterStart2 = -1;
            }
            Serial.println("OFF");
            #ifdef SECONDRELAY
            if (digitalRead(RELAY2) == HIGH) {
              turnRelay2Off();
            }
            #endif

            // delay start
            if (Relay2Event == true && afterStop2 == -1) {
                afterStop2 = t_delayStart2.after(standbyPeriod, delayStart2);   // 10 * 60 * 1000 = 10 minutes
                Serial.println("Timer Delay Relay #2 Start");
            }
          }
        }
      }
      if (options == 0 || options == 4) {
        Serial.println("Option: Humidity");
        if (humion == true) {
          if (RelayEvent == false) {
            afterStart = t_relay.after(onPeriod, turnoff);
            Serial.println("On Timer Start.");
            RelayEvent = true;
            turnRelayOn();
          }
        }
        else if (humion == false) {
          if (afterStart != -1) {
            t_relay.stop(afterStart);
            afterStart = -1;
          }
          Serial.println("OFF");
          if (digitalRead(RELAY1) == HIGH) {
            turnRelayOff();
          }

          // delay start
          if (RelayEvent == true && afterStop == -1) {
              afterStop = t_delayStart.after(standbyPeriod, delayStart);   // 10 * 60 * 1000 = 10 minutes
              Serial.println("Timer Delay Start");
          }
        }
      }

      if (options == 3) {
        #ifdef SOILMOISTURE
        soilMoistureSensor();
        Serial.println("Soil Moisture Mode");
        #endif
      }


      Serial.print("tempon = ");
      Serial.print(tempon);
      Serial.print(" humion = ");
      Serial.print(humion);
      Serial.print(" RelayEvent = ");
      Serial.print(RelayEvent);
      Serial.print(" afterStart = ");
      Serial.print(afterStart);
      Serial.print(" afterStop = ");
      Serial.println(afterStop);
      Serial.println();

      Serial.print("Relay #1 Status ");
      bool value1 = (0!=(*portOutputRegister( digitalPinToPort(RELAY1) ) & digitalPinToBitMask(RELAY1)));
      Serial.println(value1);
      #ifdef SECONDRELAY
      Serial.print("Relay #2 Status ");
      bool value2 = (0!=(*portOutputRegister( digitalPinToPort(RELAY2) ) & digitalPinToBitMask(RELAY2)));
      Serial.println(value2);
      #endif

    } // if sensorStatus
  } // if AUTO 
  else {
    Serial.println("Manual Mode Active");
  }


}

int readSensorData() 
{
  if (sht30.get() == 0) {
    humidity_sensor_value = (int) sht30.humidity;
    temperature_sensor_value = sht30.cTemp;
    fTemperature = sht30.fTemp;
    Serial.println("Device " + String(DEVICE_ID) + " - Temperature: " + String(temperature_sensor_value) + "°C, Humidity: " + String(humidity_sensor_value) + "%");
    temperature = temperature_sensor_value;
    humidity = humidity_sensor_value;
    return 0; // OK
  }
  else {
    Serial.println("Failed to read from SHT30 sensor.");
    return 1; // 
  }
}

void printConfig()
{
  Serial.println(F("===== Device Configuration ====="));

  Serial.printf("Device Name:     %s\n", deviceName);
  Serial.printf("Device ID:       %d\n", DEVICE_ID);
  Serial.printf("Temperature Set: Low = %.2f°C, High = %.2f°C\n", lowTemp, highTemp);
  Serial.printf("Humidity Set:    Low = %.2f%%, High = %.2f%%\n", lowHumidity, highHumidity);

  Serial.printf("Mode:            %s\n", AUTO ? "AUTO" : "Manual");

  Serial.printf("Option:          %d (%s)\n", options,
    (options == 0) ? "Humidity only" :
    (options == 1) ? "Temperature only" :
    (options == 2) ? "Temperature & Humidity" :
    (options == 3) ? "Soil Moisture mode" :
    (options == 4) ? "Additional mode" : "Unknown");

  Serial.printf("COOL:            %d (%s)\n", COOL,
    (COOL == 1) ? "COOL mode" : "HEAT mode");

  Serial.printf("MOISTURE:        %d (%s)\n", MOISTURE,
    (MOISTURE == 1) ? "Moisture mode" : "Dehumidifier mode");

  Serial.printf("Write API Key:   %s\n", writeAPIKey);
  Serial.printf("Read  API Key:   %s\n", readAPIKey);
  Serial.printf("Channel ID:      %lu\n", channelID);

  Serial.print("Allowed Chat IDs: ");
  for (int i = 0; i < numAllowedChatIDs; i++) {
    Serial.print(allowedChatIDs[i]);
    if (i < numAllowedChatIDs - 1) Serial.print(", ");
  }
  Serial.println();

  Serial.println(F("================================"));
}


void saveConfig()
{
  EEPROM.begin(EEPROM_SIZE);

  EEPROMWritelong(EEPROM_ADDR_HIGH_HUMIDITY, (long) highHumidity);
  EEPROMWritelong(EEPROM_ADDR_LOW_HUMIDITY,  (long) lowHumidity);
  EEPROMWritelong(EEPROM_ADDR_HIGH_TEMP,     (long) highTemp);
  EEPROMWritelong(EEPROM_ADDR_LOW_TEMP,      (long) lowTemp);

  eeWriteInt(EEPROM_ADDR_OPTIONS,  options);
  eeWriteInt(EEPROM_ADDR_COOL,     COOL);
  eeWriteInt(EEPROM_ADDR_MOISTURE, MOISTURE);

  writeEEPROM(writeAPIKey, EEPROM_ADDR_WRITE_APIKEY, 16);
  writeEEPROM(readAPIKey,  EEPROM_ADDR_READ_APIKEY, 16);
  writeEEPROM(auth,        EEPROM_ADDR_AUTH,        32);

  EEPROMWritelong(EEPROM_ADDR_CHANNEL_ID, (long) channelID);
  writeEEPROM(deviceName, EEPROM_ADDR_DEVICE_NAME, DEVICE_NAME_MAX_BYTES);

  // Save allowedChatIDs index 2–4
  for (int i = 2; i < MAX_ALLOWED_CHATIDS; i++) {
    unsigned long id = (allowedChatIDs[i].length() > 0) ? allowedChatIDs[i].toInt() : 0xFFFFFFFF;
    int addr = (i == 2) ? EEPROM_ADDR_CHATID_2 :
               (i == 3) ? EEPROM_ADDR_CHATID_3 :
                          EEPROM_ADDR_CHATID_4;
    EEPROMWritelong(addr, id);
  }

  eeWriteInt(EEPROM_ADDR_VERSION_MARK, 6550);
  EEPROM.end();
}


void getConfig()
{
  EEPROM.begin(EEPROM_SIZE);

  highHumidity = (float) EEPROMReadlong(EEPROM_ADDR_HIGH_HUMIDITY);
  lowHumidity  = (float) EEPROMReadlong(EEPROM_ADDR_LOW_HUMIDITY);
  highTemp     = (float) EEPROMReadlong(EEPROM_ADDR_HIGH_TEMP);
  lowTemp      = (float) EEPROMReadlong(EEPROM_ADDR_LOW_TEMP);

  options  = eeGetInt(EEPROM_ADDR_OPTIONS);
  COOL     = eeGetInt(EEPROM_ADDR_COOL);
  MOISTURE = eeGetInt(EEPROM_ADDR_MOISTURE);

  readEEPROM(writeAPIKey, EEPROM_ADDR_WRITE_APIKEY, 16);
  readEEPROM(readAPIKey,  EEPROM_ADDR_READ_APIKEY, 16);
  readEEPROM(auth,        EEPROM_ADDR_AUTH,        32);

  channelID = (unsigned long) EEPROMReadlong(EEPROM_ADDR_CHANNEL_ID);
  readEEPROM(deviceName, EEPROM_ADDR_DEVICE_NAME, DEVICE_NAME_MAX_BYTES);

  numAllowedChatIDs = 2;

  unsigned long id;
  id = EEPROMReadlong(EEPROM_ADDR_CHATID_2);
  if (id != 0xFFFFFFFF) {
    allowedChatIDs[2] = String(id);
    numAllowedChatIDs++;
  }

  id = EEPROMReadlong(EEPROM_ADDR_CHATID_3);
  if (id != 0xFFFFFFFF) {
    allowedChatIDs[3] = String(id);
    numAllowedChatIDs++;
  }

  id = EEPROMReadlong(EEPROM_ADDR_CHATID_4);
  if (id != 0xFFFFFFFF) {
    allowedChatIDs[4] = String(id);
    numAllowedChatIDs++;
  }

  EEPROM.end();
}



void autoWifiConnect()
{
  WiFiManager wifiManager;
  bool res;

  //first parameter is name of access point, second is the password
  res = wifiManager.autoConnect("ogosense", "12345678");

  if(!res) {
      Serial.println("Failed to connect");
      delay(3000);
      ESP.restart();
      delay(5000);
  } 
  else {
      //if you get here you have connected to the WiFi    
      Serial.println("connected...yeey :)");
  }
}

void eeWriteInt(int pos, int val) {
    byte* p = (byte*) &val;
    EEPROM.write(pos, *p);
    EEPROM.write(pos + 1, *(p + 1));
    EEPROM.write(pos + 2, *(p + 2));
    EEPROM.write(pos + 3, *(p + 3));
    EEPROM.commit();
}

int eeGetInt(int pos) {
  int val;
  byte* p = (byte*) &val;
  *p        = EEPROM.read(pos);
  *(p + 1)  = EEPROM.read(pos + 1);
  *(p + 2)  = EEPROM.read(pos + 2);
  *(p + 3)  = EEPROM.read(pos + 3);
  return val;
}

void EEPROMWritelong(int address, long value)
{
  //Decomposition from a long to 4 bytes by using bitshift.
  //One = Most significant -> Four = Least significant byte
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);

  Serial.print(four);
  Serial.print(" ");
  Serial.print(three);
  Serial.print(" ");
  Serial.print(two);
  Serial.print(" ");
  Serial.print(one);
  Serial.println();

  //Write the 4 bytes into the eeprom memory.
  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
  EEPROM.commit();
}

long EEPROMReadlong(int address)
{
  //Read the 4 bytes from the eeprom memory.
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);

  Serial.print(four);
  Serial.print(" ");
  Serial.print(three);
  Serial.print(" ");
  Serial.print(two);
  Serial.print(" ");
  Serial.print(one);
  Serial.println();

  //Return the recomposed long by using bitshift.
  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

void readEEPROM(char* buff, int offset, int len) {
    int i;
    for (i=0;i<len;i++) {
        buff[i] = (char)EEPROM.read(offset+i);
    }
    buff[len] = '\0';
}

void writeEEPROM(char* buff, int offset, int len) {
    int i;
    for (i=0;i<len;i++) {
        EEPROM.write(offset+i,buff[i]);
    }
    EEPROM.commit();
}

void buzzer_sound()
{
  analogWriteRange(1047);
  analogWrite(buzzer, 512);
  delay(100);
  analogWrite(buzzer, 0);
  pinMode(buzzer, OUTPUT);
  digitalWrite(buzzer, LOW);
  delay(100);

  analogWriteRange(1175);
  analogWrite(buzzer, 512);
  delay(300);
  analogWrite(buzzer, 0);
  pinMode(buzzer, OUTPUT);
  digitalWrite(buzzer, LOW);
  delay(300);
}

void blink()
{
  static bool ledState = false;  // ใช้ static variable เพื่อเก็บสถานะ LED ปัจจุบัน
  ledState = !ledState;          // สลับสถานะ
  digitalWrite(LED, ledState);
}