#!/usr/bin/env python3
"""
  MIT License
Version 3.2 2025-04-12 (Webhook Mode)

Copyright (c) 2017 kaebmoo gmail com
Copyright (c) 2025 OGOsense (Python port)

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
"""

import os
import sys
import time
import json
import logging
import logging.config
import asyncio
import signal
from dotenv import load_dotenv

from telegram import Bot, Update
from telegram.ext import (
    Application,
    CommandHandler,
    ContextTypes,
    MessageHandler,
    filters
)

from mqtt_client import MQTTClient
from storage import Storage, CommandHistory
from telegram_bot import TelegramBot  # ใช้ TelegramBot จากไฟล์เดิม

# โหลดค่าคอนฟิกูเรชันจากไฟล์ .env
load_dotenv()

# คอนฟิกูเรชัน logging
LOGGING_CONFIG = {
    'version': 1,
    'disable_existing_loggers': False,
    'formatters': {
        'standard': {
            'format': '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
        },
    },
    'handlers': {
        'console': {
            'class': 'logging.StreamHandler',
            'level': 'INFO',
            'formatter': 'standard',
            'stream': 'ext://sys.stdout'
        },
    },
    'loggers': {
        'mqtt_client': {'level': 'INFO'},
        'telegram_bot': {'level': 'INFO'}, 
        'webhook_server': {'level': 'INFO'},
        'storage': {'level': 'INFO'},
        '__main__': {'level': 'INFO'},
        'httpx': {'level': 'WARNING'},
        'telegram': {'level': 'WARNING'},
        'telegram.ext': {'level': 'WARNING'},
        '': {'level': 'WARNING', 'handlers': ['console']}
    }
}

# ใช้คอนฟิกนี้แทน basicConfig
logging.config.dictConfig(LOGGING_CONFIG)
logger = logging.getLogger(__name__)

# ตัวแปรเก็บ config ระบบ
MAX_ALLOWED_CHATIDS = 5

