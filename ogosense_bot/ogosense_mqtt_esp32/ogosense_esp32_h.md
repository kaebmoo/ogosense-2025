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
// ogosense_esp32.h

const int DEVICE_ID = xxxx;

// ตั้งค่า WiFi
const char *ssid = "xxxx";           // แทนที่ด้วยชื่อ WiFi ของคุณ
const char *password = "xxxx";   // แทนที่ด้วยรหัสผ่าน WiFi ของคุณ

// ตั้งค่า Telegram Bot
#define BOT_TOKEN "x:xxx"    // แทนที่ด้วย Token ของ Telegram Bot
#define CHAT_ID "xxxxxx"                 // แทนที่ด้วย Chat ID ของคุณ (ถ้าต้องการจำกัดผู้ใช้)

// ตรวจสอบ Chat IDs ที่อนุญาต
#define MAX_ALLOWED_CHATIDS 5
String authorizedChatIds[MAX_ALLOWED_CHATIDS] = {"xxxxxx", "xxxxxx", "", "", ""};
int numAuthorizedChatIds = 2; // จำนวน Chat IDs ที่อนุญาต (เริ่มต้นที่ 2)

// ในโค้ด handleNewMessages เมื่อส่งคำสั่ง MQTT
// ให้บันทึกข้อมูลว่าใคร (chat_id) ส่งคำสั่งอะไรไปยังอุปกรณ์ไหน

// ตัวอย่างโครงสร้างข้อมูลสำหรับเก็บข้อมูลคำสั่งล่าสุด
#define MAX_COMMAND_HISTORY 20

struct CommandInfo {
  String deviceId;
  String command;
  String chatId;
  unsigned long timestamp;
};

CommandInfo commandHistory[MAX_COMMAND_HISTORY];
int commandHistoryIndex = 0;


const char* mqtt_broker = "i31286ee.ala.eu-central-1.emqxsl.com";
const int mqtt_port = 8883;
const char* mqtt_username = "xxxx";
const char* mqtt_password = "xxxx";
const char* mqtt_topic = "command/devices";      // เดิมที่มี
const char* mqtt_topic_cmd = "ogosense/cmd/";    // เพิ่มใหม่
const char* mqtt_topic_resp = "ogosense/resp/";  // เพิ่มใหม่

// DigitCert Global Root CA certificate
const char ca_cert[] PROGMEM = R"EOF(
    -----BEGIN CERTIFICATE-----
    
    -----END CERTIFICATE-----
    )EOF";