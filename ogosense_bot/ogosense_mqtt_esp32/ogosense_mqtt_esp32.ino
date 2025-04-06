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
  mqtt.setBufferSize(1024);
  mqtt.subscribe(mqtt_topic_resp, 1);

  bot_lasttime = millis();
  blinker.attach(1.0, blink);  // เรียก blink() ทุก 1 วินาที
}

void loop() {
  
  static unsigned long lastReconnectAttempt = 0;
  
  // ตรวจสอบการเชื่อมต่อ MQTT ทุก 5 วินาที
  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      Serial.println("การเชื่อมต่อ MQTT ขาดหาย พยายามเชื่อมต่อใหม่...");
      connectToMQTT();
    }
  } else {
    mqtt.loop();
  }
  
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

  delay(1);
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
  wifiManager.setConnectTimeout(20);         // timeout 20 วินาที
  wifiManager.setConfigPortalTimeout(180);   // portal จะรอ 3 นาที

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
      Serial.println("WiFiManager: เชื่อมต่อสำเร็จ");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
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
    // String client_id = "esp32-client-" + String(WiFi.macAddress()); // REDACTED
    String client_id = "esp32-telegram-broker-" + String(DEVICE_ID); // REDACTED
    Serial.print("เชื่อมต่อ MQTT (client_id: ");
    Serial.print(client_id);
    Serial.print(")... ");

    if (mqtt.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("สำเร็จ");
      // เมื่อเชื่อมต่อสำเร็จ ต้อง subscribe topic ใหม่อีกครั้ง
      mqtt.subscribe(mqtt_topic_resp, 1);
      Serial.println("Subscribe topic: " + String(mqtt_topic_resp));
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
    // แยก device ID จาก topic
    String deviceId = topicStr.substring(14); // ตัด "ogosense/resp/" ออก
    
    // แปลง payload เป็น JSON
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.print("deserializeJson() ล้มเหลว: ");
      Serial.println(error.c_str());
      return;
    }
    
    // ตรวจสอบว่ามี device_id จาก payload ด้วยหรือไม่
    if (doc.containsKey("device_id")) {
      deviceId = doc["device_id"].as<String>();
    }
    
    // ตรวจสอบคำสั่งที่ได้รับตอบกลับ
    if (doc.containsKey("command")) {
      String command = doc["command"].as<String>();
      bool success = doc.containsKey("success") ? doc["success"].as<bool>() : false;
      
      // สร้างข้อความตอบกลับไปยัง Telegram
      String telegramResponse = "การตอบกลับจากอุปกรณ์ " + deviceId + "\n";
      
      // ตรวจสอบสถานะความสำเร็จ
      if (success) {
        telegramResponse += "สถานะ: สำเร็จ\n";
      } else {
        telegramResponse += "สถานะ: ไม่สำเร็จ\n";
        if (doc.containsKey("message")) {
          telegramResponse += "ข้อความ: " + doc["message"].as<String>() + "\n";
        }
      }
      
      // เพิ่มข้อมูลตามประเภทคำสั่ง
      if (command == "status" && doc.containsKey("data")) {
        if (doc["data"].containsKey("temperature")) 
          telegramResponse += "อุณหภูมิ: " + String(doc["data"]["temperature"].as<float>()) + " °C\n";
        
        if (doc["data"].containsKey("humidity")) 
          telegramResponse += "ความชื้น: " + String(doc["data"]["humidity"].as<float>()) + " %\n";
        
        if (doc["data"].containsKey("relay")) 
          telegramResponse += "สถานะ Relay: " + String(doc["data"]["relay"].as<bool>() ? "เปิด" : "ปิด") + "\n";
        
        if (doc["data"].containsKey("mode")) 
          telegramResponse += "โหมด: " + doc["data"]["mode"].as<String>() + "\n";
          
        if (doc["data"].containsKey("name")) 
          telegramResponse += "ชื่ออุปกรณ์: " + doc["data"]["name"].as<String>() + "\n";
          
        if (doc["data"].containsKey("option")) {
          int option = doc["data"]["option"].as<int>();
          String optionText = 
            (option == 0) ? "Humidity only" :
            (option == 1) ? "Temperature only" :
            (option == 2) ? "Temperature & Humidity" :
            (option == 3) ? "Soil Moisture mode" :
            (option == 4) ? "Additional mode" : "Unknown";
          telegramResponse += "ตัวเลือก: " + optionText + "\n";
        }
      }
      else if ((command == "settemp" || command == "sethum") && doc.containsKey("data")) {
        if (doc["data"].containsKey("low") && doc["data"].containsKey("high")) {
          float low = doc["data"]["low"].as<float>();
          float high = doc["data"]["high"].as<float>();
          
          if (command == "settemp") {
            telegramResponse += "ตั้งค่าอุณหภูมิ:\n";
            telegramResponse += "ต่ำสุด: " + String(low) + " °C\n";
            telegramResponse += "สูงสุด: " + String(high) + " °C\n";
          } else {
            telegramResponse += "ตั้งค่าความชื้น:\n";
            telegramResponse += "ต่ำสุด: " + String(low) + " %\n";
            telegramResponse += "สูงสุด: " + String(high) + " %\n";
          }
        }
      }
      else if (command == "setmode" && doc.containsKey("data")) {
        if (doc["data"].containsKey("mode")) {
          telegramResponse += "ตั้งค่าโหมด: " + doc["data"]["mode"].as<String>() + "\n";
        }
      }
      else if (command == "setoption" && doc.containsKey("data")) {
        if (doc["data"].containsKey("option")) {
          telegramResponse += "ตั้งค่าตัวเลือก: " + String(doc["data"]["option"].as<int>()) + "\n";
        }
      }
      else if (command == "relay" && doc.containsKey("data")) {
        if (doc["data"].containsKey("relay")) {
          telegramResponse += "ตั้งค่า Relay: " + String(doc["data"]["relay"].as<bool>() ? "เปิด" : "ปิด") + "\n";
        }
      }
      else if (command == "setname" && doc.containsKey("data")) {
        if (doc["data"].containsKey("name")) {
          telegramResponse += "ตั้งชื่ออุปกรณ์: " + doc["data"]["name"].as<String>() + "\n";
        }
      }
      else if (command == "setchannel" && doc.containsKey("data")) {
        if (doc["data"].containsKey("channel_id")) {
          telegramResponse += "ตั้งค่า ThingSpeak Channel ID: " + String(doc["data"]["channel_id"].as<unsigned long>()) + "\n";
        }
      }
      else if (command == "setwritekey" || command == "setreadkey") {
        String keyType = (command == "setwritekey") ? "Write API Key" : "Read API Key";
        telegramResponse += "ตั้งค่า " + keyType + " สำเร็จ\n";
      }
      else if (command == "info" && doc.containsKey("data")) {
        telegramResponse += "ข้อมูลอุปกรณ์:\n";
        
        if (doc["data"].containsKey("name"))
          telegramResponse += "ชื่อ: " + doc["data"]["name"].as<String>() + "\n";
        
        if (doc["data"].containsKey("device_id"))
          telegramResponse += "Device ID: " + doc["data"]["device_id"].as<String>() + "\n";
        
        if (doc["data"].containsKey("temp_low") && doc["data"].containsKey("temp_high"))
          telegramResponse += "อุณหภูมิ: " + String(doc["data"]["temp_low"].as<float>()) + "-" + 
                              String(doc["data"]["temp_high"].as<float>()) + " °C\n";
        
        if (doc["data"].containsKey("humidity_low") && doc["data"].containsKey("humidity_high"))
          telegramResponse += "ความชื้น: " + String(doc["data"]["humidity_low"].as<float>()) + "-" + 
                              String(doc["data"]["humidity_high"].as<float>()) + " %\n";
        
        if (doc["data"].containsKey("mode"))
          telegramResponse += "โหมด: " + doc["data"]["mode"].as<String>() + "\n";
        
        if (doc["data"].containsKey("option")) {
          int option = doc["data"]["option"].as<int>();
          String optionText = 
            (option == 0) ? "Humidity only" :
            (option == 1) ? "Temperature only" :
            (option == 2) ? "Temperature & Humidity" :
            (option == 3) ? "Soil Moisture mode" :
            (option == 4) ? "Additional mode" : "Unknown";
          telegramResponse += "ตัวเลือก: " + optionText + "\n";
        }
        
        if (doc["data"].containsKey("cool")) {
          bool coolMode = doc["data"]["cool"].as<bool>();
          String coolText = (coolMode) ? 
            "COOL mode: Relay ON เมื่อ Temp >= High" : 
            "HEAT mode: Relay ON เมื่อ Temp <= Low";
          telegramResponse += "โหมดทำความเย็น: " + coolText + "\n";
        }
        
        if (doc["data"].containsKey("moisture")) {
          bool moistureMode = doc["data"]["moisture"].as<bool>();
          String moistureText = (moistureMode) ? 
            "Moisture mode: Relay ON เมื่อ Humidity <= Low" : 
            "Dehumidifier mode: Relay ON เมื่อ Humidity >= High";
          telegramResponse += "โหมดความชื้น: " + moistureText + "\n";
        }
        
        if (doc["data"].containsKey("thingspeak_channel"))
          telegramResponse += "ThingSpeak Channel: " + String(doc["data"]["thingspeak_channel"].as<unsigned long>()) + "\n";
        
        // แสดง write_api_key แบบปกปิดบางส่วน
        if (doc["data"].containsKey("write_api_key")) {
          String apiKey = doc["data"]["write_api_key"].as<String>();
          String maskedKey = "";
          
          // แสดงแค่ 4 ตัวแรกและปกปิดที่เหลือด้วย *
          if (apiKey.length() > 4) {
            maskedKey = apiKey.substring(0, 4) + "****"; 
          } else {
            maskedKey = apiKey;
          }
          
          telegramResponse += "ThingSpeak Write API Key: " + maskedKey + "\n";
        }
      }
      // ส่งข้อความไปยัง Telegram
      // สำหรับ chat_id เราต้องการให้ส่งกลับไปยัง chat_id ที่ส่งคำสั่งมา
      // ในที่นี้จะใช้ ADMIN_CHAT_ID เป็นตัวอย่าง (ควรเก็บ chat_id ไว้ก่อนหน้านี้)
      String chat_id = getLastChatId(deviceId, command);
      if (chat_id.length() > 0) {
        bot.sendMessage(chat_id, telegramResponse, "");
        Serial.println("ส่งข้อความไปยัง Telegram: " + telegramResponse);
      } else {
        Serial.println("ไม่พบ chat_id ที่เกี่ยวข้อง");
      }
    }
  }
}