async def main():
    """ฟังก์ชันหลักของโปรแกรม webhook"""
    # แสดงข้อมูลโปรแกรม
    logger.info("เริ่มต้นระบบ Python Telegram MQTT Bridge - Webhook Mode")
    logger.info("เวอร์ชัน: 3.2.0 - Webhook Mode")
    
    # โหลด environment variables
    device_id = os.getenv("DEVICE_ID", "REDACTED")
    mqtt_broker = os.getenv("MQTT_BROKER")
    mqtt_port = int(os.getenv("MQTT_PORT", "8883"))
    mqtt_username = os.getenv("MQTT_USERNAME")
    mqtt_password = os.getenv("MQTT_PASSWORD")
    mqtt_topic_cmd = os.getenv("MQTT_TOPIC_CMD", "ogosense/cmd/")
    mqtt_topic_resp = os.getenv("MQTT_TOPIC_RESP", "ogosense/resp/#")
    bot_token = os.getenv("BOT_TOKEN")
    
    # Webhook configuration
    webhook_url = os.getenv("WEBHOOK_URL")
    webhook_host = os.getenv("WEBHOOK_HOST", "0.0.0.0")
    webhook_port = int(os.getenv("PORT", 8080))
    
    if not webhook_url:
        logger.error("WEBHOOK_URL ไม่ได้กำหนดในไฟล์ .env")
        sys.exit(1)
        
    logger.info(f"Webhook URL: {webhook_url}")
    
    # ตรวจสอบว่าตั้งค่าสำคัญครบถ้วนหรือไม่
    if not all([mqtt_broker, mqtt_username, mqtt_password, bot_token]):
        logger.error("ไม่มีการตั้งค่าที่จำเป็น กรุณาตรวจสอบไฟล์ .env")
        sys.exit(1)
    
    # แสดง Chat IDs ที่ได้รับอนุญาต
    default_chatid_1 = os.getenv("DEFAULT_CHATID_1", "REDACTED")
    default_chatid_2 = os.getenv("DEFAULT_CHATID_2", "REDACTED")
    logger.info(f"Chat IDs ที่ได้รับอนุญาต: {default_chatid_1}, {default_chatid_2}")
    
    # สร้างเก็บข้อมูล chat IDs
    storage = Storage(MAX_ALLOWED_CHATIDS, [default_chatid_1, default_chatid_2])
    
    # ประวัติคำสั่ง
    command_history = CommandHistory()
    
    # เพิ่มคิวสำหรับข้อความ Telegram
    telegram_message_queue = asyncio.Queue()
    
    # สร้าง Application สำหรับ Telegram Bot
    application = Application.builder().token(bot_token).build()
    
    # สร้าง MQTT client
    logger.info(f"กำลังสร้าง MQTT client สำหรับ broker {mqtt_broker}:{mqtt_port}")
    mqtt_client = MQTTClient(
        mqtt_broker,
        mqtt_port,
        mqtt_username,
        mqtt_password,
        device_id,
        mqtt_topic_resp,
        on_mqtt_message
    )
    
    # เริ่มการเชื่อมต่อ MQTT
    logger.info("กำลังเชื่อมต่อกับ MQTT Broker...")
    if not mqtt_client.connect():
        logger.error("ไม่สามารถเชื่อมต่อ MQTT ได้ กำลังปิดโปรแกรม...")
        sys.exit(1)
    
    # สร้าง TelegramBot (ใช้คลาสจากไฟล์เดิม)
    telegram_bot = TelegramBot(
        bot_token, 
        storage,
        command_history,
        mqtt_client,
        device_id,
        mqtt_topic_cmd
    )
    # กำหนดค่า application ให้กับ telegram_bot
    telegram_bot.application = application
    telegram_bot.bot = application.bot
    
    # ลงทะเบียนคำสั่งต่างๆ
    telegram_bot._register_commands()
    
    # เริ่ม task สำหรับประมวลผลคิวข้อความ
    message_processor = asyncio.create_task(process_message_queue(
        telegram_message_queue, telegram_bot
    ))
    
    # กำหนดตัวแปรใน global scope ให้ใช้ได้ใน on_mqtt_message
    globals()['telegram_message_queue'] = telegram_message_queue
    globals()['command_history'] = command_history
    globals()['telegram_bot'] = telegram_bot
    
    try:
        # เริ่ม webhook server
        logger.info(f"กำลังเริ่ม Telegram Bot ในโหมด webhook ที่ port {webhook_port}")
        
        # แทนที่จะใช้ run_webhook() ซึ่งบล็อกโปรแกรม
        # ให้ใช้วิธีการตั้งค่า webhook และเริ่ม webhook server แยกกัน
        await application.initialize()
        await application.start()
        
        # ตั้งค่า webhook URL
        await application.bot.set_webhook(url=f"{webhook_url}/{bot_token}")
        
        # สร้าง webhook server แบบแยก
        from aiohttp import web
        
        async def webhook_handler(request):
            # รับข้อมูลจาก request
            update_data = await request.json()
            # ส่งข้อมูลให้ application ประมวลผล
            await application.process_update(Update.de_json(update_data, application.bot))
            # ตอบกลับ request
            return web.Response(text="OK")
        
        # สร้าง web app
        app = web.Application()
        app.router.add_post(f"/{bot_token}", webhook_handler)
        
        # เริ่ม web server
        runner = web.AppRunner(app)
        await runner.setup()
        site = web.TCPSite(runner, webhook_host, webhook_port)
        await site.start()
        
        logger.info(f"Webhook server เริ่มทำงานที่ {webhook_host}:{webhook_port}")
        
        # รอจนกว่าโปรแกรมจะถูกปิด
        while True:
            await asyncio.sleep(3600)  # รอ 1 ชั่วโมงแล้วเช็คใหม่
            
    except Exception as e:
        logger.error(f"เกิดข้อผิดพลาดในการทำงาน webhook: {e}")
        
    finally:
        # Cleanup
        if message_processor:
            message_processor.cancel()
            try:
                await message_processor
            except asyncio.CancelledError:
                pass
        
        # หยุด MQTT Client
        mqtt_client.disconnect()
        
        logger.info("ปิด webhook เรียบร้อย")

