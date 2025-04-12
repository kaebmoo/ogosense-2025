# ogosense MQTT Webhook Bot

A Python-based Telegram bot to control ESP8266/ESP32 devices via MQTT. This project supports both background polling and webhook modes for deployment flexibility.

---

## Features

- Telegram control for IoT devices via MQTT
- Supports both polling and webhook modes
- Asynchronous processing with message queue
- Device authorization via chat ID
- Bi-directional communication with ESP8266/ESP32

---

## Requirements

- Python 3.9+
- MQTT Broker (e.g., Mosquitto, HiveMQ)
- Telegram Bot Token

### Python dependencies
Install dependencies using:
```bash
pip install -r requirements.txt
```

---

## Environment Variables
Create a `.env` file in the root directory:
```env
BOT_TOKEN=your_telegram_bot_token
DEVICE_ID=REDACTED
MQTT_BROKER=broker.example.com
MQTT_PORT=8883
MQTT_USERNAME=mqtt_user
MQTT_PASSWORD=mqtt_pass
MQTT_TOPIC_CMD=ogosense/cmd/
MQTT_TOPIC_RESP=ogosense/resp/#

WEBHOOK_URL=https://yourdomain.com
WEBHOOK_HOST=0.0.0.0
PORT=8080

DEFAULT_CHATID_1=123456789
DEFAULT_CHATID_2=987654321
```

---

## How to Run

### Background mode (Polling)
```bash
python main.py
```
This runs the bot as a background application using polling.

### Webhook mode
```bash
python webhook_server.py
```
This starts a webhook server with aiohttp to receive Telegram updates.

---

## Supported Commands

### Basic
- `/start` - Start the bot
- `/help` - Show available commands

### ESP8266
- `/status <id>`
- `/relay <id> <0|1>`
- `/settemp <id> <low> <high>`
- `/sethum <id> <low> <high>`
- `/setmode <id> auto|manual`
- `/setoption <id> <0-4>`
- `/info <id> <secret>`
- `/setname`, `/setchannel`, `/setwritekey`, `/setreadkey`

### ESP32 Management
- `/addchatid <esp32_id> <chat_id>`
- `/removechatid <esp32_id> <index> <chat_id>`
- `/updatechatid <esp32_id> <index> <old> <new>`
- `/listchatids <esp32_id>`

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
MIT License

---

## Author
Developed by [@kaebmoo](https://github.com/kaebmoo) for ogosense (2025)

