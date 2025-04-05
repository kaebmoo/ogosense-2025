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

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <time.h>
#include <Ticker.h>
#include "ogosense_esp32.h"

#include <EEPROM.h>

// กำหนดตำแหน่ง EEPROM
#define EEPROM_SIZE 512
#define EEPROM_ADDR_CHATID_0    0  // 4 bytes (default chat)
#define EEPROM_ADDR_CHATID_1    4  // 4 bytes (default chat)
#define EEPROM_ADDR_CHATID_2    8  // 4 bytes (เพิ่มเติมได้)
#define EEPROM_ADDR_CHATID_3   12  // 4 bytes (เพิ่มเติมได้)
#define EEPROM_ADDR_CHATID_4   16  // 4 bytes (เพิ่มเติมได้)
#define EEPROM_ADDR_NUM_CHATS  20  // 1 byte (จำนวน chat ids)

#define LED_PIN 2  // GPIO2 เป็น LED บนบอร์ด ESP32 บางรุ่น


WiFiClientSecure telegramClient;
UniversalTelegramBot bot(BOT_TOKEN, telegramClient);


WiFiClientSecure mqttClient;
PubSubClient mqtt(mqttClient);

// ตรวจสอบข้อความใหม่จาก Telegram ทุกๆ กี่มิลลิวินาที
unsigned long bot_lasttime;
const unsigned long BOT_MTBS = 1000;           // 1 วินาทีต่อครั้ง

Ticker blinker;

// ฟังก์ชันประกาศล่วงหน้า
void connectToWiFi();
void autoWifiConnect();
void syncTime();
void connectToMQTT();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void handleNewMessages(int numNewMessages);
bool isNumeric(const String& str);

// รายการคำสั่งที่รองรับ - ปรับปรุงให้แสดงรายละเอียดที่ชัดเจนขึ้น
const String HELP_MESSAGE = 
"Available Commands (คำสั่งที่สามารถใช้ได้):\n"
"/start - เริ่มต้นใช้งานอุปกรณ์\n"
"/help - แสดงรายการคำสั่งทั้งหมด\n\n"

"คำสั่งสำหรับควบคุมอุปกรณ์ ESP8266:\n"
"/status <id> - ตรวจสอบสถานะอุปกรณ์\n"
"/settemp <id> <lowTemp> <highTemp> - ตั้งค่าขอบเขต Temperature\n"
"/sethum <id> <lowHumidity> <highHumidity> - ตั้งค่าขอบเขต Humidity\n"
"/setmode <id> <auto/manual> - ตั้งค่าโหมด Auto หรือ Manual\n"
"/setoption <id> <0-4> - ตั้งค่าโหมดควบคุม (Option)\n"
"/relay <id> <0/1> - สั่งเปิด/ปิด Relay (Manual เท่านั้น)\n"
"/setname <id> <name> - เปลี่ยนชื่ออุปกรณ์\n"
"/setchannel <id> <channel_id> - ตั้งค่า ThingSpeak Channel ID\n"
"/setwritekey <id> <api_key> - ตั้งค่า ThingSpeak Write API Key\n"
"/setreadkey <id> <api_key> - ตั้งค่า ThingSpeak Read API Key\n"
"/info <id> <secret> - แสดงข้อมูลอุปกรณ์ (Device Info)\n\n"

"คำสั่งสำหรับจัดการ ESP32 MQTT Bridge (ต้องใช้ Device ID ของ ESP32):\n"
"/addchatid <esp32_id> <chat_id> - เพิ่ม Chat ID\n"
"/removechatid <esp32_id> <index(3-5)> <old_id> - ลบ Chat ID ตามตำแหน่ง\n"
"/updatechatid <esp32_id> <index(3-5)> <old_id> <new_id> - แก้ไข Chat ID\n"
"/listchatids <esp32_id> - แสดงรายการ Chat IDs ทั้งหมด\n\n"

"หมายเหตุ: <id> คือ Device ID ของอุปกรณ์ ESP8266\n"
"<esp32_id> คือ Device ID ของอุปกรณ์ ESP32 MQTT Bridge";

