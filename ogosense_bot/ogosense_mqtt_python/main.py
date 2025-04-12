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
    
    # ใน main.py - ที่เมธอด start ของ MQTTTelegramBridge
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
            logger.error("ไม่สามารถเชื่อมต่อ MQTT ได้ จะลองเชื่อมต่อใหม่ในลูป...")
        
        
        # เริ่ม Telegram Bot
        
        # เริ่ม task สำหรับประมวลผลคิวข้อความ
        message_processor = asyncio.create_task(self._process_message_queue())
        
        # เริ่ม task สำหรับตรวจสอบการเชื่อมต่อ
        connection_checker = asyncio.create_task(self._check_connections())
        
        try:
            # ตรวจสอบโหมดการทำงาน
            webhook_url = os.getenv('WEBHOOK_URL')
            
            # เริ่ม Telegram Bot
            # run_webhook และ run_polling จะบล็อกโปรแกรมในโหมด webhook
            # แต่จะไม่บล็อกในโหมด polling
            await self.telegram_bot.start()
            logger.info("เริ่มต้น Telegram Bot...")

            # แสดงข้อความเมื่อเริ่มทำงาน
            logger.info("ระบบพร้อมทำงานแล้ว - รอรับคำสั่งจาก Telegram...")
            
            # ในโหมด webhook, โปรแกรมจะถูกบล็อกที่ self.telegram_bot.start()
            # ในโหมด polling, เราต้องรอจนกว่าโปรแกรมจะถูกสั่งให้ปิด
            if not webhook_url:
                while self.is_running:
                    await asyncio.sleep(1)
        
        except Exception as e:
            logger.error(f"เกิดข้อผิดพลาดในการทำงานหลัก: {e}")
        finally:
            # ยกเลิก tasks
            for task in [message_processor, connection_checker]:
                if task and not task.done():
                    task.cancel()
                    try:
                        await task
                    except asyncio.CancelledError:
                        pass
            
            await self.shutdown()

    # เพิ่มเมธอดใหม่สำหรับตรวจสอบและรักษาการเชื่อมต่อ
    # ในเมธอด _check_connections ของคลาส MQTTTelegramBridge
    async def _check_connections(self):
        """ตรวจสอบและรักษาการเชื่อมต่อทั้งหมด"""
        logger.info("เริ่มตรวจสอบการเชื่อมต่อ")
        reconnect_delay = 5
        max_reconnect_delay = 300
        
        while self.is_running:
            try:
                # ตรวจสอบการเชื่อมต่อ MQTT
                if not self.mqtt_client.is_connected():
                    logger.warning(f"MQTT ขาดการเชื่อมต่อ กำลังเชื่อมต่อใหม่...")
                    
                    # ทดสอบเชื่อมต่อใหม่ด้วย exponential backoff
                    if self.mqtt_client.reconnect():
                        logger.info("เชื่อมต่อ MQTT ใหม่สำเร็จ")
                        reconnect_delay = 5  # รีเซ็ตเวลารอ
                    else:
                        logger.error(f"ไม่สามารถเชื่อมต่อ MQTT ใหม่ได้ จะลองอีกครั้งใน {reconnect_delay} วินาที")
                        reconnect_delay = min(reconnect_delay * 2, max_reconnect_delay)
                
                # ตรวจสอบการเชื่อมต่อ Telegram
                telegram_connected = False
                try:
                    # ทดสอบการเชื่อมต่อ Telegram
                    if self.telegram_bot.bot:
                        me = await self.telegram_bot.bot.get_me()
                        if me:
                            telegram_connected = True
                            logger.debug("การเชื่อมต่อ Telegram ปกติ")
                except Exception as e:
                    logger.error(f"การเชื่อมต่อ Telegram มีปัญหา: {e}")
                
                # หากไม่สามารถเชื่อมต่อ Telegram ได้ ลองรีสตาร์ท
                if not telegram_connected:
                    logger.warning("กำลังพยายามรีสตาร์ท Telegram Bot...")
                    try:
                        # หยุด bot ก่อน
                        await self.telegram_bot.stop()
                        await asyncio.sleep(2)
                        
                        # เริ่ม bot ใหม่
                        success = await self.telegram_bot.start()
                        
                        # ตรวจสอบว่าเริ่มสำเร็จจริงๆ
                        if success:
                            logger.info("รีสตาร์ท Telegram Bot สำเร็จ")
                        else:
                            logger.error("รีสตาร์ท Telegram Bot ไม่สำเร็จ")
                    except Exception as restart_error:
                        logger.error(f"เกิดข้อผิดพลาดขณะรีสตาร์ท Telegram Bot: {restart_error}")
            
            except Exception as e:
                logger.error(f"เกิดข้อผิดพลาดในการตรวจสอบการเชื่อมต่อ: {e}")
            
            # รอก่อนตรวจสอบรอบถัดไป
            await asyncio.sleep(30)
    
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

    # ปรับปรุงเมธอด _process_message_queue
    async def _process_message_queue(self):
        """ประมวลผลคิวข้อความ Telegram"""
        logger.info("เริ่มการประมวลผลคิวข้อความ Telegram")
        
        # จำนวนความพยายามสูงสุดในการส่งข้อความ
        max_retries = 3
        
        while self.is_running:
            try:
                # รอข้อความจากคิว
                chat_id, message = await self.telegram_message_queue.get()
                
                # ส่งข้อความไปยัง Telegram พร้อมลอง retry
                success = False
                retry_count = 0
                last_error = None
                
                while not success and retry_count < max_retries:
                    try:
                        await self.telegram_bot.send_message(chat_id, message)
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
                self.telegram_message_queue.task_done()
                
            except asyncio.CancelledError:
                # task ถูกยกเลิก
                logger.info("Task ประมวลผลคิวข้อความถูกยกเลิก")
                break
            except Exception as e:
                logger.error(f"เกิดข้อผิดพลาดในการประมวลผลคิวข้อความ: {e}")
                await asyncio.sleep(5)  # รอสักครู่ก่อนลองใหม่

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