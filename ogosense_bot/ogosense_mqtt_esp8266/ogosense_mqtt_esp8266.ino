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
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <WEMOS_SHT3X.h>         // ไลบรารี SHT3x สำหรับเซนเซอร์ SHT30
#include <SPI.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <umm_malloc/umm_heap_select.h>

#define MQTT_MAX_PACKET_SIZE 512
#include <PubSubClient.h>
#include <Timer.h>  //https://github.com/JChristensen/Timer
#include <Ticker.h>  //Ticker Library

#include "ogosense_mqtt_esp8266.h"

WiFiClient client;  // ใช้กับ thinkspeak
WiFiClientSecure mqttClient;
PubSubClient mqtt(mqttClient);
BearSSL::X509List mqtt_cert(ca_cert);

// NTP และเวลา
const char* ntpServer = "pool.ntp.org";

 #ifdef MATRIXLED
  #include <MLEDScroll.h>
  MLEDScroll matrix;
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
Ticker t_readSensor;
Ticker watchdogTicker;

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
void autoWifiConnect();
void setReadSensorFlag();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void getConfig();
void saveConfig();
int readSensorData();
void controlRelay();
void sendDataToThingSpeak();
void turnRelayOn();
void turnRelayOff();
void turnoff();
void delayStart();
void buzzer_sound();
void blink();
void printConfig();
bool isNumeric(const String& str);
void processCommand(StaticJsonDocument<1024>& doc);
void sendMqttResponse(const String& command, JsonDocument& response);

// EEPROM functions
void eeWriteInt(int pos, int val);
int eeGetInt(int pos);
void EEPROMWritelong(int address, long value);
long EEPROMReadlong(int address);
void readEEPROM(char* buff, int offset, int len);
void writeEEPROM(char* buff, int offset, int len);


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  delay(10);
  Serial.println();
  Serial.println("Starting ESP8266 MQTT Temperature & Humidity Sensor");

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

  // ตั้งค่าเวลาผ่าน NTP
  configTime(0, 0, ntpServer);
  Serial.println("Waiting for NTP time sync: ");
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

  getConfig();        // อ่านค่า config จาก EEPROM
  printConfig();      // แสดงค่า config ที่อ่านได้
  readSensorData();   // อ่านค่า sensor และแสดงผลลัพธ์
  buzzer_sound();     // เสียง buzzer

  // ตั้งค่า SSL สำหรับ MQTT
  // X509List mqtt_cert(ca_cert);
  // mqttClient.setTrustAnchors(&mqtt_cert);

  mqttClient.setTrustAnchors(&mqtt_cert);

  // ตั้งค่า MQTT
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);
  // mqttClient.setBufferSizes(2048, 2048);
  mqttClient.setBufferSizes(512, 512);
  mqtt.setBufferSize(2048);

  #ifdef THINGSPEAK
    ThingSpeak.begin(client);
  #endif
  
  t_readSensor.attach(5, setReadSensorFlag);  // 5 seconds  // อ่านค่า sensor ทุก 5 วินาที
  t_sendDatatoThinkSpeak.every(60L * 1000L, sendDataToThingSpeak);  // ส่งข้อมูลไป ThingSpeak ทุก 1 นาที

  // ตั้งค่า watchdog
  watchdogTicker.attach(60, checkSystem);  // ตรวจสอบทุก 60 วินาที

  // เชื่อมต่อ MQTT
   // จัดสรรพื้นที่ให้ BearSSL ในหน่วยความจำ IRAM
  HeapSelectIram ephemeral;
  mqttClient.connect(mqtt_server, mqtt_port);
  mqtt.setKeepAlive(60);
}

void loop() {
  // put your main code here, to run repeatedly:
  unsigned long currentMillis = millis();
  static unsigned long lastYieldTime = 0;
  static unsigned long lastReconnectAttempt = 0;

  // ตรวจสอบการเชื่อมต่อ MQTT
  // ตรวจสอบการเชื่อมต่อ MQTT และลองใหม่ทุก 30 วินาที
  if (!mqtt.connected()) {
    // ลองเชื่อมต่อใหม่ทุก 30 วินาที
    if (currentMillis - lastReconnectAttempt > 30000) {
      lastReconnectAttempt = currentMillis;
      Serial.println("Attempting MQTT reconnection...");
      reconnectMQTT();
    }
  } else {
    mqtt.loop();
  }

  if (shouldReadSensor) {
    shouldReadSensor = false;
    controlRelay(); // เรียกใน task context ป้องกัน core dump
  }

  t_relay.update();
  t_delayStart.update();
  t_relay2.update();
  t_delayStart2.update();
  t_sendDatatoThinkSpeak.update();

  // เรียก yield ทุก 50ms เพื่อป้องกัน watchdog timeout
  if (currentMillis - lastYieldTime > 50) {
    yield();
    lastYieldTime = currentMillis;
  }
  
  delay(1);

}