void setup() {
  Serial.begin(115200);
  delay(100);
  
  pinMode(LED_PIN, OUTPUT);

  Serial.println();
  Serial.println("เริ่มต้นระบบ ESP32 Telegram MQTT Bridge");

  // เริ่มต้น EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadAllowedChatIDs();

  // เชื่อมต่อ WiFi
  autoWifiConnect();
  
  // ซิงค์เวลา
  syncTime();

  // ตั้งค่า SSL Certificates สำหรับ Telegram และ MQTT
  telegramClient.setCACert(TELEGRAM_CERTIFICATE_ROOT);  // สำหรับ Telegram
  
  mqttClient.setCACert(ca_cert);      // สำหรับ MQTT

  // ตั้งค่า MQTT
  mqtt.setServer(mqtt_broker, mqtt_port);
  mqtt.setCallback(mqttCallback);
  connectToMQTT();
  

  bot_lasttime = millis();
  blinker.attach(1.0, blink);  // เรียก blink() ทุก 1 วินาที
}

void loop() {
  
  // ตรวจสอบการเชื่อมต่อ MQTT
  if (!mqtt.connected()) {
    connectToMQTT();
  }
  mqtt.loop();
  

  // ตรวจสอบข้อความใหม่จาก Telegram
  if (millis() > bot_lasttime + BOT_MTBS) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      Serial.println("Got Telegram messages: " + String(numNewMessages));
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    bot_lasttime = millis();
  }
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("เชื่อมต่อ WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nเชื่อมต่อ WiFi สำเร็จ");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
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



void syncTime() {
  configTime(0, 0, "pool.ntp.org");
  Serial.print("กำลังซิงค์เวลา");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nซิงค์เวลาสำเร็จ เวลา: " + String(ctime(&now)));
}

void connectToMQTT() {
  int retries = 0;
  while (!mqtt.connected() && retries < 5) {
    // String client_id = "esp32-client-" + String(WiFi.macAddress()); // 59321
    String client_id = "esp32-telegram-broker-" + String(DEVICE_ID); // 59321
    Serial.print("เชื่อมต่อ MQTT (client_id: ");
    Serial.print(client_id);
    Serial.print(")... ");

    if (mqtt.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("สำเร็จ");
    } else {
      Serial.print("ล้มเหลว state=");
      Serial.print(mqtt.state());
      Serial.println(" ลองใหม่ในอีก 5 วินาที");
      retries++;
      delay(5000);
    }
  }
  if (retries >= 5) {
    Serial.println("เชื่อมต่อ MQTT ไม่สำเร็จหลังจากพยายาม 5 ครั้ง");
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.print("ได้รับข้อความ MQTT จาก topic: ");
  Serial.print(topic);
  Serial.print(" ข้อความ: ");
  
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);
  
  // ตรวจสอบว่าเป็นการตอบกลับจากอุปกรณ์หรือไม่
  String topicStr = String(topic);
  if (topicStr.startsWith("ogosense/resp/")) {
    // แปลง payload เป็น JSON
    StaticJsonDocument<512> doc;
    deserializeJson(doc, message);
    
    // ตรวจสอบ device_id และคำสั่ง
    if (doc.containsKey("device_id") && doc.containsKey("command")) {
      String deviceId = doc["device_id"].as<String>();
      String command = doc["command"].as<String>();
      
      // สร้างข้อความตอบกลับไปยัง Telegram (ถ้าต้องการ)
      String telegramResponse = "ได้รับการตอบกลับจากอุปกรณ์ " + deviceId + "\n";
      telegramResponse += "คำสั่ง: " + command + "\n";
      
      // เพิ่มข้อมูลเพิ่มเติมตามประเภทคำสั่ง
      if (command == "status" && doc.containsKey("data")) {
        telegramResponse += "อุณหภูมิ: " + String(doc["data"]["temperature"].as<float>()) + "°C\n";
        telegramResponse += "ความชื้น: " + String(doc["data"]["humidity"].as<float>()) + "%\n";
        telegramResponse += "สถานะ Relay: " + String(doc["data"]["relay"] ? "ON" : "OFF") + "\n";
      }
      
      // ส่งข้อความไปยัง Telegram (อาจต้องเก็บ chat_id ไว้ในตอนที่รับคำสั่ง)
      // ในที่นี้ส่งไปยัง CHAT_ID เป็นตัวอย่าง
      bot.sendMessage(CHAT_ID, telegramResponse, "");
    }
  }
}

// ตรวจสอบว่าข้อความเป็นตัวเลขหรือไม่
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

