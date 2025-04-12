# OGOsense MQTT Webhook Bot

Telegram bot ภาษา Python สำหรับควบคุมอุปกรณ์ ESP8266/ESP32 ผ่าน MQTT รองรับการทำงานทั้งแบบ background app และ webhook server

---

## คุณลักษณะเด่น

- Telegram ควบคุมอุปกรณ์ IoT ผ่าน MQTT
- รองรับทั้ง background app และ webhook
- ใช้ asyncio queue ในการประมวลผลข้อความแบบ async
- ตรวจสอบสิทธิ์ผู้ใช้ด้วย chat ID
- เชื่อมต่อแบบสองทางกับ ESP8266 และ ESP32

---

## เวอร์ชั่นที่ต้องการ

- Python 3.9+
- MQTT Broker (เช่น Mosquitto, HiveMQ)
- Telegram Bot Token

### Python dependencies
ติดตั้งไลบรารีที่จำเป็น:
```bash
pip install -r requirements.txt
```

---

## การตั้งค่า .env
สร้างไฟล์ `.env` ที่ root directory:
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

## การรันระบบ

### แบบ background app (Polling)
```bash
python main.py
```
เป็นการรันแบบ background โดยใช้ polling รับข้อความจาก Telegram

### แบบ webhook
```bash
python webhook_server.py
```
เป็นการรัน webhook server แบบ async ด้วย aiohttp

---

## คำสั่งที่รองรับ

### เบื้องต้น
- `/start` - เริ่มต้นบอท
- `/help` - แสดงคำสั่งทั้งหมด

### ควบคุม ESP8266
- `/status <id>`
- `/relay <id> <0|1>`
- `/settemp <id> <low> <high>`
- `/sethum <id> <low> <high>`
- `/setmode <id> auto|manual`
- `/setoption <id> <0-4>`
- `/info <id> <secret>`
- `/setname`, `/setchannel`, `/setwritekey`, `/setreadkey`

### จัดการ ESP32
- `/addchatid <esp32_id> <chat_id>`
- `/removechatid <esp32_id> <index> <chat_id>`
- `/updatechatid <esp32_id> <index> <old> <new>`
- `/listchatids <esp32_id>`

---

## โครงสร้างโปรเจกต์
```
ogosense_mqtt_python_webhook/
├── main.py              # สำหรับ polling (background app)
├── webhook_server.py    # สำหรับ webhook (aiohttp server)
├── telegram_bot.py      # จัดการคำสั่ง Telegram
├── mqtt_client.py       # จัดการ MQTT client
├── storage.py           # จัดการสิทธิ์ chat ID
├── requirements.txt
└── .env                 # เก็บค่า config (ไม่ควร commit)
```

---

## License
โปรเจกต์นี้ใช้สัญญาอนุญาตแบบ MIT

---

## ผู้พัฒนา
จัดทำโดย [@kaebmoo](https://github.com/kaebmoo) สำหรับโครงการ OGOsense (2025)

