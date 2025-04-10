#!/usr/bin/env python3
"""
  MIT License
Version 1.0 2018-01-22 (Original Arduino)
Version 2.0 2025-03-15 (Original Arduino)
Version 3.0 2025-04-10 (Python Port)

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
import asyncio
import signal
from dotenv import load_dotenv

from telegram_bot import TelegramBot
from mqtt_client import MQTTClient
from storage import Storage, CommandHistory

# เซตอัพการบันทึกล็อก
logging.basicConfig(
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    level=logging.INFO  # เปลี่ยนเป็น DEBUG เพื่อดูข้อมูลเพิ่มเติม
)
logger = logging.getLogger(__name__)

# โหลดค่าคอนฟิกูเรชันจากไฟล์ .env
load_dotenv()

# ตัวแปรเก็บ config ระบบ
MAX_ALLOWED_CHATIDS = 5

class MQTTTelegramBridge:
    def __init__(self):
        # โหลด environment variables
        self.device_id = os.getenv("DEVICE_ID", "REDACTED")
        self.mqtt_broker = os.getenv("MQTT_BROKER")
        self.mqtt_port = int(os.getenv("MQTT_PORT", "8883"))
        self.mqtt_username = os.getenv("MQTT_USERNAME")
        self.mqtt_password = os.getenv("MQTT_PASSWORD")
        self.mqtt_topic_cmd = os.getenv("MQTT_TOPIC_CMD", "ogosense/cmd/")
        self.mqtt_topic_resp = os.getenv("MQTT_TOPIC_RESP", "ogosense/resp/#")
        self.bot_token = os.getenv("BOT_TOKEN")
        
        # แสดง Chat IDs ที่ได้รับอนุญาต
        default_chatid_1 = os.getenv("DEFAULT_CHATID_1", "REDACTED")
        default_chatid_2 = os.getenv("DEFAULT_CHATID_2", "REDACTED")
        logger.info(f"Chat IDs ที่ได้รับอนุญาต: {default_chatid_1}, {default_chatid_2}")
        
        # ตรวจสอบว่าตั้งค่าสำคัญครบถ้วนหรือไม่
        if not all([self.mqtt_broker, self.mqtt_username, self.mqtt_password, self.bot_token]):
            logger.error("ไม่มีการตั้งค่าที่จำเป็น กรุณาตรวจสอบไฟล์ .env")
            sys.exit(1)
        
        # เพิ่มคิวสำหรับข้อความ Telegram
        self.telegram_message_queue = asyncio.Queue()
        
        # สร้างเก็บข้อมูล chat IDs
        self.storage = Storage(MAX_ALLOWED_CHATIDS, [default_chatid_1, default_chatid_2])
        
        # ประวัติคำสั่ง
        self.command_history = CommandHistory()
        
        # สร้าง MQTT client
        logger.info(f"กำลังสร้าง MQTT client สำหรับ broker {self.mqtt_broker}:{self.mqtt_port}")
        self.mqtt_client = MQTTClient(
            self.mqtt_broker,
            self.mqtt_port,
            self.mqtt_username,
            self.mqtt_password,
            self.device_id,
            self.mqtt_topic_resp,
            self.on_mqtt_message
        )
        
        # สร้าง Telegram Bot
        logger.info(f"กำลังสร้าง Telegram Bot")
        self.telegram_bot = TelegramBot(
            self.bot_token, 
            self.storage,
            self.command_history,
            self.mqtt_client,
            self.device_id,
            self.mqtt_topic_cmd
        )
        
        # สถานะระบบ
        self.is_running = False
    
    def on_mqtt_message(self, client, topic, payload):
        """ฟังก์ชันที่ทำงานเมื่อได้รับข้อความ MQTT"""
        try:
            logger.debug(f"ได้รับข้อความ MQTT - Topic: {topic}, Payload: {payload}")
            
            # ตรวจสอบว่าเป็นการตอบกลับจากอุปกรณ์หรือไม่
            if topic.startswith("ogosense/resp/"):
                # แยก device ID จาก topic
                device_id = topic[14:]  # ตัด "ogosense/resp/" ออก
                logger.debug(f"Device ID จาก topic: {device_id}")
                
                # แปลง payload เป็น JSON
                try:
                    data = json.loads(payload)
                    logger.debug(f"ข้อมูล JSON: {data}")
                except json.JSONDecodeError:
                    logger.error(f"ไม่สามารถแปลงข้อความ JSON ได้: {payload}")
                    return
                
                # ตรวจสอบว่ามี device_id จาก payload ด้วยหรือไม่
                if "device_id" in data:
                    device_id = str(data["device_id"])
                    logger.debug(f"ใช้ Device ID จาก payload: {device_id}")
                
                # ตรวจสอบคำสั่งที่ได้รับการตอบกลับ
                if "command" in data:
                    command = data["command"]
                    logger.debug(f"คำสั่งที่ได้รับตอบกลับ: {command}")
                    
                    # ลองดึง chat_id โดยไม่ระบุคำสั่ง
                    chat_id = self.command_history.get_last_chat_id(device_id)
                    
                    # ถ้าไม่พบ ให้ลองดึงโดยระบุคำสั่ง
                    if not chat_id:
                        logger.debug(f"ไม่พบ chat_id ทั่วไป ลองดึงด้วยคำสั่งเฉพาะ {command}")
                        chat_id = self.command_history.get_last_chat_id(device_id, command)
                    
                    if chat_id:
                        logger.debug(f"พบ chat_id: {chat_id}")
                        success = data.get("success", False)
                        
                        # สร้างข้อความตอบกลับไปยัง Telegram
                        telegram_response = self.telegram_bot.format_mqtt_response(device_id, command, data, success)
                        
                        try:
                            # ใช้ put_nowait เพื่อไม่ให้บล็อก
                            self.telegram_message_queue.put_nowait((chat_id, telegram_response))
                            logger.debug(f"เพิ่มข้อความเข้าคิวสำเร็จ")
                        except Exception as e:
                            logger.error(f"ไม่สามารถเพิ่มข้อความเข้าคิว: {e}")
                    else:
                        logger.warning(f"ไม่พบ chat_id ล่าสุดที่เกี่ยวข้องกับอุปกรณ์ {device_id}")
        except Exception as e:
            logger.error(f"เกิดข้อผิดพลาดในการประมวลผลข้อความ MQTT: {e}")
            import traceback
            logger.error(traceback.format_exc())
    
    async def start(self):
        """เริ่มการทำงานของ bridge"""
        self.is_running = True
        
        # รับสัญญาณปิดโปรแกรม
        loop = asyncio.get_event_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, lambda: asyncio.create_task(self.shutdown()))
        
        # เริ่มการเชื่อมต่อ MQTT
        logger.info("กำลังเชื่อมต่อกับ MQTT Broker...")
        if not self.mqtt_client.connect():
            logger.error("ไม่สามารถเชื่อมต่อ MQTT ได้ กำลังปิดโปรแกรม...")
            await self.shutdown()
            return
        
        logger.info("เริ่มต้น Telegram Bot...")
        # เริ่ม Telegram Bot
        await self.telegram_bot.start()
        
        # เริ่ม task สำหรับประมวลผลคิวข้อความ
        message_processor = asyncio.create_task(self._process_message_queue())
        
        try:
            # แสดงข้อความเมื่อเริ่มทำงาน
            logger.info("ระบบพร้อมทำงานแล้ว - รอรับคำสั่งจาก Telegram...")
            
            # รอจนกว่าโปรแกรมจะถูกสั่งให้ปิด
            while self.is_running:
                await asyncio.sleep(1)
                
                # ตรวจสอบการเชื่อมต่อ MQTT
                if not self.mqtt_client.is_connected():
                    logger.warning("การเชื่อมต่อ MQTT ขาดหาย กำลังเชื่อมต่อใหม่...")
                    self.mqtt_client.reconnect()
        
        except Exception as e:
            logger.error(f"เกิดข้อผิดพลาดในการทำงานหลัก: {e}")
        finally:
            # ยกเลิก task ประมวลผลคิวข้อความ
            if message_processor and not message_processor.done():
                message_processor.cancel()
                try:
                    await message_processor
                except asyncio.CancelledError:
                    pass
            
            await self.shutdown()
    
    async def shutdown(self):
        """หยุดการทำงานของ bridge"""
        if not self.is_running:
            return
            
        logger.info("กำลังปิดโปรแกรม...")
        self.is_running = False
        
        # หยุด Telegram Bot
        await self.telegram_bot.stop()
        
        # หยุด MQTT Client
        self.mqtt_client.disconnect()
        
        logger.info("ปิดโปรแกรมเรียบร้อย")

    async def _process_message_queue(self):
        """ประมวลผลคิวข้อความ Telegram"""
        logger.info("เริ่มการประมวลผลคิวข้อความ Telegram")
        while self.is_running:
            try:
                # รอข้อความจากคิว
                chat_id, message = await self.telegram_message_queue.get()
                
                # ส่งข้อความไปยัง Telegram
                await self.telegram_bot.send_message(chat_id, message)
                logger.info(f"ส่งข้อความจากคิวไปยัง Telegram: {message[:100]}...")
                
                # บอกว่าได้ประมวลผลข้อความนี้แล้ว
                self.telegram_message_queue.task_done()
            except asyncio.CancelledError:
                # task ถูกยกเลิก
                logger.info("Task ประมวลผลคิวข้อความถูกยกเลิก")
                break
            except Exception as e:
                logger.error(f"เกิดข้อผิดพลาดในการประมวลผลคิวข้อความ: {e}")
                await asyncio.sleep(1)  # ป้องกันการวนลูปเร็วเกินไปในกรณีที่เกิดข้อผิดพลาด

async def main():
    """ฟังก์ชันหลักของโปรแกรม"""
    # แสดงข้อมูลโปรแกรม
    logger.info("เริ่มต้นระบบ Python Telegram MQTT Bridge")
    logger.info("เวอร์ชัน: 3.0.0 - Python DigitalOcean Version")
    
    # สร้างและเริ่ม Bridge
    bridge = MQTTTelegramBridge()
    await bridge.start()

if __name__ == "__main__":
    asyncio.run(main())