async def process_message_queue(queue, telegram_bot):
    """ประมวลผลคิวข้อความ Telegram"""
    logger.info("เริ่มการประมวลผลคิวข้อความ Telegram")
    
    # จำนวนความพยายามสูงสุดในการส่งข้อความ
    max_retries = 3
    
    while True:
        try:
            # รอข้อความจากคิว
            chat_id, message = await queue.get()
            
            # ส่งข้อความไปยัง Telegram พร้อมลอง retry
            success = False
            retry_count = 0
            last_error = None
            
            while not success and retry_count < max_retries:
                try:
                    await telegram_bot.send_message(chat_id, message)
                    success = True
                    logger.info(f"ส่งข้อความจากคิวไปยัง Telegram: {message[:100]}...")
                except Exception as e:
                    retry_count += 1
                    last_error = e
                    logger.warning(f"ไม่สามารถส่งข้อความไปยัง Telegram ได้ (ครั้งที่ {retry_count}/{max_retries}): {e}")
                    await asyncio.sleep(2 * retry_count)  # รอนานขึ้นในแต่ละครั้ง
            
            if not success:
                logger.error(f"ไม่สามารถส่งข้อความไปยัง Telegram ได้หลังจากลอง {max_retries} ครั้ง: {last_error}")
            
            # บอกว่าได้ประมวลผลข้อความนี้แล้ว
            queue.task_done()
        except asyncio.CancelledError:
            # task ถูกยกเลิก
            logger.info("Task ประมวลผลคิวข้อความถูกยกเลิก")
            break
        except Exception as e:
            logger.error(f"เกิดข้อผิดพลาดในการประมวลผลคิวข้อความ: {e}")
            await asyncio.sleep(5)  # รอสักครู่ก่อนลองใหม่

def on_mqtt_message(client, topic, payload):
    """ฟังก์ชันที่ทำงานเมื่อได้รับข้อความ MQTT"""
    try:
        logger.debug(f"ได้รับข้อความ MQTT - Topic: {topic}, Payload: {payload}")
        
        # ตรวจสอบว่าเป็นการตอบกลับจากอุปกรณ์หรือไม่
        if topic.startswith("ogosense/resp/"):
            # แยก device ID จาก topic
            device_id = topic[14:]  # ตัด "ogosense/resp/" ออก
            
            # แปลง payload เป็น JSON
            try:
                data = json.loads(payload)
            except json.JSONDecodeError:
                logger.error(f"ไม่สามารถแปลงข้อความ JSON ได้: {payload}")
                return
            
            # ตรวจสอบว่ามี device_id จาก payload ด้วยหรือไม่
            if "device_id" in data:
                device_id = str(data["device_id"])
            
            # ตรวจสอบคำสั่งที่ได้รับการตอบกลับ
            if "command" in data:
                command = data["command"]
                success = data.get("success", False)
                
                # สร้างข้อความตอบกลับไปยัง Telegram
                telegram_response = telegram_bot.format_mqtt_response(device_id, command, data, success)
                
                # ส่งข้อความไปยัง Telegram ใช้ chat_id ที่ส่งคำสั่งนี้
                chat_id = command_history.get_last_chat_id(device_id)
                if chat_id:
                    logger.debug(f"กำลังเพิ่มข้อความเข้าคิวสำหรับ chat ID: {chat_id}")
                    try:
                        # ใช้ put_nowait เพื่อไม่ให้บล็อก
                        telegram_message_queue.put_nowait((chat_id, telegram_response))
                    except Exception as e:
                        logger.error(f"ไม่สามารถเพิ่มข้อความเข้าคิว: {e}")
                else:
                    logger.warning(f"ไม่พบ chat_id ล่าสุดที่เกี่ยวข้องกับอุปกรณ์ {device_id}")
    except Exception as e:
        logger.error(f"เกิดข้อผิดพลาดในการประมวลผลข้อความ MQTT: {e}")

if __name__ == "__main__":
    asyncio.run(main())