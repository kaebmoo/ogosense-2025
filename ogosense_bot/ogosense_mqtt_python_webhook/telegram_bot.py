#!/usr/bin/env python3
"""Telegram Bot module สำหรับการเชื่อมต่อกับ Telegram"""

import os
import json
import time
import logging
from typing import Dict, List, Any, Optional, Union, Tuple

from telegram import Update, Bot
from telegram.ext import (
    Application,
    CommandHandler,
    ContextTypes,
    MessageHandler,
    filters
)

from storage import Storage, CommandHistory
from mqtt_client import MQTTClient

logger = logging.getLogger(__name__)

# ข้อความช่วยเหลือ
HELP_MESSAGE = """
Available Commands (คำสั่งที่สามารถใช้ได้):
/start - เริ่มต้นใช้งานอุปกรณ์
/help - แสดงรายการคำสั่งทั้งหมด

คำสั่งสำหรับควบคุมอุปกรณ์ ESP8266:
/status <id> - ตรวจสอบสถานะอุปกรณ์
/settemp <id> <lowTemp> <highTemp> - ตั้งค่าขอบเขต Temperature
/sethum <id> <lowHumidity> <highHumidity> - ตั้งค่าขอบเขต Humidity
/setmode <id> <auto/manual> - ตั้งค่าโหมด Auto หรือ Manual
/setoption <id> <0-4> - ตั้งค่าโหมดควบคุม (Option)
/relay <id> <0/1> - สั่งเปิด/ปิด Relay (Manual เท่านั้น)
/setname <id> <name> - เปลี่ยนชื่ออุปกรณ์
/setchannel <id> <channel_id> - ตั้งค่า ThingSpeak Channel ID
/setwritekey <id> <api_key> - ตั้งค่า ThingSpeak Write API Key
/setreadkey <id> <api_key> - ตั้งค่า ThingSpeak Read API Key
/info <id> <secret> - แสดงข้อมูลอุปกรณ์ (Device Info)

คำสั่งสำหรับจัดการ ESP32 MQTT Bridge (ต้องใช้ Device ID ของ ESP32):
/addchatid <esp32_id> <chat_id> - เพิ่ม Chat ID
/removechatid <esp32_id> <index(3-5)> <old_id> - ลบ Chat ID ตามตำแหน่ง
/updatechatid <esp32_id> <index(3-5)> <old_id> <new_id> - แก้ไข Chat ID
/listchatids <esp32_id> - แสดงรายการ Chat IDs ทั้งหมด

หมายเหตุ: <id> คือ Device ID ของอุปกรณ์ ESP8266
<esp32_id> คือ Device ID ของอุปกรณ์ ESP32 MQTT Bridge
"""