void setReadSensorFlag() {
  shouldReadSensor = true;
}

// ฟังก์ชันสำหรับตรวจสอบระบบ
void checkSystem() {
  static unsigned long lastSuccessfulMqttOperation = 0;
  static int failCount = 0;
  
  // ตรวจสอบการเชื่อมต่อ MQTT
  if (mqtt.connected()) {
    lastSuccessfulMqttOperation = millis();
    failCount = 0;
  } else {
    // ตรวจสอบการ overflow ของ millis()
    unsigned long currentMillis = millis();
    if (currentMillis < lastSuccessfulMqttOperation) {
      // millis() overflow
      lastSuccessfulMqttOperation = currentMillis;
    } else if (currentMillis - lastSuccessfulMqttOperation > 5 * 60 * 1000) {
      failCount++;
      if (failCount >= 3) {
        Serial.println("System appears stuck, restarting...");
        stopTimers();
        ESP.restart();
      }
    }
  }
  
  // เพิ่ม: ตรวจสอบ heap memory ที่เหลือ
  int freeHeap = ESP.getFreeHeap();
  Serial.printf("Free heap: %d bytes\n", freeHeap);
  
  // หากหน่วยความจำน้อยเกินไป ให้รีสตาร์ท
  if (freeHeap < 4000) {  // ปรับค่าตามความเหมาะสม
    Serial.println("Low memory condition, restarting...");
    ESP.restart();
  }
}

