#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>   // Universal Telegram Bot Library written by Brian Lough: https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <time.h>
#include "ogosense_mqtt.h"



// ตั้งค่า NTP สำหรับเวลา
const char *ntp_server = "pool.ntp.org";  // NTP server
const long gmt_offset_sec = 25200;  // +7 ชั่วโมงเป็นวินาที (ปรับตามเขตเวลาของคุณ)
const int daylight_offset_sec = 0;  // ชดเชยเวลาออมแสง

// กำหนดค่าตัวแปรสำหรับตรวจสอบข้อความใหม่จาก Telegram
unsigned long bot_lasttime;
const unsigned long BOT_MTBS = 1000;  // เวลาในการตรวจสอบข้อความใหม่ (1 วินาที)

// สร้าง client สำหรับเชื่อมต่อ MQTT แบบ TLS/SSL
BearSSL::WiFiClientSecure mqtt_ssl_client;
PubSubClient mqtt_client(mqtt_ssl_client);


// Function declarations
void connectToWiFi();
void syncTime();
void connectToMQTT();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void handleNewMessages(int numNewMessages);

void setup() {
  // เริ่มต้น Serial สำหรับ debug
  Serial.begin(115200);
  delay(100);
  Serial.println("");

  
  // เชื่อมต่อ WiFi
  connectToWiFi();
  
  // ตั้งค่าเวลาผ่าน NTP
  // syncTime();
  
  // ตั้งค่า Telegram client 
  configTime(0, 0, "pool.ntp.org");
  clientSecure.setTrustAnchors(&cert);
  
  
  // ตั้งค่า MQTT
  BearSSL::X509List serverTrustedCA(ca_cert);
  mqtt_ssl_client.setTrustAnchors(&serverTrustedCA);
  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  // เชื่อมต่อ MQTT
  connectToMQTT();
  

  // กำหนดเวลาเริ่มต้นสำหรับการตรวจสอบข้อความ Telegram
  bot_lasttime = millis();
  
  
}

void loop() {
  
  // ตรวจสอบการเชื่อมต่อ MQTT และเชื่อมต่อใหม่ถ้าจำเป็น
  if (!mqtt_client.connected()) {
    connectToMQTT();
  }
  mqtt_client.loop();
  
  
  // ตรวจสอบข้อความใหม่จาก Telegram ทุกๆ BOT_MTBS วินาที
  if (millis() > bot_lasttime + BOT_MTBS) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      Serial.println("got response");
      yield();
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
  Serial.println("");
  Serial.println("เชื่อมต่อ WiFi สำเร็จ");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void syncTime() {
  configTime(gmt_offset_sec, daylight_offset_sec, ntp_server);
  Serial.print("รอซิงค์เวลาจาก NTP: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.println(asctime(&timeinfo));
}

void connectToMQTT() {
  int retries = 0;
  while (!mqtt_client.connected() && retries < 5) {
    String client_id = "esp8266-telegram-" + String(WiFi.macAddress());
    Serial.print("กำลังเชื่อมต่อกับ MQTT broker (");
    Serial.print(client_id);
    Serial.print(")...");
    
    // พยายามเชื่อมต่อด้วย username และ password
    if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("เชื่อมต่อสำเร็จ");
      // สมัครสมาชิกเพื่อรับข้อความจาก topic (ตัวอย่าง)
      // mqtt_client.subscribe("response/devices/#");
    } else {
      char err_buf[128];
      mqtt_ssl_client.getLastSSLError(err_buf, sizeof(err_buf));
      Serial.print("เชื่อมต่อล้มเหลว, rc=");
      Serial.print(mqtt_client.state());
      Serial.print(" SSL error: ");
      Serial.println(err_buf);
      retries++;
      delay(5000);
    }
  }
  
  if (retries >= 5) {
    Serial.println("ไม่สามารถเชื่อมต่อกับ MQTT broker ได้หลังจากลองหลายครั้ง");
    Serial.println("กรุณาตรวจสอบการตั้งค่าและ restart อุปกรณ์");
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.print("ได้รับข้อความจาก topic: ");
  Serial.print(topic);
  Serial.print(" ข้อความ: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char) payload[i]);
  }
  Serial.println();
  
  // สามารถแปลงข้อความเป็น JSON และส่งกลับไปยัง Telegram ได้ที่นี่
  // ตัวอย่าง:
  // StaticJsonDocument<200> doc;
  // deserializeJson(doc, payload, length);
  // String message = doc["message"];
  // bot.sendMessage("CHAT_ID", message, "");
}

// ฟังก์ชันจัดการข้อความใหม่จาก Telegram
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    
    // Add this authentication check
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }
    
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;
    
    Serial.println("ได้รับข้อความ: " + text + " จาก " + from_name);
    
    // Add more command options similar to the working code
    if (text == "/start") {
      String welcome = "Welcome, " + from_name + ".\n";
      welcome += "Use the following commands:\n\n";
      welcome += "/c <device_id> to check device status\n";
      bot.sendMessage(chat_id, welcome, "");
    }
    // ตรวจสอบคำสั่ง /c (เปลี่ยนจาก /status)
    else if (text.startsWith("/c")) {
      // แยก device_id จากข้อความ
      String device_id = "";
      if (text.indexOf(" ") > 0) {
        device_id = text.substring(text.indexOf(" ") + 1);
        device_id.trim();
        
        if (device_id != "") {
          // สร้าง JSON สำหรับส่งไปยัง MQTT
          StaticJsonDocument<200> doc;
          doc["command"] = "status";
          doc["device_id"] = device_id;
          doc["timestamp"] = millis();
          
          // แปลง JSON เป็น String
          String jsonString;
          serializeJson(doc, jsonString);
          
          // ส่งข้อความไปยัง MQTT
          String topic = String(mqtt_topic) + "/" + device_id;
          if (mqtt_client.publish(topic.c_str(), jsonString.c_str())) {
            String response = "ส่งคำสั่งตรวจสอบสถานะไปยังอุปกรณ์ " + device_id + " แล้ว";
            bot.sendMessage(chat_id, response, "");
            Serial.println("ส่งข้อความไปยัง MQTT สำเร็จ: " + jsonString);
          } else {
            bot.sendMessage(chat_id, "เกิดข้อผิดพลาดในการส่งคำสั่งไปยัง MQTT", "");
            Serial.println("ส่งข้อความไปยัง MQTT ล้มเหลว");
          }
        } else {
          bot.sendMessage(chat_id, "กรุณาระบุ device_id เช่น /c device1", "");
        }
      } else {
        bot.sendMessage(chat_id, "กรุณาระบุ device_id เช่น /c device1", "");
      }
    }
    // คุณสามารถเพิ่มเติมคำสั่งอื่นๆ ได้ที่นี่
  }
}