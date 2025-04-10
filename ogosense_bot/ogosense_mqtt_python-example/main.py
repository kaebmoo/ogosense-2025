# Project: ogosense_python_app
# Purpose: Replace ESP32 sketch with Python app for DigitalOcean deployment

import ssl
import time
import os
import logging
from dotenv import load_dotenv
from telegram import Update
from telegram.ext import (
    ApplicationBuilder, CommandHandler, MessageHandler,
    ContextTypes, filters
)
import paho.mqtt.client as mqtt
from paho.mqtt.client import Client, CallbackAPIVersion

# Load environment variables
load_dotenv()

# ----- CONFIGURATION -----
DEVICE_ID = os.getenv("DEVICE_ID", "59322")
BOT_TOKEN = os.getenv("BOT_TOKEN")
AUTHORIZED_CHAT_IDS = os.getenv("AUTHORIZED_CHAT_IDS", "32971348,25340254").split(",")

MQTT_BROKER = os.getenv("MQTT_BROKER")
MQTT_PORT = int(os.getenv("MQTT_PORT", 8883))
MQTT_USERNAME = os.getenv("MQTT_USERNAME")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD")
MQTT_TOPIC_CMD = os.getenv("MQTT_TOPIC_CMD", "ogosense/cmd/")
MQTT_TOPIC_RESP = os.getenv("MQTT_TOPIC_RESP", "ogosense/resp/#")
CA_CERT_PATH = os.getenv("CA_CERT_PATH", "ca_cert.pem")
MQTT_CLIENT_ID = f"python-telegram-broker-{DEVICE_ID}"

# Logging setup
logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s', level=logging.INFO)
logger = logging.getLogger(__name__)

# ----- Command History -----
MAX_COMMAND_HISTORY = 20
command_history = []

# ----- MQTT Functions -----
def on_connect(client, userdata, flags, rc):
    logger.info("Connected to MQTT with result code %s", str(rc))
    client.subscribe(MQTT_TOPIC_RESP)

def on_message(client, userdata, msg):
    logger.info("[MQTT] %s => %s", msg.topic, msg.payload.decode())

    topic_parts = msg.topic.split("/")
    if len(topic_parts) >= 3:
        device_id = topic_parts[2]
        response = msg.payload.decode()

        # Find the latest matching chat_id for this device_id
        matching = [c for c in reversed(command_history) if c["deviceId"] == device_id]
        if matching:
            chat_id = matching[0]["chatId"]
            try:
                context = userdata['context']
                app = userdata['app']
                import asyncio
                asyncio.run_coroutine_threadsafe(
                    app.bot.send_message(chat_id=chat_id, text=f"Response from {device_id}: {response}"),
                    context['loop']
                )
            except Exception as e:
                logger.error(f"Failed to send Telegram message: {e}")

mqtt_client = mqtt.Client(client_id=MQTT_CLIENT_ID, userdata={})
mqtt_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
mqtt_client.tls_set(CA_CERT_PATH, tls_version=ssl.PROTOCOL_TLSv1_2)
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
try:
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT)
except Exception as e:
    logger.error(f"MQTT connection failed: {e}")
    exit(1)

mqtt_client.loop_start()

# ----- Telegram Handlers -----
async def start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await update.message.reply_text("Hello! Send your command.")

async def help_command(update: Update, context: ContextTypes.DEFAULT_TYPE):
    help_text = (
        "Available commands:\n"
        "/start - Start the bot\n"
        "/help - Show this help message\n"
        "Send any command directly and it will be forwarded to your device via MQTT."
    )
    await update.message.reply_text(help_text)

async def handle_command(update: Update, context: ContextTypes.DEFAULT_TYPE):
    chat_id = str(update.effective_chat.id)
    text = update.message.text.strip()

    if chat_id not in AUTHORIZED_CHAT_IDS:
        await update.message.reply_text("Unauthorized user.")
        return

    payload = f"{DEVICE_ID}:{text}"
    mqtt_client.publish(MQTT_TOPIC_CMD + DEVICE_ID, payload)

    # Keep only the last MAX_COMMAND_HISTORY items
    if len(command_history) >= MAX_COMMAND_HISTORY:
        command_history.pop(0)

    command_history.append({
        "deviceId": DEVICE_ID,
        "command": text,
        "chatId": chat_id,
        "timestamp": time.time()
    })

    await update.message.reply_text(f"Command sent: {text}")

# ----- Main Function -----
def main():
    import asyncio
    loop = asyncio.get_event_loop()

    app = ApplicationBuilder().token(BOT_TOKEN).build()
    mqtt_client.user_data_set({"context": {"loop": loop}, "app": app})

    app.add_handler(CommandHandler("start", start))
    app.add_handler(CommandHandler("help", help_command))
    app.add_handler(MessageHandler(filters.TEXT & ~filters.COMMAND, handle_command))

    logger.info("Starting Telegram bot...")
    app.run_polling()

if __name__ == "__main__":
    main()