void processCommand(StaticJsonDocument<1024>& doc) {
  // Verify that we have a command
  if (!doc.containsKey("command")) {
    Serial.println("Error: No command in message");
    return;
  }
  
  String command = doc["command"].as<String>();
  Serial.print("Processing command: ");
  Serial.println(command);
  
  // Check device_id (optional, since we're already subscribed to our own topic)
  if (doc.containsKey("device_id")) {
    String deviceIdStr = doc["device_id"].as<String>();
    if (deviceIdStr != String(DEVICE_ID)) {
      Serial.println("Error: Device ID mismatch");
      return;
    }
  }
  
  // Create response document
  DynamicJsonDocument response(1024);
  response["device_id"] = String(DEVICE_ID);
  response["command"] = command;
  response["success"] = true;
  
  // Process different commands
  if (command == "status") {
    // Read sensor data
    if (readSensorData() == 0) {
      JsonObject data = response.createNestedObject("data");
      data["temperature"] = temperature;
      data["humidity"] = humidity;
      data["relay"] = (digitalRead(RELAY1) == HIGH);
      data["mode"] = AUTO ? "auto" : "manual";
      data["name"] = deviceName;
      data["option"] = options;
    } else {
      response["success"] = false;
      response["message"] = "Failed to read sensor";
    }
  }
  else if (command == "settemp") {
    if (doc.containsKey("low") && doc.containsKey("high")) {
      float lt = doc["low"].as<float>();
      float ht = doc["high"].as<float>();
      
      // ตรวจสอบขอบเขตค่า
      if (lt < 0 || ht > 100) {
        response["success"] = false;
        response["message"] = "Temperature must be between 0-100°C";
        sendMqttResponse(command, response);
        return;
      }
      
      // ตรวจสอบว่า low ต้องน้อยกว่า high
      if (lt >= ht) {
        response["success"] = false;
        response["message"] = "Low temperature must be less than high temperature";
        sendMqttResponse(command, response);
        return;
      }
      
      lowTemp = lt;
      highTemp = ht;
      saveConfig();
      
      response["data"]["low"] = lowTemp;
      response["data"]["high"] = highTemp;
      response["message"] = "Temperature settings updated";
    } else {
      response["success"] = false;
      response["message"] = "Missing low or high temperature";
    }
  }
  else if (command == "sethum") {
    if (doc.containsKey("low") && doc.containsKey("high")) {
      float lh = doc["low"].as<float>();
      float hh = doc["high"].as<float>();
      
      // ตรวจสอบขอบเขตค่า
      if (lh < 0 || hh > 100) {
        response["success"] = false;
        response["message"] = "Humidity must be between 0-100%";
        sendMqttResponse(command, response);
        return;
      }
      
      // ตรวจสอบว่า low ต้องน้อยกว่า high
      if (lh >= hh) {
        response["success"] = false;
        response["message"] = "Low humidity must be less than high humidity";
        sendMqttResponse(command, response);
        return;
      }
      
      lowHumidity = lh;
      highHumidity = hh;
      saveConfig();
      
      response["data"]["low"] = lowHumidity;
      response["data"]["high"] = highHumidity;
      response["message"] = "Humidity settings updated";
    } else {
      response["success"] = false;
      response["message"] = "Missing low or high humidity";
    }
  }
  else if (command == "setmode") {
    if (doc.containsKey("mode")) {
      String mode = doc["mode"].as<String>();
      if (mode == "auto") {
        AUTO = true;
        stopTimers();
      } else if (mode == "manual") {
        AUTO = false;
        stopTimers();
      } else {
        response["success"] = false;
        response["message"] = "Invalid mode (use 'auto' or 'manual')";
        sendMqttResponse(command, response);
        return;
      }
      saveConfig();
      
      response["data"]["mode"] = AUTO ? "auto" : "manual";
      response["message"] = "Mode updated";
    } else {
      response["success"] = false;
      response["message"] = "Missing mode parameter";
    }
  }
  else if (command == "setoption") {
    if (doc.containsKey("option")) {
      int option = doc["option"].as<int>();
      if (option >= 0 && option <= 4) {
        options = option;
        saveConfig();
        
        response["data"]["option"] = options;
        response["message"] = "Option updated";
      } else {
        response["success"] = false;
        response["message"] = "Option must be between 0-4";
      }
    } else {
      response["success"] = false;
      response["message"] = "Missing option parameter";
    }
  }
  else if (command == "relay") {
    if (doc.containsKey("state")) {
      int state = doc["state"].as<int>();
      
      if (!AUTO) {
        if (state == 1) {
          turnRelayOn();
          RelayEvent = true;
          response["data"]["relay"] = true;
          response["message"] = "Relay turned ON";
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
          response["data"]["relay"] = false;
          response["message"] = "Relay turned OFF";
        } else {
          response["success"] = false;
          response["message"] = "State must be 0 or 1";
        }
      } else {
        response["success"] = false;
        response["message"] = "Cannot control relay in AUTO mode";
      }
    } else {
      response["success"] = false;
      response["message"] = "Missing state parameter";
    }
  }
  else if (command == "setname") {
    if (doc.containsKey("name")) {
      String name = doc["name"].as<String>();
      if (name.length() <= 200) {
        name.toCharArray(deviceName, 201);
        saveConfig();
        
        response["data"]["name"] = deviceName;
        response["message"] = "Device name updated";
      } else {
        response["success"] = false;
        response["message"] = "Name too long (max 200 chars)";
      }
    } else {
      response["success"] = false;
      response["message"] = "Missing name parameter";
    }
  }
  else if (command == "setchannel") {
    if (doc.containsKey("channel_id")) {
      unsigned long newChannelId = doc["channel_id"].as<unsigned long>();
      channelID = newChannelId;
      saveConfig();
      
      response["data"]["channel_id"] = channelID;
      response["message"] = "ThingSpeak channel ID updated";
    } else {
      response["success"] = false;
      response["message"] = "Missing channel_id parameter";
    }
  }
  else if (command == "setwritekey") {
    if (doc.containsKey("api_key")) {
      String apiKey = doc["api_key"].as<String>();
      if (apiKey.length() <= 16) {
        apiKey.toCharArray(writeAPIKey, 17);
        saveConfig();
        
        response["message"] = "ThingSpeak Write API Key updated";
      } else {
        response["success"] = false;
        response["message"] = "API key too long (max 16 chars)";
      }
    } else {
      response["success"] = false;
      response["message"] = "Missing api_key parameter";
    }
  }
  else if (command == "setreadkey") {
    if (doc.containsKey("api_key")) {
      String apiKey = doc["api_key"].as<String>();
      if (apiKey.length() <= 16) {
        apiKey.toCharArray(readAPIKey, 17);
        saveConfig();
        
        response["message"] = "ThingSpeak Read API Key updated";
      } else {
        response["success"] = false;
        response["message"] = "API key too long (max 16 chars)";
      }
    } else {
      response["success"] = false;
      response["message"] = "Missing api_key parameter";
    }
  }
  else if (command == "info") {
    if (doc.containsKey("secret")) {
      String secret = doc["secret"].as<String>();
      if (secret == INFO_SECRET) {
        // ใช้ response ที่สร้างไว้แล้ว แทนการสร้างใหม่
        JsonObject data = response.createNestedObject("data");
        data["name"] = deviceName;
        data["device_id"] = DEVICE_ID;
        data["temp_low"] = lowTemp;
        data["temp_high"] = highTemp;
        data["humidity_low"] = lowHumidity;
        data["humidity_high"] = highHumidity;
        data["mode"] = AUTO ? "auto" : "manual";
        data["option"] = options;
        data["cool"] = COOL;
        data["thingspeak_channel"] = channelID;
        
        response["message"] = "Device information retrieved";
      } else {
        response["success"] = false;
        response["message"] = "Invalid secret code";
      }
    } else {
      response["success"] = false;
      response["message"] = "Missing secret parameter";
    }
  }
  // เพิ่มคำสั่งอื่นๆ ต่อไปได้ตามต้องการ
  else {
    response["success"] = false;
    response["message"] = "Unknown command: " + command;
  }
  
  // Send response back via MQTT
  sendMqttResponse(command, response);
}

