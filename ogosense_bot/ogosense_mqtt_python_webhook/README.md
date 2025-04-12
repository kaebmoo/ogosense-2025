
# Python Telegram MQTT Bridge

A Python-based bridge that connects Telegram with MQTT for IoT device control and monitoring. This project allows you to control ESP8266/ESP32 devices via a Telegram Bot using MQTT as the communication protocol.

## Overview

This system provides a convenient way to monitor and control IoT devices through Telegram messages. It supports two operational modes:

1. **Polling Mode** (main.py): The traditional way of receiving updates from Telegram.
2. **Webhook Mode** (webhook_server.py): For deployment on servers with public IP addresses, offering better performance.

## Features

- Control ESP8266/ESP32 devices via Telegram commands
- Monitor temperature, humidity, and relay states
- Auto/Manual mode control for devices
- Secure communication with TLS encryption
- Automatic reconnection on connection failures
- Authorized chat ID management (access control)
- Command history tracking
- Queue-based message handling for reliability

## Requirements

- Python 3.9+
- MQTT Broker (e.g., EMQX, Mosquitto, HiveMQ)
- Telegram Bot Token

```
python-telegram-bot
paho-mqtt
python-dotenv
aiohttp
```

### Python dependencies
Install dependencies using:
```bash
pip install -r requirements.txt
```

---

## Setup Instructions

### 1. Create a `.env` File

Create a `.env` file in the project root with the following configuration:

```
# MQTT Configuration
MQTT_BROKER=your-mqtt-broker.com
MQTT_PORT=8883
MQTT_USERNAME=your_username
MQTT_PASSWORD=your_password
MQTT_TOPIC_CMD=ogosense/cmd/
MQTT_TOPIC_RESP=ogosense/resp/#

# Telegram Configuration 
BOT_TOKEN=your_telegram_bot_token

# Device Configuration
DEVICE_ID=REDACTED  # Default device ID for the bridge

# Authorized Chat IDs (default administrators)
DEFAULT_CHATID_1=REDACTED
DEFAULT_CHATID_2=REDACTED

# Storage Settings
STORAGE_FILE=authorized_chatids.pkl

# For Webhook Mode Only
WEBHOOK_URL=https://your-server.com/webhooks
WEBHOOK_HOST=0.0.0.0
PORT=8080

# SSL Certificate Path (for secure MQTT connection)
CA_CERT_PATH=emqxsl-ca.crt
```

### 2. SSL Certificate

If your MQTT broker uses SSL/TLS, place the CA certificate file (usually named `emqxsl-ca.crt`) in the project root directory.

### 3. Running the Application

#### Polling Mode (Standard)

```bash
python main.py
```

#### Webhook Mode (For servers with public IP address)

```bash
python webhook_server.py
```

## Command Reference

The Telegram bot supports the following commands:

### Basic Commands
- `/start` - Start using the device
- `/help` - Show all available commands

### ESP8266 Device Control Commands
- `/status <id>` - Check device status
- `/settemp <id> <lowTemp> <highTemp>` - Set temperature range
- `/sethum <id> <lowHumidity> <highHumidity>` - Set humidity range
- `/setmode <id> <auto/manual>` - Set Auto or Manual mode
- `/setoption <id> <0-4>` - Set control mode (Option)
- `/relay <id> <0/1>` - Turn relay ON/OFF (Manual mode only)
- `/setname <id> <name>` - Change device name
- `/setchannel <id> <channel_id>` - Set ThingSpeak Channel ID
- `/setwritekey <id> <api_key>` - Set ThingSpeak Write API Key
- `/setreadkey <id> <api_key>` - Set ThingSpeak Read API Key
- `/info <id> <secret>` - Show device information

### Admin Commands (ESP32 MQTT Bridge Management)
- `/addchatid <esp32_id> <chat_id>` - Add a Chat ID
- `/removechatid <esp32_id> <index(3-5)> <old_id>` - Remove a Chat ID by index
- `/updatechatid <esp32_id> <index(3-5)> <old_id> <new_id>` - Update a Chat ID
- `/listchatids <esp32_id>` - List all authorized Chat IDs

## Project Structure

- **main.py** - Main application for polling mode
- **webhook_server.py** - Server application for webhook mode
- **telegram_bot.py** - Telegram bot implementation
- **mqtt_client.py** - MQTT client implementation
- **storage.py** - Data storage management

## Operation Mode Details

### 1. Polling Mode

This is the default operation mode that works on any device without the need for a public IP address. The application actively polls Telegram servers for updates.

### 2. Webhook Mode

This mode requires a server with a public IP address and offers better performance as Telegram servers push updates to your webhook URL. This eliminates the need for continuous polling.

## Troubleshooting

### Common Issues

1. **MQTT Connection Failures**
   - Check your MQTT broker credentials
   - Verify that the CA certificate is correct
   - Ensure your broker is accessible from your network

2. **Telegram Bot Not Responding**
   - Verify that your bot token is correct
   - Check that the bot is active by messaging @BotFather

3. **Command Not Working**
   - Ensure the device ID is correct
   - Check that you have authorization (your Chat ID must be in the allowed list)
   - Verify the command syntax with `/help`

---

## Project Structure
```
ogosense_mqtt_python_webhook/
├── main.py              # Polling background app
├── webhook_server.py    # Webhook server via aiohttp
├── telegram_bot.py      # Telegram command logic
├── mqtt_client.py       # MQTT client
├── storage.py           # Authorized chat IDs
├── requirements.txt
└── .env                 # Environment variables (not committed)
```

---

## License

MIT License - See the project files for the full license text.

## Credits

- Original Arduino Version: kaebmoo (gmail com) - 2017
- Python Port: OGOsense - 2025

---

## Author
Developed by [@kaebmoo](https://github.com/kaebmoo) for ogosense (2025)