// ฟังก์ชันค้นหา chat_id ล่าสุดที่ส่งคำสั่งไปยังอุปกรณ์
// ฟังก์ชันบันทึกคำสั่งใหม่
void recordCommand(String deviceId, String command, String chatId) {
  commandHistory[commandHistoryIndex].deviceId = deviceId;
  commandHistory[commandHistoryIndex].command = command;
  commandHistory[commandHistoryIndex].chatId = chatId;
  commandHistory[commandHistoryIndex].timestamp = millis();
  
  commandHistoryIndex = (commandHistoryIndex + 1) % MAX_COMMAND_HISTORY;
}

// ฟังก์ชันค้นหา chat_id จากคำสั่งล่าสุด
String getLastChatId(String deviceId, String command) {
  unsigned long currentTime = millis();
  unsigned long newestTime = 0;
  int bestMatch = -1;
  
  for (int i = 0; i < MAX_COMMAND_HISTORY; i++) {
    if (commandHistory[i].deviceId == deviceId && 
        commandHistory[i].command == command) {
      // หาคำสั่งล่าสุด
      if (commandHistory[i].timestamp > newestTime) {
        newestTime = commandHistory[i].timestamp;
        bestMatch = i;
      }
    }
  }
  
  if (bestMatch != -1) {
    return commandHistory[bestMatch].chatId;
  }
  
  // หากไม่พบ ใช้ chat_id แรกในรายการที่อนุญาต
  return authorizedChatIds[0];
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

// ฟังก์ชันช่วยนับจำนวนพารามิเตอร์ โดยไม่สนใจช่องว่างติดกัน
int countParams(String text) {
  text.trim(); // ตัดช่องว่างต้น-ท้าย
  
  int count = 0;
  bool inWord = false;
  
  for (unsigned int i = 0; i < text.length(); i++) {
    if (text.charAt(i) == ' ') {
      if (inWord) {
        inWord = false;
      }
    } else {
      if (!inWord) {
        count++;
        inWord = true;
      }
    }
  }
  
  return count;
}

// ฟังก์ชันช่วยสำหรับตัดช่องว่างและแยกพารามิเตอร์
String getParamAtIndex(String text, int index) {
  text.trim(); // ตัดช่องว่างต้น-ท้าย
  
  int currentIndex = 0;
  int startPos = 0;
  bool inWord = false;
  
  for (unsigned int i = 0; i <= text.length(); i++) {
    if (i == text.length() || text.charAt(i) == ' ') {
      if (inWord) {
        // เจอจุดสิ้นสุดของคำ
        if (currentIndex == index) {
          return text.substring(startPos, i);
        }
        currentIndex++;
        inWord = false;
      }
    } else {
      if (!inWord) {
        // เริ่มต้นคำใหม่
        startPos = i;
        inWord = true;
      }
    }
  }
  
  return ""; // ไม่พบพารามิเตอร์ตามตำแหน่งที่ต้องการ
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

    // แยกคำสั่งและพารามิเตอร์
    String command = text.substring(0, firstSpace);
    String paramsString = text.substring(firstSpace + 1);
    paramsString.trim(); // ตัดช่องว่างหน้าหลัง
    
    // ตรวจสอบว่ามีพารามิเตอร์เพียงพอหรือไม่
    int paramCount = countParams(paramsString);
    if (paramCount == 0) {
      bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: ต้องมี device ID", "");
      continue;
    }
    
    // ดึง device_id (พารามิเตอร์แรกเสมอ)
    String deviceIdStr = getParamAtIndex(paramsString, 0);
    
    if (!isNumeric(deviceIdStr)) {
      bot.sendMessage(chat_id, "Device ID ต้องเป็นตัวเลขเท่านั้น", "");
      continue;
    }
    
    int deviceId = deviceIdStr.toInt();
    String deviceIdStr2 = String(deviceId);  // เพื่อแน่ใจว่าเป็นตัวเลขล้วน

    // ---------- คำสั่ง /status ----------
    if (command == "/status") {
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
        recordCommand(deviceIdStr2, "status", chat_id);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    // ---------- คำสั่ง /settemp ----------
    else if (command == "/settemp") {
      if (paramCount < 3) { 
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /settemp <id> <lowTemp> <highTemp>", "");
        continue;
      }
      
      String lowTempStr = getParamAtIndex(paramsString, 1);
      String highTempStr = getParamAtIndex(paramsString, 2);
      
      if (!isNumeric(lowTempStr) || !isNumeric(highTempStr)) {
        bot.sendMessage(chat_id, "ค่าอุณหภูมิต้องเป็นตัวเลข", "");
        continue;
      }
      
      float lowTemp = lowTempStr.toFloat();
      float highTemp = highTempStr.toFloat();
      
      // ตรวจสอบขอบเขตค่า
      if (lowTemp < 0 || highTemp > 100) {
        bot.sendMessage(chat_id, "ค่าอุณหภูมิต้องอยู่ระหว่าง 0-100°C", "");
        continue;
      }
      
      // ตรวจสอบว่า lowTemp ต้องน้อยกว่า highTemp
      if (lowTemp >= highTemp) {
        bot.sendMessage(chat_id, "ค่าอุณหภูมิต่ำสุดต้องน้อยกว่าค่าอุณหภูมิสูงสุด", "");
        continue;
      }
      
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
        recordCommand(deviceIdStr2, "settemp", chat_id);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }

    // ---------- คำสั่ง /sethum ----------
    else if (command == "/sethum") {
      if (paramCount < 3) { 
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /sethum <id> <lowHumidity> <highHumidity>", "");
        continue;
      }
      
      String lowHumStr = getParamAtIndex(paramsString, 1);
      String highHumStr = getParamAtIndex(paramsString, 2);
      
      if (!isNumeric(lowHumStr) || !isNumeric(highHumStr)) {
        bot.sendMessage(chat_id, "ค่าความชื้นต้องเป็นตัวเลข", "");
        continue;
      }
      
      float lowHum = lowHumStr.toFloat();
      float highHum = highHumStr.toFloat();
      
      // ตรวจสอบขอบเขตค่า
      if (lowHum < 0 || highHum > 100) {
        bot.sendMessage(chat_id, "ค่าความชื้นต้องอยู่ระหว่าง 0-100%", "");
        continue;
      }
      
      // ตรวจสอบว่า lowHum ต้องน้อยกว่า highHum
      if (lowHum >= highHum) {
        bot.sendMessage(chat_id, "ค่าความชื้นต่ำสุดต้องน้อยกว่าค่าความชื้นสูงสุด", "");
        continue;
      }
      
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
        recordCommand(deviceIdStr2, "sethum", chat_id);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /setmode ----------
    else if (command == "/setmode") {
      if (paramCount < 2) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setmode <id> <auto/manual>", "");
        continue;
      }
      
      String modeStr = getParamAtIndex(paramsString, 1);
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
        recordCommand(deviceIdStr2, "setmode", chat_id);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /setoption ----------
    else if (command == "/setoption") {
      if (paramCount < 2) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setoption <id> <0-4>", "");
        continue;
      }
      
      String optionStr = getParamAtIndex(paramsString, 1);
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
        recordCommand(deviceIdStr2, "setoption", chat_id);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /relay ----------
    else if (command == "/relay") {
      if (paramCount < 2) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /relay <id> <0/1>", "");
        continue;
      }
      
      String stateStr = getParamAtIndex(paramsString, 1);
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
        recordCommand(deviceIdStr2, "relay", chat_id);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /setname ----------
    else if (command == "/setname") {
      if (paramCount < 2) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setname <id> <name>", "");
        continue;
      }
      
      // ในกรณีของ setname เราต้องการรวมทุกคำหลัง deviceId เป็นชื่อเดียว
      // ตัดคำสั่งและ deviceId ออกเพื่อให้เหลือแค่ name
      int cmdLength = command.length() + 1; // +1 for the space
      int deviceIdLength = deviceIdStr.length() + 1; // +1 for the space
      int startPos = text.indexOf(deviceIdStr) + deviceIdLength;
      
      String name = text.substring(startPos);
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
        recordCommand(deviceIdStr2, "setname", chat_id);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /setchannel ----------
    else if (command == "/setchannel") {
      if (paramCount < 2) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /setchannel <id> <channel_id>", "");
        continue;
      }
      
      String channelIdStr = getParamAtIndex(paramsString, 1);
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
        recordCommand(deviceIdStr2, "setchannel", chat_id);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // ---------- คำสั่ง /setwritekey และ /setreadkey ----------
    else if (command == "/setwritekey" || command == "/setreadkey") {
    
      bool isWriteKey = (command == "/setwritekey");
      String commandName = isWriteKey ? "setwritekey" : "setreadkey";
      String displayName = isWriteKey ? "Write API Key" : "Read API Key";
      
      if (paramCount < 2) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /" + commandName + " <id> <api_key>", "");
        continue;
      }
      
      // สำหรับ API key เราต้องการรวมทุกคำหลัง deviceId เป็น key เดียว
      int cmdLength = command.length() + 1; // +1 for the space
      int deviceIdLength = deviceIdStr.length() + 1; // +1 for the space
      int startPos = text.indexOf(deviceIdStr) + deviceIdLength;
      
      String apiKey = text.substring(startPos);
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
        recordCommand(deviceIdStr2, commandName, chat_id);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }

    // ---------- คำสั่ง /info ----------
    else if (command == "/info") {
      if (paramCount < 2) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /info <id> <secret>", "");
        continue;
      }
      
      String secretParams = getParamAtIndex(paramsString, 1);
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
        recordCommand(deviceIdStr2, "info", chat_id);
      } else {
        bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่ง", "");
        Serial.println("ส่งคำสั่งไปยัง MQTT ล้มเหลว");
      }
    }
    
    // --------------- คำสั่งจัดการ ChatID ที่ต้องตรวจสอบ ESP32 Device ID ก่อน ---------------
    
    // ---------- คำสั่ง /addchatid ----------
    else if (command == "/addchatid") {
      // ตรวจสอบ device ID ก่อนทำคำสั่ง
      if (!checkDeviceId(deviceIdStr2)) {
        bot.sendMessage(chat_id, "Device ID ไม่ถูกต้อง สำหรับ ESP32 Bridge นี้", "");
        continue;
      }
      
      if (paramCount < 2) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /addchatid <esp32_id> <chat_id>", "");
        continue;
      }
      
      String newChatId = getParamAtIndex(paramsString, 1);
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
    else if (command == "/updatechatid") {
      // ตรวจสอบ device ID ก่อนทำคำสั่ง
      if (!checkDeviceId(deviceIdStr2)) {
        bot.sendMessage(chat_id, "Device ID ไม่ถูกต้อง สำหรับ ESP32 Bridge นี้", "");
        continue;
      }
      
      if (paramCount < 4) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /updatechatid <esp32_id> <index(3-5)> <old_chatid> <new_chatid>", "");
        continue;
      }
      
      String indexStr = getParamAtIndex(paramsString, 1);
      String oldChatId = getParamAtIndex(paramsString, 2);
      String newChatId = getParamAtIndex(paramsString, 3);
      
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
    else if (command == "/removechatid") {
      // ตรวจสอบ device ID ก่อนทำคำสั่ง
      if (!checkDeviceId(deviceIdStr2)) {
        bot.sendMessage(chat_id, "Device ID ไม่ถูกต้อง สำหรับ ESP32 Bridge นี้", "");
        continue;
      }
      
      if (paramCount < 3) {
        bot.sendMessage(chat_id, "รูปแบบคำสั่งไม่ถูกต้อง: /removechatid <esp32_id> <index(3-5)> <old_id>", "");
        continue;
      }
      
      String indexStr = getParamAtIndex(paramsString, 1);
      String oldChatId = getParamAtIndex(paramsString, 2);
      
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
    else if (command == "/listchatids") {
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
    else if (command == "/help") {
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
    authorizedChatIds[0] = "REDACTED";
    authorizedChatIds[1] = "REDACTED";
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