void sendMqttResponse(const String& command, JsonDocument& response) {
  String jsonStr;
  size_t len = measureJson(response);
  
  // ตรวจสอบขนาดก่อนส่ง
  if (len > 256) {  // หรือตามขนาด buffer ที่คิดว่าปลอดภัย
    Serial.println("Warning: Response too large, trimming data");
    // ลดขนาดข้อมูล โดยการลบฟิลด์ที่ไม่จำเป็น
    if (response.containsKey("data")) {
      JsonObject data = response["data"];
      // ลบฟิลด์ที่อาจมีขนาดใหญ่
      if (data.containsKey("message")) data.remove("message");
      if (data.containsKey("description")) data.remove("description");
    }
  }
  
  serializeJson(response, jsonStr);
  
  String topic = String(mqtt_topic_resp) + String(DEVICE_ID);
  
  // เพิ่ม Debug แสดงขนาดข้อความ
  Serial.print("Sending to topic: ");
  Serial.println(topic);
  Serial.print("JSON size: ");
  Serial.println(jsonStr.length());
  
  // ตรวจสอบการเชื่อมต่อก่อนส่ง
  if (!mqtt.connected()) {
    Serial.println("MQTT disconnected, attempting to reconnect...");
    reconnectMQTT();
  }
  
  // ตรวจสอบขนาดอีกครั้งก่อนส่ง
  if (jsonStr.length() > 400) {  // ตั้งค่าตามขนาดที่คิดว่าปลอดภัย
    Serial.println("Error: Message too large for MQTT buffer");
    return;
  }
  
  // ใช้ QoS 1 และไม่ retain (false)
  if (mqtt.publish(topic.c_str(), jsonStr.c_str(), false)) {
    Serial.println("Response sent to MQTT: SUCCESS");
  } else {
    Serial.print("Failed to send response to MQTT, state: ");
    Serial.println(mqtt.state());
    
    // แสดงรหัสข้อผิดพลาด
    switch (mqtt.state()) {
      case -4: Serial.println("Connection timeout"); break;
      case -3: Serial.println("Connection lost"); break;
      case -2: Serial.println("Connect failed"); break;
      case -1: Serial.println("Disconnected"); break;
      case 1: Serial.println("Bad protocol"); break;
      case 2: Serial.println("Bad client ID"); break;
      case 3: Serial.println("Unavailable"); break;
      case 4: Serial.println("Bad credentials"); break;
      case 5: Serial.println("Unauthorized"); break;
    }
  }
}

void __reconnectMQTT() {
  char clientId[12];
  snprintf(clientId, sizeof(clientId), "ESP%d", DEVICE_ID);

  int freeHeap = ESP.getFreeHeap();
  Serial.printf("Free heap before MQTT connect: %d\n", freeHeap);

  // ถ้า heap น้อยเกินไป ให้รีสตาร์ท
  if (freeHeap < 5000) {
    Serial.println("Low memory, restarting...");
    stopTimers();
    ESP.restart();
    return;
  }
  
  // ลองเชื่อมต่อแบบง่ายที่สุด - ไม่มี will message, ไม่มี credentials
  if (mqtt.connect(clientId)) {
    Serial.println("Connected!");
    char topic[32];
    snprintf(topic, sizeof(topic), "%s%d", mqtt_topic_cmd, DEVICE_ID);
    mqtt.subscribe(topic);
    return;
  }
  
  Serial.print("Failed, rc=");
  Serial.println(mqtt.state());
  
  // เพิ่ม delay และ yield เพื่อให้ watchdog ทำงานได้
  delay(10);
  yield();
}

