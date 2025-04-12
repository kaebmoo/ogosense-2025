# set_webhook.py
import os
import requests
from dotenv import load_dotenv

# โหลด environment variables จากไฟล์ .env
load_dotenv()

BOT_TOKEN = os.getenv("BOT_TOKEN")
WEBHOOK_URL = os.getenv("WEBHOOK_URL")  # ตัวอย่าง: https://yourdomain.com/webhook

response = requests.post(
    f"https://api.telegram.org/bot{BOT_TOKEN}/setWebhook",
    data={"url": WEBHOOK_URL}
)

print(response.json())
