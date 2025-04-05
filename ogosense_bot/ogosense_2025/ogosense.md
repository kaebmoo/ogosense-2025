```markdown
# OgoSense Library for ESP8266

This library provides functionalities for the OgoSense project, designed for environmental monitoring and control using ESP8266 microcontrollers. It supports integration with various platforms like Netpie, ThingSpeak, and Telegram.

## Features

* **Sensor Data Acquisition:** Handles reading data from sensors (e.g., SHT30).
* **Actuator Control:** Manages control of actuators (e.g., relays).
* **Cloud Platform Integration:**
    * **Netpie:** Supports communication with the Netpie IoT platform for data publishing and control.
    * **ThingSpeak:** Enables data logging to ThingSpeak for analysis and visualization.
* **Telegram Integration:** Allows sending notifications and receiving commands via Telegram Bot.
* **Device Identification:** Provides a unique device ID for identification and management.
* **Security:** Includes a secret code for accessing sensitive information.

## Hardware Requirements

* ESP8266 (e.g., Wemos D1 mini, Pro)
* SHT30 Shield (for temperature and humidity sensing)
* Relay Shield (for controlling external devices)

## Software Requirements

* Arduino IDE
* ESP8266 Board Support Package
* Required Libraries (install via Arduino Library Manager):
    * WiFiClientSecure
    * ArduinoJson
    * ThingSpeak
    * Universal Telegram Bot
    * MicroGear (if using Netpie)

## Installation

1.  Download the OgoSense library.
2.  In the Arduino IDE, go to Sketch > Include Library > Add .ZIP Library...
3.  Select the downloaded ZIP file.

## Configuration

The library's behavior is configured through the `ogosense.h` header file. You'll need to adjust the following settings:

###   Netpie Configuration (if using Netpie)

```c++
#ifdef NETPIE
  #define APPID   "MyOgoSenseApp"                  // application id from netpie
  #define KEY     "MyNetpieKey123"           // key from netpie
  #define SECRET  "MyNetpieSecret456" // secret from netpie

  String ALIAS = "ogosense-mydevice";              // alias name netpie
  char *me = "/ogosense/sensor";                  // topic set for sensor data
  char *relayStatus1 = "/ogosense/relay/1/control";     // topic for relay 1 control
  char *relayStatus2 = "/ogosense/relay/2/control";     // topic for relay 2 control

  MicroGear microgear(client);
#endif
```

* `APPID`, `KEY`, `SECRET`:  Obtain these from your Netpie account.
* `ALIAS`:  A unique alias for your device on Netpie.
* `me`, `relayStatus1`, `relayStatus2`:  Define the topics used for communication with Netpie.

###   ThingSpeak Configuration (if using ThingSpeak)

```c++
#ifdef THINGSPEAK
  // ThingSpeak information
  const char thingSpeakAddress= "api.thingspeak.com";
  unsigned long channelID = 1234567;
  char readAPIKey= "MYREADAPIKEY";
  char writeAPIKey= "MYWRITEAPIKEY";
#endif
```

* `channelID`:  The ID of your ThingSpeak channel.
* `readAPIKey`:  The Read API Key for your ThingSpeak channel.
* `writeAPIKey`:  The Write API Key for your ThingSpeak channel.

###   Telegram Bot Configuration

```c++
const char* telegramToken = "1234567890:ABCDEFG1234567890";
#define TELEGRAM_CHAT_ID "123456789"  // ไอดีแชทสำหรับรับส่งข้อความ
const char* allowedChatIDs= {"123456789", "987654321"}; // เพิ่ม chat ID อื่น ๆ ที่อนุญาตได้
const int numAllowedChatIDs = sizeof(allowedChatIDs) / sizeof(allowedChatIDs[0]);
```

* `telegramToken`:  The API token for your Telegram Bot.
* `TELEGRAM_CHAT_ID`:  The chat ID of the user or group to receive messages.
* `allowedChatIDs`:  An array of chat IDs that are allowed to interact with the bot.

###   Device Settings

```c++
// หมายเลขเครื่อง (ปรับให้ตรงกับเครื่องนี้)
const int DEVICE_ID = 12345;

// กำหนด secret code สำหรับ /info (ค่าคงที่)
const char* INFO_SECRET = "mysecretcode";
```

* `DEVICE_ID`:  A unique identifier for your device.
* `INFO_SECRET`:  A secret code used for authentication when accessing device information.

## Usage

1.  Include the `ogosense.h` header file in your Arduino sketch:

    ```c++
    #include "ogosense.h"
    ```

2.  Initialize the OgoSense library in your `setup()` function. The initialization process will vary depending on which platforms you are using (Netpie, ThingSpeak, Telegram).

3.  Use the library's functions to:
    * Read sensor data.
    * Control relays.
    * Send data to Netpie or ThingSpeak.
    * Send and receive messages via Telegram.

## Example

```c++
#include <ESP8266WiFi.h>
#include "ogosense.h"

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

void setup() {
  Serial.begin(115200);
  delay(10);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // Initialize OgoSense (example - adjust based on your configuration)
  #ifdef NETPIE
    microgear.init(APPID, KEY, SECRET);
    microgear.connect(ALIAS.c_str());
  #endif

  #ifdef THINGSPEAK
    // ThingSpeak initialization (if needed)
  #endif

  // Telegram initialization (if needed)
  bot.setTelegramToken(telegramToken);
}

void loop() {
  // Example: Read sensor data (replace with actual sensor reading)
  float temperature = 25.5;
  float humidity = 60.2;

  // Example: Send data to ThingSpeak (if enabled)
  #ifdef THINGSPEAK
    ThingSpeak.setField(1, temperature);
    ThingSpeak.setField(2, humidity);
    ThingSpeak.writeFields(channelID, writeAPIKey);
  #endif

  // Example: Send data to Netpie (if enabled)
  #ifdef NETPIE
    microgear.publish(me, String("{\"temperature\":") + String(temperature) + String(",\"humidity\":") + String(humidity) + String("}"));
  #endif

  // Example: Handle Telegram messages (if enabled)
  TelegramMessage msg = bot.getNewMessage();
  if (msg.chat_id != 0) {
    Serial.println(msg.text);
    // Process Telegram commands here
    if (msg.text == "/start") {
      bot.sendMessage(msg.chat_id, "Welcome to OgoSense!");
    }
  }

  delay(5000); // Delay for demonstration purposes
}
```

**Important Notes:**

* This README provides a general outline. You'll need to refer to the library's source code and examples for specific function details and usage instructions.
* Remember to replace the placeholder values in the configuration sections with your actual credentials and settings.
* The example code is a simplified illustration. You'll need to adapt it to your specific application requirements.
* Always handle security best practices when dealing with API keys and sensitive information.