class TelegramBot:
    """คลาสสำหรับจัดการ Telegram Bot"""
    
    def __init__(self, token: str, storage: Storage, command_history: CommandHistory, 
                 mqtt_client: MQTTClient, esp32_device_id: str, mqtt_topic_cmd: str):
        """
        Args:
            token: Telegram Bot token
            storage: อ็อบเจกต์สำหรับจัดการ Chat IDs
            command_history: อ็อบเจกต์สำหรับจัดการประวัติคำสั่ง
            mqtt_client: อ็อบเจกต์สำหรับส่งข้อความ MQTT
            esp32_device_id: Device ID ของ ESP32 MQTT Bridge
            mqtt_topic_cmd: MQTT Topic สำหรับส่งคำสั่ง
        """
        self.token = token
        self.storage = storage
        self.command_history = command_history
        self.mqtt_client = mqtt_client
        self.esp32_device_id = esp32_device_id
        self.mqtt_topic_cmd = mqtt_topic_cmd
        self.application = None
        self.bot = None
    
    # ปรับปรุงเมธอด start ใน TelegramBot
    async def start(self):
        """เริ่มการทำงานของ Telegram Bot
        
        Returns:
            bool: True ถ้าเริ่มสำเร็จ, False ถ้าไม่สำเร็จ
        """
        try:
            # สร้าง Bot และ Application
            self.application = Application.builder().token(self.token).build()
            self.bot = self.application.bot
            
            # ทดสอบการเชื่อมต่อก่อน
            await self.bot.get_me()
            
            # ลงทะเบียนคำสั่ง
            self._register_commands()
            
            logger.info("กำลังเริ่ม Telegram Bot...")

            # ตรวจสอบโหมดการทำงาน (webhook หรือ polling)
            webhook_url = os.getenv('WEBHOOK_URL')

            if webhook_url:
                # ใช้โหมด webhook สำหรับ production (render.com)
                webhook_host = os.getenv('WEBHOOK_HOST', '0.0.0.0')
                webhook_port = int(os.getenv('PORT', 8080))
                
                logger.info(f"กำลังเริ่ม Telegram Bot ในโหมด webhook ที่ port {webhook_port}")
                
                # ใช้ run_webhook ซึ่งจะจัดการทั้ง initialize, start และ set_webhook
                await self.application.run_webhook(
                    listen=webhook_host,
                    port=webhook_port,
                    webhook_url=f"{webhook_url}/{self.token}",
                    url_path=self.token,
                    drop_pending_updates=True,
                    allowed_updates=Update.ALL_TYPES
                )
            else:
                # ใช้โหมด polling สำหรับ development (local)
                logger.info("กำลังเริ่ม Telegram Bot ในโหมด polling...")
                
                # run_polling จะจัดการทั้ง initialize และ start
                await self.application.run_polling(
                    allowed_updates=Update.ALL_TYPES,
                    drop_pending_updates=True
                )
            
            # แสดงข้อความว่า Bot เริ่มทำงานแล้ว
            logger.info("Telegram Bot เริ่มทำงานแล้ว")
            return True
        except Exception as e:
            logger.error(f"ไม่สามารถเริ่ม Telegram Bot ได้: {e}")
            return False

    # ปรับปรุงเมธอด stop ใน TelegramBot
    async def stop(self):
        """หยุดการทำงานของ Telegram Bot"""
        if self.application:
            try:
                logger.info("กำลังหยุด Telegram Bot...")
                
                # application.shutdown() จะจัดการยกเลิก webhook ให้โดยอัตโนมัติ
                await self.application.shutdown()
                
                logger.info("Telegram Bot หยุดทำงานแล้ว")
            except Exception as e:
                logger.error(f"เกิดข้อผิดพลาดขณะหยุด Telegram Bot: {e}")
    
    async def send_message(self, chat_id: str, text: str):
        """ส่งข้อความไปยัง Telegram"""
        if self.bot:
            try:
                await self.bot.send_message(chat_id=chat_id, text=text)
                logger.debug(f"ส่งข้อความไปยัง chat ID {chat_id} สำเร็จ")
            except Exception as e:
                logger.error(f"เกิดข้อผิดพลาดในการส่งข้อความไปยัง Telegram: {e}")
    
    def _register_commands(self):
        """ลงทะเบียนคำสั่งทั้งหมดของ Bot"""
        # คำสั่งพื้นฐาน
        self.application.add_handler(CommandHandler("start", self._cmd_start))
        self.application.add_handler(CommandHandler("help", self._cmd_help))
        
        # คำสั่งควบคุมอุปกรณ์
        self.application.add_handler(CommandHandler("status", self._cmd_status))
        self.application.add_handler(CommandHandler("settemp", self._cmd_settemp))
        self.application.add_handler(CommandHandler("sethum", self._cmd_sethum))
        self.application.add_handler(CommandHandler("setmode", self._cmd_setmode))
        self.application.add_handler(CommandHandler("setoption", self._cmd_setoption))
        self.application.add_handler(CommandHandler("relay", self._cmd_relay))
        self.application.add_handler(CommandHandler("setname", self._cmd_setname))
        self.application.add_handler(CommandHandler("setchannel", self._cmd_setchannel))
        self.application.add_handler(CommandHandler("setwritekey", self._cmd_setwritekey))
        self.application.add_handler(CommandHandler("setreadkey", self._cmd_setreadkey))
        self.application.add_handler(CommandHandler("info", self._cmd_info))
        
        # คำสั่งจัดการ Chat IDs
        self.application.add_handler(CommandHandler("addchatid", self._cmd_addchatid))
        self.application.add_handler(CommandHandler("removechatid", self._cmd_removechatid))
        self.application.add_handler(CommandHandler("updatechatid", self._cmd_updatechatid))
        self.application.add_handler(CommandHandler("listchatids", self._cmd_listchatids))
        
        # ข้อความที่ไม่ใช่คำสั่ง
        self.application.add_handler(MessageHandler(filters.TEXT & ~filters.COMMAND, self._handle_text))
        
        # เพิ่ม error handler
        self.application.add_error_handler(self._error_handler)
        
        logger.info("ลงทะเบียนคำสั่งทั้งหมดเรียบร้อยแล้ว")
    
    async def _error_handler(self, update: object, context: ContextTypes.DEFAULT_TYPE):
        """จัดการข้อผิดพลาดที่เกิดขึ้นในระหว่างการประมวลผลอัปเดต"""
        logger.error(f"เกิดข้อผิดพลาด: {context.error}")
    
    def _is_authorized(self, chat_id: str) -> bool:
        """ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่"""
        is_auth = self.storage.is_authorized(str(chat_id))
        logger.debug(f"ตรวจสอบสิทธิ์ chat ID {chat_id}: {'ได้รับอนุญาต' if is_auth else 'ไม่ได้รับอนุญาต'}")
        return is_auth
    
    def _check_device_id(self, device_id: str) -> bool:
        """ตรวจสอบว่า Device ID ตรงกับ ESP32 หรือไม่"""
        return device_id == self.esp32_device_id
    
    def _is_numeric(self, text: str) -> bool:
        """ตรวจสอบว่าข้อความเป็นตัวเลขหรือไม่"""
        if not text:
            return False
        
        has_dot = False
        start_index = 1 if text[0] == '-' else 0
        
        for i in range(start_index, len(text)):
            char = text[i]
            
            if char == '.':
                if has_dot:
                    return False  # มีจุดมากกว่า 1 จุด
                has_dot = True
            elif not char.isdigit():
                return False
        
        return True
    
    async def _handle_text(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """จัดการข้อความที่ไม่ใช่คำสั่ง"""
        chat_id = str(update.effective_chat.id)
        logger.debug(f"ได้รับข้อความจาก chat ID: {chat_id}")
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            logger.warning(f"ผู้ใช้ที่ไม่ได้รับอนุญาต Chat ID: {chat_id} พยายามใช้งานระบบ")
            return
        
        # แนะนำให้ใช้คำสั่ง
        await update.message.reply_text("กรุณาใช้คำสั่งเพื่อควบคุมอุปกรณ์\nพิมพ์ /help เพื่อดูรายการคำสั่งทั้งหมด")
    
    async def _cmd_start(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /start - เริ่มต้นใช้งาน"""
        chat_id = str(update.effective_chat.id)
        from_name = update.message.from_user.first_name
        logger.debug(f"ได้รับคำสั่ง /start จาก chat ID: {chat_id}")
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            logger.warning(f"ผู้ใช้ที่ไม่ได้รับอนุญาต Chat ID: {chat_id} พยายามใช้คำสั่ง /start")
            return
        
        welcome_msg = f"ยินดีต้อนรับสู่ Telegram MQTT Bridge คุณ{from_name}\n"
        welcome_msg += "คุณสามารถใช้คำสั่งต่างๆ เพื่อควบคุมอุปกรณ์ได้\n"
        welcome_msg += "พิมพ์ /help เพื่อดูรายการคำสั่งทั้งหมด"
        
        await update.message.reply_text(welcome_msg)
    
    async def _cmd_help(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /help - แสดงคำแนะนำ"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        await update.message.reply_text(HELP_MESSAGE)
    
    # ฟังก์ชันเตรียมคำสั่ง MQTT ทั่วไป
    def _prepare_mqtt_command(self, command: str, device_id: str, params: Dict = None) -> Tuple[str, str]:
        """เตรียมข้อมูลสำหรับส่งคำสั่ง MQTT
        
        Returns:
            Tuple[str, str]: (topic, payload)
        """
        data = {
            "command": command,
            "device_id": device_id,
            "timestamp": int(time.time())
        }
        
        # เพิ่มพารามิเตอร์เพิ่มเติม (ถ้ามี)
        if params:
            data.update(params)
        
        topic = f"{self.mqtt_topic_cmd}{device_id}"
        payload = json.dumps(data)
        
        return topic, payload
    
    # ฟังก์ชันสำหรับส่งคำสั่ง MQTT
    async def _send_mqtt_command(self, update: Update, command: str, device_id: str, params: Dict = None, success_msg: str = None):
        """ส่งคำสั่ง MQTT และตอบกลับไปยัง Telegram
        
        Args:
            update: Telegram update object
            command: ชื่อคำสั่ง
            device_id: Device ID ของอุปกรณ์
            params: พารามิเตอร์เพิ่มเติม
            success_msg: ข้อความตอบกลับเมื่อส่งสำเร็จ
        """
        """ส่งคำสั่ง MQTT และตอบกลับไปยัง Telegram"""
        chat_id = str(update.effective_chat.id)
        
        # บันทึกประวัติก่อนส่งคำสั่ง
        self.command_history.record_command(device_id, command, chat_id)
        
        # เตรียมข้อมูลสำหรับส่ง MQTT
        topic, payload = self._prepare_mqtt_command(command, device_id, params)
        
        # ส่งผ่าน MQTT
        if self.mqtt_client.publish(topic, payload):
            await update.message.reply_text(success_msg or f"ส่งคำสั่ง {command} ไปยังอุปกรณ์ {device_id} สำเร็จ")
            logger.info(f"ส่งคำสั่ง {command} ไปยัง MQTT สำเร็จ: {payload}")
        else:
            await update.message.reply_text("เกิดข้อผิดพลาดในการส่งคำสั่ง")
            logger.error(f"ส่งคำสั่ง {command} ไปยัง MQTT ล้มเหลว")
    
    # คำสั่ง /status
    async def _cmd_status(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /status - ตรวจสอบสถานะอุปกรณ์"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 1:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /status <id>")
            return
        
        device_id = context.args[0]
        
        # ตรวจสอบว่า Device ID เป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id):
            await update.message.reply_text("Device ID ต้องเป็นตัวเลขเท่านั้น")
            return
        
        # ส่งคำสั่ง
        await self._send_mqtt_command(
            update, 
            "status", 
            device_id, 
            success_msg=f"กำลังตรวจสอบสถานะอุปกรณ์ {device_id}"
        )
    
    # คำสั่ง /settemp
    async def _cmd_settemp(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /settemp - ตั้งค่าขอบเขตอุณหภูมิ"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 3:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /settemp <id> <lowTemp> <highTemp>")
            return
        
        device_id = context.args[0]
        low_temp_str = context.args[1]
        high_temp_str = context.args[2]
        
        # ตรวจสอบว่า Device ID และค่าอุณหภูมิเป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id) or not self._is_numeric(low_temp_str) or not self._is_numeric(high_temp_str):
            await update.message.reply_text("Device ID และค่าอุณหภูมิต้องเป็นตัวเลขเท่านั้น")
            return
        
        low_temp = float(low_temp_str)
        high_temp = float(high_temp_str)
        
        # ตรวจสอบขอบเขตค่า
        if low_temp < 0 or high_temp > 100:
            await update.message.reply_text("ค่าอุณหภูมิต้องอยู่ระหว่าง 0-100°C")
            return
        
        # ตรวจสอบว่า lowTemp ต้องน้อยกว่า highTemp
        if low_temp >= high_temp:
            await update.message.reply_text("ค่าอุณหภูมิต่ำสุดต้องน้อยกว่าค่าอุณหภูมิสูงสุด")
            return
        
        # ส่งคำสั่ง
        await self._send_mqtt_command(
            update, 
            "settemp", 
            device_id, 
            {"low": low_temp, "high": high_temp},
            success_msg=f"ตั้งค่าอุณหภูมิสำหรับอุปกรณ์ {device_id}\nLow: {low_temp_str}°C, High: {high_temp_str}°C"
        )
    
    # คำสั่ง /sethum
    async def _cmd_sethum(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /sethum - ตั้งค่าขอบเขตความชื้น"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 3:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /sethum <id> <lowHum> <highHum>")
            return
        
        device_id = context.args[0]
        low_hum_str = context.args[1]
        high_hum_str = context.args[2]
        
        # ตรวจสอบว่า Device ID และค่าความชื้นเป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id) or not self._is_numeric(low_hum_str) or not self._is_numeric(high_hum_str):
            await update.message.reply_text("Device ID และค่าความชื้นต้องเป็นตัวเลขเท่านั้น")
            return
        
        low_hum = float(low_hum_str)
        high_hum = float(high_hum_str)
        
        # ตรวจสอบขอบเขตค่า
        if low_hum < 0 or high_hum > 100:
            await update.message.reply_text("ค่าความชื้นต้องอยู่ระหว่าง 0-100%")
            return
        
        # ตรวจสอบว่า lowHum ต้องน้อยกว่า highHum
        if low_hum >= high_hum:
            await update.message.reply_text("ค่าความชื้นต่ำสุดต้องน้อยกว่าค่าความชื้นสูงสุด")
            return
        
        # ส่งคำสั่ง
        await self._send_mqtt_command(
            update, 
            "sethum", 
            device_id, 
            {"low": low_hum, "high": high_hum},
            success_msg=f"ตั้งค่าความชื้นสำหรับอุปกรณ์ {device_id}\nLow: {low_hum_str}%, High: {high_hum_str}%"
        )
    
    # คำสั่ง /setmode
    async def _cmd_setmode(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /setmode - ตั้งค่าโหมด Auto หรือ Manual"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 2:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /setmode <id> <auto/manual>")
            return
        
        device_id = context.args[0]
        mode = context.args[1].lower()
        
        # ตรวจสอบว่า Device ID เป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id):
            await update.message.reply_text("Device ID ต้องเป็นตัวเลขเท่านั้น")
            return
        
        # ตรวจสอบว่าโหมดถูกต้องหรือไม่
        if mode not in ["auto", "manual"]:
            await update.message.reply_text("โหมดไม่ถูกต้อง ต้องเป็น auto หรือ manual เท่านั้น")
            return
        
        # ส่งคำสั่ง
        await self._send_mqtt_command(
            update, 
            "setmode", 
            device_id, 
            {"mode": mode},
            success_msg=f"ตั้งค่าโหมดสำหรับอุปกรณ์ {device_id} เป็น {mode}"
        )
    
    # คำสั่ง /setoption
    async def _cmd_setoption(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /setoption - ตั้งค่าโหมดควบคุม"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 2:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /setoption <id> <0-4>")
            return
        
        device_id = context.args[0]
        option_str = context.args[1]
        
        # ตรวจสอบว่า Device ID และ option เป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id) or not self._is_numeric(option_str):
            await update.message.reply_text("Device ID และ option ต้องเป็นตัวเลขเท่านั้น")
            return
        
        option = int(option_str)
        
        # ตรวจสอบว่า option อยู่ในช่วงที่ถูกต้องหรือไม่
        if option < 0 or option > 4:
            await update.message.reply_text("Option ต้องอยู่ระหว่าง 0-4 เท่านั้น")
            return
        
        # ส่งคำสั่ง
        await self._send_mqtt_command(
            update, 
            "setoption", 
            device_id, 
            {"option": option},
            success_msg=f"ตั้งค่า Option สำหรับอุปกรณ์ {device_id} เป็น {option}"
        )
    
    # คำสั่ง /relay
    async def _cmd_relay(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /relay - สั่งเปิด/ปิด Relay"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 2:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /relay <id> <0/1>")
            return
        
        device_id = context.args[0]
        state_str = context.args[1]
        
        # ตรวจสอบว่า Device ID เป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id):
            await update.message.reply_text("Device ID ต้องเป็นตัวเลขเท่านั้น")
            return
        
        # ตรวจสอบว่า state เป็น 0 หรือ 1 เท่านั้น
        if state_str not in ["0", "1"]:
            await update.message.reply_text("สถานะ Relay ต้องเป็น 0 หรือ 1 เท่านั้น")
            return
        
        state = int(state_str)
        
        # ส่งคำสั่ง
        await self._send_mqtt_command(
            update, 
            "relay", 
            device_id, 
            {"state": state},
            success_msg=f"ตั้งค่า Relay สำหรับอุปกรณ์ {device_id} เป็น {'เปิด' if state else 'ปิด'}"
        )
    
    # คำสั่ง /setname
    async def _cmd_setname(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /setname - เปลี่ยนชื่ออุปกรณ์"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 2:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /setname <id> <name>")
            return
        
        device_id = context.args[0]
        
        # ตรวจสอบว่า Device ID เป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id):
            await update.message.reply_text("Device ID ต้องเป็นตัวเลขเท่านั้น")
            return
        
        # รวมข้อความที่เหลือเป็นชื่อ
        name = " ".join(context.args[1:])
        
        if not name:
            await update.message.reply_text("ต้องระบุชื่ออุปกรณ์")
            return
        
        # ส่งคำสั่ง
        await self._send_mqtt_command(
            update, 
            "setname", 
            device_id, 
            {"name": name},
            success_msg=f"ตั้งชื่ออุปกรณ์ {device_id} เป็น: {name}"
        )
    
    # คำสั่ง /setchannel
    async def _cmd_setchannel(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /setchannel - ตั้งค่า ThingSpeak Channel ID"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 2:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /setchannel <id> <channel_id>")
            return
        
        device_id = context.args[0]
        channel_id_str = context.args[1]
        
        # ตรวจสอบว่า Device ID และ Channel ID เป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id) or not self._is_numeric(channel_id_str):
            await update.message.reply_text("Device ID และ Channel ID ต้องเป็นตัวเลขเท่านั้น")
            return
        
        channel_id = int(channel_id_str)
        
        # ส่งคำสั่ง
        await self._send_mqtt_command(
            update, 
            "setchannel", 
            device_id, 
            {"channel_id": channel_id},
            success_msg=f"ตั้งค่า ThingSpeak Channel ID สำหรับอุปกรณ์ {device_id} เป็น {channel_id}"
        )
    
    # คำสั่ง API Key
    async def _cmd_setkey(self, update: Update, context: ContextTypes.DEFAULT_TYPE, command: str):
        """คำสั่ง /setwritekey และ /setreadkey - ตั้งค่า ThingSpeak API Key"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 2:
            await update.message.reply_text(f"รูปแบบคำสั่งไม่ถูกต้อง: /{command} <id> <api_key>")
            return
        
        device_id = context.args[0]
        
        # ตรวจสอบว่า Device ID เป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id):
            await update.message.reply_text("Device ID ต้องเป็นตัวเลขเท่านั้น")
            return
        
        # รวมข้อความที่เหลือเป็น API Key
        api_key = " ".join(context.args[1:])
        
        if not api_key:
            await update.message.reply_text(f"ต้องระบุ {'Write' if command == 'setwritekey' else 'Read'} API Key")
            return
        
        key_type = "Write" if command == "setwritekey" else "Read"
        
        # ส่งคำสั่ง
        await self._send_mqtt_command(
            update, 
            command, 
            device_id, 
            {"api_key": api_key},
            success_msg=f"ตั้งค่า {key_type} API Key สำหรับอุปกรณ์ {device_id} เรียบร้อย"
        )
        
    # คำสั่ง /setwritekey
    async def _cmd_setwritekey(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /setwritekey - ตั้งค่า ThingSpeak Write API Key"""
        await self._cmd_setkey(update, context, "setwritekey")
        
    # คำสั่ง /setreadkey
    async def _cmd_setreadkey(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /setreadkey - ตั้งค่า ThingSpeak Read API Key"""
        await self._cmd_setkey(update, context, "setreadkey")
        
    # คำสั่ง /info
    async def _cmd_info(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /info - แสดงข้อมูลอุปกรณ์"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 2:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /info <id> <secret>")
            return
        
        device_id = context.args[0]
        secret = context.args[1]
        
        # ตรวจสอบว่า Device ID เป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id):
            await update.message.reply_text("Device ID ต้องเป็นตัวเลขเท่านั้น")
            return
        
        # ส่งคำสั่ง
        await self._send_mqtt_command(
            update, 
            "info", 
            device_id, 
            {"secret": secret},
            success_msg=f"กำลังเรียกข้อมูลของอุปกรณ์ {device_id}"
        )
    
    # คำสั่ง Chat ID
    async def _cmd_addchatid(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /addchatid - เพิ่ม Chat ID"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 2:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /addchatid <esp32_id> <chat_id>")
            return
        
        device_id = context.args[0]
        new_chat_id = context.args[1]
        
        # ตรวจสอบว่า Device ID และ Chat ID เป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id) or not self._is_numeric(new_chat_id):
            await update.message.reply_text("Device ID และ Chat ID ต้องเป็นตัวเลขเท่านั้น")
            return
        
        # ตรวจสอบว่า Device ID ตรงกับ ESP32 หรือไม่
        if not self._check_device_id(device_id):
            await update.message.reply_text("Device ID ไม่ถูกต้อง สำหรับ ESP32 Bridge นี้")
            return
        
        # เพิ่ม Chat ID
        if self.storage.add_chat_id(new_chat_id):
            await update.message.reply_text(f"เพิ่ม Chat ID: {new_chat_id} สำเร็จ")
            logger.info(f"เพิ่ม Chat ID: {new_chat_id} สำเร็จ")
        else:
            await update.message.reply_text("ไม่สามารถเพิ่ม Chat ID ได้ (อาจมีอยู่แล้วหรือเต็ม)")
            logger.warning(f"ไม่สามารถเพิ่ม Chat ID: {new_chat_id} ได้")
    
    async def _cmd_removechatid(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /removechatid - ลบ Chat ID"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 3:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /removechatid <esp32_id> <index(3-5)> <old_id>")
            return
        
        device_id = context.args[0]
        index_str = context.args[1]
        old_chat_id = context.args[2]
        
        # ตรวจสอบว่า Device ID, index และ old_chat_id เป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id) or not self._is_numeric(index_str) or not self._is_numeric(old_chat_id):
            await update.message.reply_text("Device ID, index และ old_chat_id ต้องเป็นตัวเลขเท่านั้น")
            return
        
        # ตรวจสอบว่า Device ID ตรงกับ ESP32 หรือไม่
        if not self._check_device_id(device_id):
            await update.message.reply_text("Device ID ไม่ถูกต้อง สำหรับ ESP32 Bridge นี้")
            return
        
        index = int(index_str)
        
        # ตรวจสอบว่า index อยู่ในช่วง 3-5 หรือไม่
        if index < 3 or index > 5:
            await update.message.reply_text("Index ต้องอยู่ระหว่าง 3-5")
            return
        
        # ลบ Chat ID
        if self.storage.remove_chat_id(index, old_chat_id):
            await update.message.reply_text(f"ลบ Chat ID ลำดับ {index}: {old_chat_id} สำเร็จ")
            logger.info(f"ลบ Chat ID ลำดับ {index}: {old_chat_id} สำเร็จ")
        else:
            await update.message.reply_text("ไม่สามารถลบ Chat ID ได้ (อาจไม่มีหรือไม่ตรงกัน)")
            logger.warning(f"ไม่สามารถลบ Chat ID ลำดับ {index}: {old_chat_id} ได้")
    
    async def _cmd_updatechatid(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /updatechatid - แก้ไข Chat ID"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 4:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /updatechatid <esp32_id> <index(3-5)> <old_id> <new_id>")
            return
        
        device_id = context.args[0]
        index_str = context.args[1]
        old_chat_id = context.args[2]
        new_chat_id = context.args[3]
        
        # ตรวจสอบว่า Device ID, index, old_chat_id และ new_chat_id เป็นตัวเลขหรือไม่
        if (not self._is_numeric(device_id) or not self._is_numeric(index_str) or 
            not self._is_numeric(old_chat_id) or not self._is_numeric(new_chat_id)):
            await update.message.reply_text("Device ID, index, old_chat_id และ new_chat_id ต้องเป็นตัวเลขเท่านั้น")
            return
        
        # ตรวจสอบว่า Device ID ตรงกับ ESP32 หรือไม่
        if not self._check_device_id(device_id):
            await update.message.reply_text("Device ID ไม่ถูกต้อง สำหรับ ESP32 Bridge นี้")
            return
        
        index = int(index_str)
        
        # ตรวจสอบว่า index อยู่ในช่วง 3-5 หรือไม่
        if index < 3 or index > 5:
            await update.message.reply_text("Index ต้องอยู่ระหว่าง 3-5")
            return
        
        # อัปเดต Chat ID
        if self.storage.update_chat_id(index, old_chat_id, new_chat_id):
            await update.message.reply_text(f"อัปเดต Chat ID ลำดับ {index} เป็น {new_chat_id} สำเร็จ")
            logger.info(f"อัปเดต Chat ID ลำดับ {index} จาก {old_chat_id} เป็น {new_chat_id} สำเร็จ")
        else:
            await update.message.reply_text("ไม่สามารถอัปเดต Chat ID ได้ (อาจไม่มีหรือไม่ตรงกัน)")
            logger.warning(f"ไม่สามารถอัปเดต Chat ID ลำดับ {index} จาก {old_chat_id} เป็น {new_chat_id} ได้")
    
    async def _cmd_listchatids(self, update: Update, context: ContextTypes.DEFAULT_TYPE):
        """คำสั่ง /listchatids - แสดงรายการ Chat IDs"""
        chat_id = str(update.effective_chat.id)
        
        # ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        if not self._is_authorized(chat_id):
            await update.message.reply_text("คุณไม่มีสิทธิ์ใช้งานระบบนี้")
            return
        
        # ตรวจสอบจำนวนพารามิเตอร์
        if not context.args or len(context.args) < 1:
            await update.message.reply_text("รูปแบบคำสั่งไม่ถูกต้อง: /listchatids <esp32_id>")
            return
        
        device_id = context.args[0]
        
        # ตรวจสอบว่า Device ID เป็นตัวเลขหรือไม่
        if not self._is_numeric(device_id):
            await update.message.reply_text("Device ID ต้องเป็นตัวเลขเท่านั้น")
            return
        
        # ตรวจสอบว่า Device ID ตรงกับ ESP32 หรือไม่
        if not self._check_device_id(device_id):
            await update.message.reply_text("Device ID ไม่ถูกต้อง สำหรับ ESP32 Bridge นี้")
            return
        
        # แสดงรายการ Chat IDs
        chat_ids = self.storage.get_all_chat_ids()
        
        msg = "รายการ Chat IDs ที่อนุญาต:\n"
        
        for i, cid in enumerate(chat_ids):
            msg += f"{i + 1}: {cid}\n"
        
        await update.message.reply_text(msg)
    
    def format_mqtt_response(self, device_id: str, command: str, data: Dict, success: bool) -> str:
        """สร้างข้อความตอบกลับไปยัง Telegram จากข้อมูล MQTT
        
        Args:
            device_id: Device ID ของอุปกรณ์
            command: ชื่อคำสั่ง
            data: ข้อมูลจาก MQTT
            success: สถานะความสำเร็จ
            
        Returns:
            str: ข้อความสำหรับส่งกลับไปยัง Telegram
        """
        telegram_response = f"การตอบกลับจากอุปกรณ์ {device_id}\n"
        
        # ตรวจสอบสถานะความสำเร็จ
        if success:
            telegram_response += "สถานะ: สำเร็จ\n"
        else:
            telegram_response += "สถานะ: ไม่สำเร็จ\n"
            if "message" in data:
                telegram_response += f"ข้อความ: {data['message']}\n"
        
        # เพิ่มข้อมูลตามประเภทคำสั่ง
        if command == "status" and "data" in data:
            device_data = data["data"]
            if "temperature" in device_data:
                telegram_response += f"อุณหภูมิ: {device_data['temperature']:.2f} °C\n"
            
            if "humidity" in device_data:
                telegram_response += f"ความชื้น: {device_data['humidity']} %\n"
            
            if "relay" in device_data:
                relay_status = "เปิด" if device_data["relay"] else "ปิด"
                telegram_response += f"สถานะ Relay: {relay_status}\n"
            
            if "mode" in device_data:
                telegram_response += f"โหมด: {device_data['mode']}\n"
            
            if "name" in device_data:
                telegram_response += f"ชื่ออุปกรณ์: {device_data['name']}\n"
                
            if "option" in device_data:
                option = device_data["option"]
                option_text = "Unknown"
                if option == 0:
                    option_text = "Humidity only"
                elif option == 1:
                    option_text = "Temperature only"
                elif option == 2:
                    option_text = "Temperature & Humidity"
                elif option == 3:
                    option_text = "Soil Moisture mode"
                elif option == 4:
                    option_text = "Additional mode"
                
                telegram_response += f"ตัวเลือก: {option_text}\n"
                
        elif (command == "settemp" or command == "sethum") and "data" in data:
            device_data = data["data"]
            if "low" in device_data and "high" in device_data:
                low = device_data["low"]
                high = device_data["high"]
                
                if command == "settemp":
                    telegram_response += "ตั้งค่าอุณหภูมิ:\n"
                    telegram_response += f"ต่ำสุด: {low} °C\n"
                    telegram_response += f"สูงสุด: {high} °C\n"
                else:
                    telegram_response += "ตั้งค่าความชื้น:\n"
                    telegram_response += f"ต่ำสุด: {low} %\n"
                    telegram_response += f"สูงสุด: {high} %\n"
                    
        elif command == "setmode" and "data" in data:
            device_data = data["data"]
            if "mode" in device_data:
                telegram_response += f"ตั้งค่าโหมด: {device_data['mode']}\n"
                
        elif command == "setoption" and "data" in data:
            device_data = data["data"]
            if "option" in device_data:
                telegram_response += f"ตั้งค่าตัวเลือก: {device_data['option']}\n"
                
        elif command == "relay" and "data" in data:
            device_data = data["data"]
            if "relay" in device_data:
                relay_status = "เปิด" if device_data["relay"] else "ปิด"
                telegram_response += f"ตั้งค่า Relay: {relay_status}\n"
                
        elif command == "setname" and "data" in data:
            device_data = data["data"]
            if "name" in device_data:
                telegram_response += f"ตั้งชื่ออุปกรณ์: {device_data['name']}\n"
                
        elif command == "setchannel" and "data" in data:
            device_data = data["data"]
            if "channel_id" in device_data:
                telegram_response += f"ตั้งค่า ThingSpeak Channel ID: {device_data['channel_id']}\n"
                
        elif command == "setwritekey" or command == "setreadkey":
            key_type = "Write API Key" if command == "setwritekey" else "Read API Key"
            telegram_response += f"ตั้งค่า {key_type} สำเร็จ\n"
    
        elif command == "info" and "data" in data:
            device_data = data["data"]
            telegram_response += "ข้อมูลอุปกรณ์:\n"
            
            if "name" in device_data:
                telegram_response += f"ชื่อ: {device_data['name']}\n"
            
            if "device_id" in device_data:
                telegram_response += f"Device ID: {device_data['device_id']}\n"
            
            if "temp_low" in device_data and "temp_high" in device_data:
                telegram_response += f"อุณหภูมิ: {device_data['temp_low']}-{device_data['temp_high']} °C\n"
            
            if "humidity_low" in device_data and "humidity_high" in device_data:
                telegram_response += f"ความชื้น: {device_data['humidity_low']}-{device_data['humidity_high']} %\n"
            
            if "mode" in device_data:
                telegram_response += f"โหมด: {device_data['mode']}\n"
            
            if "option" in device_data:
                option = device_data["option"]
                option_text = "Unknown"
                if option == 0:
                    option_text = "Humidity only"
                elif option == 1:
                    option_text = "Temperature only"
                elif option == 2:
                    option_text = "Temperature & Humidity"
                elif option == 3:
                    option_text = "Soil Moisture mode"
                elif option == 4:
                    option_text = "Additional mode"
                
                telegram_response += f"ตัวเลือก: {option_text}\n"
            
            if "cool" in device_data:
                cool_mode = device_data["cool"]
                cool_text = "COOL mode: Relay ON เมื่อ Temp >= High" if cool_mode else "HEAT mode: Relay ON เมื่อ Temp <= Low"
                telegram_response += f"โหมดทำความเย็น: {cool_text}\n"
            
            if "moisture" in device_data:
                moisture_mode = device_data["moisture"]
                moisture_text = "Moisture mode: Relay ON เมื่อ Humidity <= Low" if moisture_mode else "Dehumidifier mode: Relay ON เมื่อ Humidity >= High"
                telegram_response += f"โหมดความชื้น: {moisture_text}\n"
            
            if "thingspeak_channel" in device_data:
                telegram_response += f"ThingSpeak Channel: {device_data['thingspeak_channel']}\n"
            
            # แสดง write_api_key แบบปกปิดบางส่วน
            if "write_api_key" in device_data:
                api_key = device_data["write_api_key"]
                masked_key = api_key[:4] + "****" if len(api_key) > 4 else api_key
                telegram_response += f"ThingSpeak Write API Key: {masked_key}\n"

        return telegram_response