// เพิ่มฟังก์ชันสำหรับตรวจสอบ Device ID ของ ESP32
bool checkDeviceId(const String& inputDeviceId) {
  // แปลง DEVICE_ID (จากไฟล์ ogosense_esp32.h) เป็นข้อความ
  String espDeviceId = String(DEVICE_ID);
  
  // ตรวจสอบว่า Device ID ที่ระบุตรงกับ Device ID ของ ESP32 หรือไม่
  return (inputDeviceId == espDeviceId);
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    
    // ตรวจสอบว่า chat_id อยู่ในรายชื่อที่อนุญาตหรือไม่
    bool authorized = false;
    for (int j = 0; j < numAuthorizedChatIds; j++) {
      if (chat_id == authorizedChatIds[j]) {
        authorized = true;
        break;
      }
    }

    if (!authorized) {
      bot.sendMessage(chat_id, "คุณไม่มีสิทธิ์ใช้งานระบบนี้", "");
      continue;
    }

    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;
    Serial.println("ได้รับข้อความ: " + text + " จาก " + from_name);

    // คำสั่ง /start
    if (text == "/start") {
      String welcome = "ยินดีต้อนรับสู่ Telegram MQTT Bridge\n";
      welcome += "คุณสามารถใช้คำสั่งต่างๆ เพื่อควบคุมอุปกรณ์ได้\n";
      welcome += "พิมพ์ /help เพื่อดูรายการคำสั่งทั้งหมด";
      bot.sendMessage(chat_id, welcome, "");
      continue;
    }

    // ถ้ามีแค่คำสั่ง /help โดยไม่มีพารามิเตอร์
    if (text == "/help") {
      bot.sendMessage(chat_id, HELP_MESSAGE, "");
      continue;
    }

    // ตรวจสอบว่ามีช่องว่างแรกหรือไม่
    int firstSpace = text.indexOf(' ');
    if (firstSpace == -1) {
      bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: ต้องมี /[command] <id>", "");
      continue;
    }

    // แยกพารามิเตอร์หลังช่องว่างแรก
    String params = text.substring(firstSpace + 1);
    int space1 = params.indexOf(' ');

    // ตรวจสอบ device_id
    String deviceIdStr;
    if (space1 == -1) {
      deviceIdStr = params;
    } else {
      deviceIdStr = params.substring(0, space1);
    }
    deviceIdStr.trim();
    
    if (!isNumeric(deviceIdStr)) {
      bot.sendMessage(chat_id, "Device ID ต้องเป็นตัวเลขเท่านั้น", "");
      continue;
    }
    
    int deviceId = deviceIdStr.toInt();
    String deviceIdStr2 = String(deviceId);  // เพื่อแน่ใจว่าเป็นตัวเลขล้วน

    // ---------- คำสั่ง /status ----------
    if (text.startsWith("/status")) {
      // สร้าง JSON สำหรับ MQTT
      StaticJsonDocument<200> doc;
      doc["command"] = "status";
      doc["device_id"] = deviceIdStr2;
      doc["timestamp"] = millis();
      
      // ส่งผ่าน MQTT
      String topic = String(mqtt_topic_cmd) + deviceIdStr2;
      String jsonStr;
      serializeJson(doc, jsonStr);
      
      if (mqtt.publish(topic.c_str(), jsonStr.c_str())) {
        bot.sendMessage(chat_id, "กำลังตรวจสอบสถานะอุปกรณ์ " + deviceIdStr2, "");
        Serial.println("ส่งคำสั่ง status ไปยัง MQTT สำเร็จ: " + jsonStr);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /settemp ----------
    else if (text.startsWith("/settemp")) {
      if (space1 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /settemp <id> <lowTemp> <highTemp>", "");
        continue;
      }
      
      String restParams = params.substring(space1 + 1);
      int space2 = restParams.indexOf(' ');
      
      if (space2 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /settemp <id> <lowTemp> <highTemp>", "");
        continue;
      }
      
      String lowTempStr = restParams.substring(0, space2);
      String highTempStr = restParams.substring(space2 + 1);
      
      if (!isNumeric(lowTempStr) || !isNumeric(highTempStr)) {
        bot.sendMessage(chat_id, "ค่าอุณหภูมิต้องเป็นตัวเลข", "");
        continue;
      }
      
      float lowTemp = lowTempStr.toFloat();
      float highTemp = highTempStr.toFloat();
      
      // สร้าง JSON สำหรับ MQTT
      StaticJsonDocument<200> doc;
      doc["command"] = "settemp";
      doc["device_id"] = deviceIdStr2;
      doc["low"] = lowTemp;
      doc["high"] = highTemp;
      doc["timestamp"] = millis();
      
      // ส่งผ่าน MQTT
      String topic = String(mqtt_topic_cmd) + deviceIdStr2;
      String jsonStr;
      serializeJson(doc, jsonStr);
      
      if (mqtt.publish(topic.c_str(), jsonStr.c_str())) {
        bot.sendMessage(chat_id, "ตั้งค่าอุณหภูมิสำหรับอุปกรณ์ " + deviceIdStr2 + "\nLow: " + lowTempStr + "°C, High: " + highTempStr + "°C", "");
        Serial.println("ส่งคำสั่ง settemp ไปยัง MQTT สำเร็จ: " + jsonStr);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /sethum ----------
    else if (text.startsWith("/sethum")) {
      if (space1 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /sethum <id> <lowHumidity> <highHumidity>", "");
        continue;
      }
      
      String restParams = params.substring(space1 + 1);
      int space2 = restParams.indexOf(' ');
      
      if (space2 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /sethum <id> <lowHumidity> <highHumidity>", "");
        continue;
      }
      
      String lowHumStr = restParams.substring(0, space2);
      String highHumStr = restParams.substring(space2 + 1);
      
      if (!isNumeric(lowHumStr) || !isNumeric(highHumStr)) {
        bot.sendMessage(chat_id, "ค่าความชื้นต้องเป็นตัวเลข", "");
        continue;
      }
      
      float lowHum = lowHumStr.toFloat();
      float highHum = highHumStr.toFloat();
      
      // สร้าง JSON สำหรับ MQTT
      StaticJsonDocument<200> doc;
      doc["command"] = "sethum";
      doc["device_id"] = deviceIdStr2;
      doc["low"] = lowHum;
      doc["high"] = highHum;
      doc["timestamp"] = millis();
      
      // ส่งผ่าน MQTT
      String topic = String(mqtt_topic_cmd) + deviceIdStr2;
      String jsonStr;
      serializeJson(doc, jsonStr);
      
      if (mqtt.publish(topic.c_str(), jsonStr.c_str())) {
        bot.sendMessage(chat_id, "ตั้งค่าความชื้นสำหรับอุปกรณ์ " + deviceIdStr2 + "\nLow: " + lowHumStr + "%, High: " + highHumStr + "%", "");
        Serial.println("ส่งคำสั่ง sethum ไปยัง MQTT สำเร็จ: " + jsonStr);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /setmode ----------
    else if (text.startsWith("/setmode")) {
      if (space1 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setmode <id> <auto/manual>", "");
        continue;
      }
      
      String modeStr = params.substring(space1 + 1);
      modeStr.trim();
      modeStr.toLowerCase();
      
      if (modeStr != "auto" && modeStr != "manual") {
        bot.sendMessage(chat_id, "โหมดไม่ถูกต้อง ต้องเป็น auto หรือ manual เท่านั้น", "");
        continue;
      }
      
      // สร้าง JSON สำหรับ MQTT
      StaticJsonDocument<200> doc;
      doc["command"] = "setmode";
      doc["device_id"] = deviceIdStr2;
      doc["mode"] = modeStr;
      doc["timestamp"] = millis();
      
      // ส่งผ่าน MQTT
      String topic = String(mqtt_topic_cmd) + deviceIdStr2;
      String jsonStr;
      serializeJson(doc, jsonStr);
      
      if (mqtt.publish(topic.c_str(), jsonStr.c_str())) {
        bot.sendMessage(chat_id, "ตั้งค่าโหมดสำหรับอุปกรณ์ " + deviceIdStr2 + " เป็น " + modeStr, "");
        Serial.println("ส่งคำสั่ง setmode ไปยัง MQTT สำเร็จ: " + jsonStr);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /setoption ----------
    else if (text.startsWith("/setoption")) {
      if (space1 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setoption <id> <0-4>", "");
        continue;
      }
      
      String optionStr = params.substring(space1 + 1);
      optionStr.trim();
      
      if (!isNumeric(optionStr) || optionStr.toInt() < 0 || optionStr.toInt() > 4) {
        bot.sendMessage(chat_id, "Option ต้องเป็นตัวเลข 0-4 เท่านั้น", "");
        continue;
      }
      
      int option = optionStr.toInt();
      
      // สร้าง JSON สำหรับ MQTT
      StaticJsonDocument<200> doc;
      doc["command"] = "setoption";
      doc["device_id"] = deviceIdStr2;
      doc["option"] = option;
      doc["timestamp"] = millis();
      
      // ส่งผ่าน MQTT
      String topic = String(mqtt_topic_cmd) + deviceIdStr2;
      String jsonStr;
      serializeJson(doc, jsonStr);
      
      if (mqtt.publish(topic.c_str(), jsonStr.c_str())) {
        bot.sendMessage(chat_id, "ตั้งค่า Option สำหรับอุปกรณ์ " + deviceIdStr2 + " เป็น " + optionStr, "");
        Serial.println("ส่งคำสั่ง setoption ไปยัง MQTT สำเร็จ: " + jsonStr);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /relay ----------
    else if (text.startsWith("/relay")) {
      if (space1 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /relay <id> <0/1>", "");
        continue;
      }
      
      String stateStr = params.substring(space1 + 1);
      stateStr.trim();
      
      if (!isNumeric(stateStr) || (stateStr != "0" && stateStr != "1")) {
        bot.sendMessage(chat_id, "สถานะ Relay ต้องเป็น 0 หรือ 1 เท่านั้น", "");
        continue;
      }
      
      int state = stateStr.toInt();
      
      // สร้าง JSON สำหรับ MQTT
      StaticJsonDocument<200> doc;
      doc["command"] = "relay";
      doc["device_id"] = deviceIdStr2;
      doc["state"] = state;
      doc["timestamp"] = millis();
      
      // ส่งผ่าน MQTT
      String topic = String(mqtt_topic_cmd) + deviceIdStr2;
      String jsonStr;
      serializeJson(doc, jsonStr);
      
      if (mqtt.publish(topic.c_str(), jsonStr.c_str())) {
        bot.sendMessage(chat_id, "ตั้งค่า Relay สำหรับอุปกรณ์ " + deviceIdStr2 + " เป็น " + (state ? "ON" : "OFF"), "");
        Serial.println("ส่งคำสั่ง relay ไปยัง MQTT สำเร็จ: " + jsonStr);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /setname ----------
    else if (text.startsWith("/setname")) {
      if (space1 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setname <id> <name>", "");
        continue;
      }
      
      String name = params.substring(space1 + 1);
      name.trim();
      
      if (name.length() == 0) {
        bot.sendMessage(chat_id, "ต้องระบุชื่ออุปกรณ์", "");
        continue;
      }
      
      // สร้าง JSON สำหรับ MQTT
      StaticJsonDocument<300> doc;
      doc["command"] = "setname";
      doc["device_id"] = deviceIdStr2;
      doc["name"] = name;
      doc["timestamp"] = millis();
      
      // ส่งผ่าน MQTT
      String topic = String(mqtt_topic_cmd) + deviceIdStr2;
      String jsonStr;
      serializeJson(doc, jsonStr);
      
      if (mqtt.publish(topic.c_str(), jsonStr.c_str())) {
        bot.sendMessage(chat_id, "ตั้งชื่ออุปกรณ์ " + deviceIdStr2 + " เป็น: " + name, "");
        Serial.println("ส่งคำสั่ง setname ไปยัง MQTT สำเร็จ: " + jsonStr);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /setchannel ----------
    else if (text.startsWith("/setchannel")) {
      if (space1 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setchannel <id> <channel_id>", "");
        continue;
      }
      
      String channelIdStr = params.substring(space1 + 1);
      channelIdStr.trim();
      
      if (!isNumeric(channelIdStr)) {
        bot.sendMessage(chat_id, "Channel ID ต้องเป็นตัวเลข", "");
        continue;
      }
      
      unsigned long channelId = channelIdStr.toInt();
      
      // สร้าง JSON สำหรับ MQTT
      StaticJsonDocument<200> doc;
      doc["command"] = "setchannel";
      doc["device_id"] = deviceIdStr2;
      doc["channel_id"] = channelId;
      doc["timestamp"] = millis();
      
      // ส่งผ่าน MQTT
      String topic = String(mqtt_topic_cmd) + deviceIdStr2;
      String jsonStr;
      serializeJson(doc, jsonStr);
      
      if (mqtt.publish(topic.c_str(), jsonStr.c_str())) {
        bot.sendMessage(chat_id, "ตั้งค่า ThingSpeak Channel ID สำหรับอุปกรณ์ " + deviceIdStr2 + " เป็น " + channelIdStr, "");
        Serial.println("ส่งคำสั่ง setchannel ไปยัง MQTT สำเร็จ: " + jsonStr);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /setwritekey และ /setreadkey ----------
    else if (text.startsWith("/setwritekey") || text.startsWith("/setreadkey")) {
      bool isWriteKey = text.startsWith("/setwritekey");
      String commandName = isWriteKey ? "setwritekey" : "setreadkey";
      String displayName = isWriteKey ? "Write API Key" : "Read API Key";
      
      if (space1 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /" + commandName + " <id> <api_key>", "");
        continue;
      }
      
      String apiKey = params.substring(space1 + 1);
      apiKey.trim();
      
      if (apiKey.length() == 0) {
        bot.sendMessage(chat_id, "ต้องระบุ " + displayName, "");
        continue;
      }
      
      // สร้าง JSON สำหรับ MQTT
      StaticJsonDocument<200> doc;
      doc["command"] = commandName;
      doc["device_id"] = deviceIdStr2;
      doc["api_key"] = apiKey;
      doc["timestamp"] = millis();
      
      // ส่งผ่าน MQTT
      String topic = String(mqtt_topic_cmd) + deviceIdStr2;
      String jsonStr;
      serializeJson(doc, jsonStr);
      
      if (mqtt.publish(topic.c_str(), jsonStr.c_str())) {
        bot.sendMessage(chat_id, "ตั้งค่า " + displayName + " สำหรับอุปกรณ์ " + deviceIdStr2 + " เรียบร้อย", "");
        Serial.println("ส่งคำสั่ง " + commandName + " ไปยัง MQTT สำเร็จ: " + jsonStr);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }

    // ---------- คำสั่ง /info ----------
    else if (text.startsWith("/info")) {
      if (space1 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /info <id> <secret>", "");
        continue;
      }
      
      String secretParams = params.substring(space1 + 1);
      secretParams.trim();
      
      if (secretParams.length() == 0) {
        bot.sendMessage(chat_id, "ต้องระบุรหัสลับสำหรับเรียกดูข้อมูล", "");
        continue;
      }
      
      // สร้าง JSON สำหรับ MQTT
      StaticJsonDocument<200> doc;
      doc["command"] = "info";
      doc["device_id"] = deviceIdStr2;
      doc["secret"] = secretParams;
      doc["timestamp"] = millis();
      
      // ส่งผ่าน MQTT
      String topic = String(mqtt_topic_cmd) + deviceIdStr2;
      String jsonStr;
      serializeJson(doc, jsonStr);
      
      if (mqtt.publish(topic.c_str(), jsonStr.c_str())) {
        bot.sendMessage(chat_id, "กำลังเรียกข้อมูลของอุปกรณ์ " + deviceIdStr2, "");
        Serial.println("ส่งคำสั่ง info ไปยัง MQTT สำเร็จ: " + jsonStr);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // --------------- คำสั่งจัดการ ChatID ที่ต้องตรวจสอบ ESP32 Device ID ก่อน ---------------
    
    // ---------- คำสั่ง /addchatid ----------
    else if (text.startsWith("/addchatid")) {
      // ตรวจสอบ device ID ก่อนทำคำสั่ง
      if (!checkDeviceId(deviceIdStr2)) {
        bot.sendMessage(chat_id, "Device ID ไม่ถูกต้อง สำหรับ ESP32 Bridge นี้", "");
        continue;
      }
      
      if (space1 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /addchatid <esp32_id> <chat_id>", "");
        continue;
      }
      
      String newChatId = params.substring(space1 + 1);
      newChatId.trim();
      
      if (!isNumeric(newChatId)) {
        bot.sendMessage(chat_id, "Chat ID ต้องเป็นตัวเลข", "");
        continue;
      }
      
      // ตรวจสอบว่า Chat ID นี้มีอยู่แล้วหรือไม่
      bool alreadyExists = false;
      for (int j = 0; j < numAuthorizedChatIds; j++) {
        if (authorizedChatIds[j] == newChatId) {
          alreadyExists = true;
          break;
        }
      }
      
      if (alreadyExists) {
        bot.sendMessage(chat_id, "Chat ID นี้มีอยู่แล้ว", "");
        continue;
      }
      
      if (numAuthorizedChatIds >= MAX_ALLOWED_CHATIDS) {
        bot.sendMessage(chat_id, "จำนวน Chat ID เต็มแล้ว (สูงสุด " + String(MAX_ALLOWED_CHATIDS) + " IDs)", "");
        continue;
      }
      
      // เพิ่ม Chat ID ใหม่
      authorizedChatIds[numAuthorizedChatIds] = newChatId;
      numAuthorizedChatIds++;
      
      // บันทึกลง EEPROM
      saveAllowedChatIDs();
      
      int addedIndex = numAuthorizedChatIds - 1;
      String msg = "เพิ่ม Chat ID ลำดับ " + String(addedIndex + 1) + " สำเร็จ: " + newChatId;
      bot.sendMessage(chat_id, msg, "");
      
      Serial.println("เพิ่ม Chat ID: " + newChatId + " สำเร็จ");
    }
    // ---------- คำสั่ง /updatechatid ----------
    else if (text.startsWith("/updatechatid")) {
      // ตรวจสอบ device ID ก่อนทำคำสั่ง
      if (!checkDeviceId(deviceIdStr2)) {
        bot.sendMessage(chat_id, "Device ID ไม่ถูกต้อง สำหรับ ESP32 Bridge นี้", "");
        continue;
      }
      
      if (space1 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /updatechatid <esp32_id> <index(3-5)> <old_chatid> <new_chatid>", "");
        continue;
      }
      
      String restParams = params.substring(space1 + 1);
      int space2 = restParams.indexOf(' ');
      int space3 = restParams.indexOf(' ', space2 + 1);
      
      if (space2 == -1 || space3 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /updatechatid <esp32_id> <index(3-5)> <old_chatid> <new_chatid>", "");
        continue;
      }
      
      String indexStr = restParams.substring(0, space2);
      String oldChatId = restParams.substring(space2 + 1, space3);
      String newChatId = restParams.substring(space3 + 1);
      
      indexStr.trim();
      oldChatId.trim();
      newChatId.trim();
      
      if (!isNumeric(indexStr) || !isNumeric(oldChatId) || !isNumeric(newChatId)) {
        bot.sendMessage(chat_id, "ค่าที่ป้อนต้องเป็นตัวเลข", "");
        continue;
      }
      
      int idx = indexStr.toInt();
      
      // เช็คว่า index ที่ระบุอยู่ในช่วง 3-5 เท่านั้น
      if (idx < 3 || idx > MAX_ALLOWED_CHATIDS) {
        bot.sendMessage(chat_id, "Index ต้องอยู่ระหว่าง 3-" + String(MAX_ALLOWED_CHATIDS), "");
        continue;
      }
      
      int arrayIndex = idx - 1;
      
      if (arrayIndex >= numAuthorizedChatIds) {
        bot.sendMessage(chat_id, "ไม่มี Chat ID ในตำแหน่งที่ระบุ", "");
        continue;
      }
      
      if (authorizedChatIds[arrayIndex] != oldChatId) {
        bot.sendMessage(chat_id, "ไม่สามารถอัปเดตได้: Chat ID เดิมไม่ตรงกับที่เก็บไว้", "");
        continue;
      }
      
      // อัปเดต Chat ID
      authorizedChatIds[arrayIndex] = newChatId;
      
      // บันทึกลง EEPROM
      saveAllowedChatIDs();
      
      bot.sendMessage(chat_id, "อัปเดต Chat ID ลำดับ " + String(idx) + " เป็น " + newChatId + " สำเร็จ", "");
      Serial.println("อัปเดต Chat ID ลำดับ " + String(idx) + " จาก " + oldChatId + " เป็น " + newChatId);
    }

    // ---------- คำสั่ง /removechatid ----------
    else if (text.startsWith("/removechatid")) {
      // ตรวจสอบ device ID ก่อนทำคำสั่ง
      if (!checkDeviceId(deviceIdStr2)) {
        bot.sendMessage(chat_id, "Device ID ไม่ถูกต้อง สำหรับ ESP32 Bridge นี้", "");
        continue;
      }
      
      if (space1 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /removechatid <esp32_id> <index(3-5)> <old_id>", "");
        continue;
      }
      
      String restParams = params.substring(space1 + 1);
      int space2 = restParams.indexOf(' ');
      
      if (space2 == -1) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /removechatid <esp32_id> <index(3-5)> <old_id>", "");
        continue;
      }
      
      String indexStr = restParams.substring(0, space2);
      String oldChatId = restParams.substring(space2 + 1);
      
      indexStr.trim();
      oldChatId.trim();
      
      if (!isNumeric(indexStr) || !isNumeric(oldChatId)) {
        bot.sendMessage(chat_id, "Index และ Chat ID ต้องเป็นตัวเลข", "");
        continue;
      }
      
      int idx = indexStr.toInt();
      
      // เช็คว่า index ที่ระบุอยู่ในช่วง 3-5 เท่านั้น
      if (idx < 3 || idx > MAX_ALLOWED_CHATIDS) {
        bot.sendMessage(chat_id, "Index ต้องอยู่ระหว่าง 3-" + String(MAX_ALLOWED_CHATIDS), "");
        continue;
      }
      
      int arrayIndex = idx - 1;
      
      if (arrayIndex >= numAuthorizedChatIds) {
        bot.sendMessage(chat_id, "ไม่มี Chat ID ในตำแหน่งที่ระบุ", "");
        continue;
      }
      
      // ตรวจสอบว่า old_id ตรงกับที่เก็บไว้หรือไม่
      if (authorizedChatIds[arrayIndex] != oldChatId) {
        bot.sendMessage(chat_id, "ไม่สามารถลบได้: Chat ID ที่ระบุไม่ตรงกับที่เก็บไว้", "");
        continue;
      }
      
      String removedChatId = authorizedChatIds[arrayIndex];
      
      // ย้าย Chat IDs ที่เหลือมาแทนที่
      for (int j = arrayIndex; j < numAuthorizedChatIds - 1; j++) {
        authorizedChatIds[j] = authorizedChatIds[j + 1];
      }
      
      authorizedChatIds[numAuthorizedChatIds - 1] = "";
      numAuthorizedChatIds--;
      
      // บันทึกลง EEPROM
      saveAllowedChatIDs();
      
      bot.sendMessage(chat_id, "ลบ Chat ID ลำดับ " + String(idx) + ": " + removedChatId + " สำเร็จ", "");
      Serial.println("ลบ Chat ID ลำดับ " + String(idx) + ": " + removedChatId);
    }
    
    // ---------- คำสั่ง /listchatids ----------
    else if (text.startsWith("/listchatids")) {
      // ตรวจสอบ device ID ก่อนทำคำสั่ง
      if (!checkDeviceId(deviceIdStr2)) {
        bot.sendMessage(chat_id, "Device ID ไม่ถูกต้อง สำหรับ ESP32 Bridge นี้", "");
        continue;
      }
      
      String msg = "รายการ Chat IDs ที่อนุญาต:\n";
      
      for (int j = 0; j < numAuthorizedChatIds; j++) {
        msg += String(j + 1) + ": " + authorizedChatIds[j] + "\n";
      }
      
      bot.sendMessage(chat_id, msg, "");
    }
    
    // ---------- คำสั่ง /help หรือกรณีไม่รู้จักคำสั่ง ----------
    else if (text.startsWith("/help")) {
      bot.sendMessage(chat_id, HELP_MESSAGE, "");
    } else {
      bot.sendMessage(chat_id, "ไม่รู้จักคำสั่งนี้\nพิมพ์ /help เพื่อดูรายการคำสั่งทั้งหมด", "");
    }
  }
}

// ---- เพิ่มฟังก์ชันเหล่านี้ ----
void EEPROMWritelong(int address, long value) {
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);

  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
  EEPROM.commit();
}

long EEPROMReadlong(int address) {
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);

  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

void saveAllowedChatIDs() {
  EEPROM.write(EEPROM_ADDR_NUM_CHATS, numAuthorizedChatIds);
  
  for (int i = 0; i < numAuthorizedChatIds && i < MAX_ALLOWED_CHATIDS; i++) {
    unsigned long chatId = authorizedChatIds[i].toInt();
    int addr = (i == 0) ? EEPROM_ADDR_CHATID_0 :
              (i == 1) ? EEPROM_ADDR_CHATID_1 :
              (i == 2) ? EEPROM_ADDR_CHATID_2 :
              (i == 3) ? EEPROM_ADDR_CHATID_3 : EEPROM_ADDR_CHATID_4;
    
    EEPROMWritelong(addr, chatId);
  }
}

void loadAllowedChatIDs() {
  numAuthorizedChatIds = EEPROM.read(EEPROM_ADDR_NUM_CHATS);
  if (numAuthorizedChatIds < 2 || numAuthorizedChatIds > MAX_ALLOWED_CHATIDS) {
    // ข้อมูลไม่ถูกต้องหรือยังไม่เคยบันทึก ให้ใช้ค่าเริ่มต้น
    numAuthorizedChatIds = 2;
    authorizedChatIds[0] = "32971348";
    authorizedChatIds[1] = "25340254";
    saveAllowedChatIDs();
    return;
  }
  
  for (int i = 0; i < numAuthorizedChatIds; i++) {
    int addr = (i == 0) ? EEPROM_ADDR_CHATID_0 :
              (i == 1) ? EEPROM_ADDR_CHATID_1 :
              (i == 2) ? EEPROM_ADDR_CHATID_2 :
              (i == 3) ? EEPROM_ADDR_CHATID_3 : EEPROM_ADDR_CHATID_4;
    
    unsigned long chatId = EEPROMReadlong(addr);
    if (chatId != 0xFFFFFFFF) {
      authorizedChatIds[i] = String(chatId);
    } else {
      authorizedChatIds[i] = "";
    }
  }
}

void blink() {
  static bool ledState = false;
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState);
}