void reconnectMQTT() {
  // ไม่ใช้ bool เพื่อลดการสร้างตัวแปรบน stack
  char client_id[20]; // ลดขนาดลงเพื่อประหยัดหน่วยความจำ
  static int retries = 0; // เปลี่ยนเป็น static เพื่อลดการใช้ stack
  static unsigned long lastAttempt = 0;
  
  unsigned long currentMillis = millis();
  
  // ลองเชื่อมต่อใหม่แค่ทุก 5 วินาที
  if (currentMillis - lastAttempt < 5000) {
    return; // ยังไม่ถึงเวลาลองใหม่
  }
  
  lastAttempt = currentMillis;

  int freeHeap = ESP.getFreeHeap();
  Serial.printf("Free heap before MQTT connect: %d\n", freeHeap);
  // ถ้า heap น้อยเกินไป ให้รีสตาร์ท
  if (freeHeap < 5000) {
    Serial.println("Low memory, restarting...");
    stopTimers();
    ESP.restart();
    return;
  }
  
  // ตรวจสอบว่ายังเชื่อมต่ออยู่หรือไม่
  if (mqtt.connected()) {
    retries = 0;
    return;
  }
  
  // ถ้าลองมากเกินไป รอให้นานขึ้น
  if (retries >= 3) {
    Serial.println("Too many MQTT connection attempts, waiting longer...");
    if (retries >= 5) {
      Serial.println("Maximum reconnection attempts reached. Restarting...");
      stopTimers();
      ESP.restart();
    }
    retries++;
    return;
  }
  
  // สร้าง client ID แบบง่าย
  snprintf(client_id, sizeof(client_id), "ESP-%d", DEVICE_ID);
  
  Serial.print("MQTT connecting as ");
  Serial.print(client_id);
  Serial.print("... ");
  
  // ลองเชื่อมต่อแบบง่าย - ไม่ใส่ will topic/message ในครั้งแรก
  if (retries < 2) {
    if (mqtt.connect(client_id, mqtt_username, mqtt_password)) {
      Serial.println("connected!");
      
      // สมัครรับข้อมูล
      char topic[40];
      snprintf(topic, sizeof(topic), "%s%d", mqtt_topic_cmd, DEVICE_ID);
      mqtt.subscribe(topic);
      
      retries = 0;
      return;
    }
  } 
  // ถ้าลองแบบง่ายไม่สำเร็จ จึงลองแบบมี will message
  else {
    char willTopic[40];
    snprintf(willTopic, sizeof(willTopic), "%s%d/status", mqtt_topic_resp, DEVICE_ID);
    
    if (mqtt.connect(client_id, mqtt_username, mqtt_password, willTopic, 0, true, "{\"status\":\"offline\"}")) {
      Serial.println("connected with will!");
      
      // สมัครรับข้อมูล
      char topic[40];
      snprintf(topic, sizeof(topic), "%s%d", mqtt_topic_cmd, DEVICE_ID);
      mqtt.subscribe(topic);
      
      // ส่งสถานะออนไลน์
      mqtt.publish(willTopic, "{\"status\":\"online\"}", true);
      
      retries = 0;
      return;
    }
  }
  
  Serial.print("failed, rc=");
  Serial.println(mqtt.state());
  retries++;
  yield(); // ป้องกัน watchdog timeout
}

