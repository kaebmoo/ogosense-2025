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
 * SHT30 Shield
 * Relay Shield
 *
 *
 */

 // ogosense_mqtt_esp8266.h

 #define THINGSPEAK

// กำหนดค่าสำหรับแต่ละอุปกรณ์
#define DEVICE_CLINIC_SUPPLY 1
#define DEVICE_CLINIC_SALINE 2
#define DEVICE_CLINIC_REAGENT 3
#define DEVICE_CLINIC_SURIYAPHONG 4

// เลือกประเภทอุปกรณ์ที่จะใช้ (แก้ไขเพียงบรรทัดนี้เมื่อต้องการเปลี่ยนอุปกรณ์)
#define DEVICE_TYPE DEVICE_CLINIC_SALINE

#define DEVICE_NAME_MAX_BYTES   201

// ===== ThingSpeak & Communication Settings =====
// ThingSpeak สำหรับอุปกรณ์นี้ (แต่ละเครื่องมี channel ของตัวเอง)
// https://thingspeak.mathworks.com/
#ifdef THINGSPEAK
  // ThingSpeak information
  const char thingSpeakAddress[] = "api.thingspeak.com";
  #if DEVICE_TYPE == DEVICE_CLINIC_SUPPLY
    unsigned long channelID = 4444;
    char readAPIKey[] = "zzzzz";
    char writeAPIKey[] = "zzzz";
    char deviceName[DEVICE_NAME_MAX_BYTES + 1] = "ห้องเก็บซัพพลายคลินิก";
    // ===== Device Settings =====
    // หมายเลขเครื่อง (ปรับให้ตรงกับเครื่องนี้)
    const int DEVICE_ID = 44;  // ห้องเก็บซัพพลายคลินิก
  #elif DEVICE_TYPE == DEVICE_CLINIC_SALINE
    unsigned long channelID = 5555;
    char readAPIKey[] = "zzzz";
    char writeAPIKey[] = "zzzz";
    char deviceName[DEVICE_NAME_MAX_BYTES + 1] = "ห้องเก็บน้ำเกลือคลินิก";
    const int DEVICE_ID = 5555;  // ห้องเก็บน้ำเกลือคลินิก
  #elif DEVICE_TYPE == DEVICE_CLINIC_REAGENT
    unsigned long channelID = 2222;
    char readAPIKey[] = "xxxx";
    char writeAPIKey[] = "xxxx";
    char deviceName[DEVICE_NAME_MAX_BYTES + 1] = "ห้องเก็บน้ำยาคลินิก";
    const int DEVICE_ID = 2222;  // ห้องเก็บน้ำยาคลินิก
  #elif DEVICE_TYPE == DEVICE_CLINIC_SURIYAPHONG
    unsigned long channelID = 3333;
    char readAPIKey[] = "xxxx";
    char writeAPIKey[] = "xxxx";
    char deviceName[DEVICE_NAME_MAX_BYTES + 1] = "ห้องเก็บซัพพลาย น้ำเกลือ";
    const int DEVICE_ID = 33333;  // ห้องเก็บซัพพลาย น้ำเกลือ 

#endif

// กำหนด secret code สำหรับ /info (ค่าคงที่)
const char* INFO_SECRET = "xxxx";

// เนื่องจาก telegram_token และ device_id เป็นค่าคงที่ จึงไม่ต้องมีตัวแปรเก็บค่าชั่วคราว
// ตัวแปรสำหรับเก็บค่าพารามิเตอร์ชั่วคราว (ใช้ใน WiFiManager)
char temp_deviceName[DEVICE_NAME_MAX_BYTES + 1] = "";
char temp_channelID[10] = "";
char temp_writeAPIKey[20] = "";
char temp_readAPIKey[20] = "";

// อาร์เรย์ชั่วคราวสำหรับเก็บคำสั่ง
char auth[] = "XXXXXXXXXXed4061bb4e0dXXXXXXXXXX";

#define EEPROM_SIZE 512

// EEPROM Address Mapping
#define EEPROM_ADDR_HIGH_HUMIDITY     0   // 4 bytes
#define EEPROM_ADDR_LOW_HUMIDITY      4   // 4 bytes
#define EEPROM_ADDR_HIGH_TEMP         8   // 4 bytes
#define EEPROM_ADDR_LOW_TEMP         12   // 4 bytes
#define EEPROM_ADDR_OPTIONS          16   // 4 bytes (int)
#define EEPROM_ADDR_COOL             20   // 4 bytes (int)
#define EEPROM_ADDR_MOISTURE         24   // 4 bytes (int)
#define EEPROM_ADDR_WRITE_APIKEY     28   // 16 bytes
#define EEPROM_ADDR_READ_APIKEY      44   // 16 bytes
#define EEPROM_ADDR_AUTH             60   // 32 bytes
#define EEPROM_ADDR_CHANNEL_ID       92   // 4 bytes
#define EEPROM_ADDR_DEVICE_NAME     96   // 201 bytes (UTF-8 with Thai, safe length)
#define EEPROM_ADDR_VERSION_MARK   500   // 4 bytes (for config version marker or flag)

#ifndef HELP_MESSAGE_H
#define HELP_MESSAGE_H

const char HELP_MESSAGE[] PROGMEM = R"rawliteral(
Available Commands (คำสั่งที่สามารถใช้ได้):
/start - เริ่มต้นใช้งานอุปกรณ์
/status <id> - ตรวจสอบสถานะอุปกรณ์
/settemp <id> <lowTemp> <highTemp> - ตั้งค่าขอบเขต Temperature
/sethum <id> <lowHumidity> <highHumidity> - ตั้งค่าขอบเขต Humidity
/setmode <id> <auto/manual> - ตั้งค่าโหมด Auto หรือ Manual
/setoption <id> <0-4> - ตั้งค่าโหมดควบคุม (Option)
/relay <id> <0/1> - สั่งเปิด/ปิด Relay (Manual เท่านั้น)
/setname <id> <name> - เปลี่ยนชื่ออุปกรณ์
/setchannel <id> <channel_id> - ตั้งค่า ThingSpeak Channel ID
/setwritekey <id> <api_key> - ตั้งค่า ThingSpeak Write API Key
/setreadkey <id> <api_key> - ตั้งค่า ThingSpeak Read API Key
/info <id> <secret> - แสดงข้อมูลอุปกรณ์ (Device Info)
/help <id> - แสดงรายการคำสั่งทั้งหมด
)rawliteral";

#endif

// MQTT settings
const char* mqtt_server = "i31286ee.ala.eu-central-1.emqxsl.com";
const int mqtt_port = 8883;
const char* mqtt_username = "xxx";
const char* mqtt_password = "xxxx";
const char* mqtt_topic_cmd = "xxx/cmd/";
const char* mqtt_topic_resp = "xxx/resp/";

// DigiCert Global Root CA certificate
const char ca_cert[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----

-----END CERTIFICATE-----
)EOF";