bool _reconnectMQTT() {
  int retries = 0;
  char client_id[32];
  snprintf(client_id, sizeof(client_id), "ESP8266-%d", DEVICE_ID);

  while (!mqtt.connected() && retries < 3) {  // ลองเพียง 3 ครั้ง
    Serial.print("Connecting to MQTT broker as ");
    Serial.print(client_id);
    Serial.print("... ");

    char willTopic[64];
    snprintf(willTopic, sizeof(willTopic), "%s%d/status", mqtt_topic_resp, DEVICE_ID);
    const char* willMessage = "{\"status\":\"offline\"}";

    if (mqtt.connect(client_id, mqtt_username, mqtt_password, willTopic, 0, true, willMessage)) {
      Serial.println("connected");

      // Publish online message
      const char* onlineMsg = "{\"status\":\"online\"}";
      mqtt.publish(willTopic, onlineMsg, true);

      char topic[64];
      snprintf(topic, sizeof(topic), "%s%d", mqtt_topic_cmd, DEVICE_ID);
      mqtt.subscribe(topic);
      Serial.print("Subscribed to ");
      Serial.println(topic);

      // เพิ่ม keep alive และ clean session
      mqtt.setKeepAlive(60);
      
      return true;  // สำเร็จ
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" try again in 5 seconds");
      retries++;
      delay(5000);
      yield();  // ป้องกัน watchdog timeout
    }
  }
  
  // เพิ่มการตรวจสอบภายนอกลูป
  if (retries >= 3) {
    Serial.println("Maximum MQTT reconnection attempts reached. Will try again later or restart.");
    // บันทึกความล้มเหลวไว้ใน global variable เพื่อให้ checkSystem สามารถตรวจสอบได้
    static unsigned long lastReconnectAttempt = 0;
    static int failedReconnectCount = 0;
    
    unsigned long currentMillis = millis();
    if (currentMillis - lastReconnectAttempt > 60000) { // หากพยายามใหม่ห่างกันเกิน 1 นาที ให้รีเซ็ทตัวนับ
      failedReconnectCount = 1; 
    } else {
      failedReconnectCount++;
    }
    
    lastReconnectAttempt = currentMillis;
    
    if (failedReconnectCount >= 3) {
      Serial.println("Multiple reconnection failures, restarting...");
      stopTimers();
      ESP.restart();
    }
  }
  
  return false;  // ไม่สำเร็จ
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  
  // ตรวจสอบว่าข้อความมีขนาดใหญ่เกินไปหรือไม่
  if (length > 1024) {
    Serial.println("Message too large!");
    return;
  }
  
  // สร้าง buffer แยกเพื่อไม่ต้องแก้ไข payload โดยตรง
  char message[1025]; // ต้องแน่ใจว่าขนาดเพียงพอ
  if (length > sizeof(message) - 1) {
    length = sizeof(message) - 1; // ป้องกัน buffer overflow
  }
  
  memcpy(message, payload, length);
  message[length] = '\0';
  Serial.println(message);
  
  // Check if this message is for this device
  String topicStr = String(topic);
  String expectedTopic = String(mqtt_topic_cmd) + String(DEVICE_ID);
  
  if (topicStr == expectedTopic) {
    // Parse JSON command - ใช้ StaticJsonDocument ตามต้นฉบับเดิม
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }
    
    // ตรวจสอบ Device ID ถ้ามีใน JSON
    if (doc.containsKey("device_id")) {
      String deviceIdStr = doc["device_id"].as<String>();
      if (deviceIdStr != String(DEVICE_ID)) {
        Serial.println("Error: Device ID mismatch");
        return;
      }
    }
    
    // Process the command
    processCommand(doc);
  }
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

  int retries = 0;
  int sensorStatus = -1;

  // ลองอ่านค่าเซ็นเซอร์สูงสุด 3 ครั้ง
  while (sensorStatus != 0 && retries < 3) {
    sensorStatus = readSensorData();
    if (sensorStatus != 0) {
      Serial.print("Sensor read failed, retry: ");
      Serial.println(retries + 1);
      delay(100);
      retries++;
    }
  }

  if (sensorStatus != 0) {
    Serial.println("Sensor read failed after multiple attempts");
    return;  // ไม่ดำเนินการต่อถ้าอ่านเซ็นเซอร์ไม่สำเร็จ
  }

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

// 5. เพิ่มฟังก์ชัน stopTimers สำหรับหยุด timer ที่กำลังทำงาน
void stopTimers() {
  // ตรวจสอบและหยุด timer ที่กำลังทำงาน
  if (afterStart != -1) {
    t_relay.stop(afterStart);
    afterStart = -1;
  }
  
  if (afterStop != -1) {
    t_delayStart.stop(afterStop);
    afterStop = -1;
  }
  
  if (afterStart2 != -1) {
    t_relay2.stop(afterStart2);
    afterStart2 = -1;
  }
  
  if (afterStop2 != -1) {
    t_delayStart2.stop(afterStop2);
    afterStop2 = -1;
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
  Serial.println();
  Serial.println(F("================================"));
}


void saveConfig() {
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

  eeWriteInt(EEPROM_ADDR_VERSION_MARK, 6550);
  EEPROM.end();
}

void getConfig() {
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

  EEPROM.end();
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