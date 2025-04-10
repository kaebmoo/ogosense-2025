#!/usr/bin/env python3
"""MQTT Client module สำหรับการเชื่อมต่อกับ MQTT Broker"""

import os
import time
import json
import logging
import ssl
import paho.mqtt.client as mqtt
from typing import Callable, Optional, Dict, Any

logger = logging.getLogger(__name__)

class MQTTClient:
    def __init__(self, broker: str, port: int, username: str, password: str, 
                 device_id: str, topic_resp: str, message_callback: Callable = None):
        """สร้าง MQTT Client
        
        Args:
            broker: MQTT broker host
            port: MQTT broker port
            username: MQTT username
            password: MQTT password
            device_id: Device ID สำหรับใช้ในการสร้าง client ID
            topic_resp: Topic สำหรับรับข้อความตอบกลับ
            message_callback: ฟังก์ชันที่จะถูกเรียกเมื่อได้รับข้อความ
        """
        self.broker = broker
        self.port = port
        self.username = username
        self.password = password
        self.device_id = device_id
        self.topic_resp = topic_resp
        self.message_callback = message_callback
        self.client = None
        self._connected = False
        self.ca_cert_path = "ogosense_bot/ogosense_mqtt_python/emqxsl-ca.crt"
        
        # สร้าง MQTT Client
        self._setup_mqtt_client()
    
    def _setup_mqtt_client(self):
        """ตั้งค่า MQTT Client"""
        client_id = f"python-telegram-broker-{self.device_id}-{int(time.time())}"
        
        # เลือกใช้โปรโตคอลตามเวอร์ชัน
        try:
            # ลอง MQTTv5 ก่อน
            self.client = mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv5)
        except Exception as e:
            logger.warning(f"ไม่สามารถใช้ MQTTv5 ได้: {e}, กำลังใช้ MQTTv311 แทน")
            self.client = mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311)
        
        # ตั้งค่า TLS ถ้ามี CA Cert
        if os.path.exists(self.ca_cert_path):
            self.client.tls_set(
                ca_certs=self.ca_cert_path, 
                cert_reqs=ssl.CERT_REQUIRED, 
                tls_version=ssl.PROTOCOL_TLS
            )
        else:
            logger.warning(f"ไม่พบไฟล์ CA Certificate ที่ {self.ca_cert_path} การเชื่อมต่ออาจไม่ปลอดภัย")
        
        # ตั้งค่าฟังก์ชันการรับข้อความและการเชื่อมต่อ
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect
        
        # ตั้งค่าข้อมูลการเข้าสู่ระบบ
        self.client.username_pw_set(self.username, self.password)
        
        # ตั้งค่า Last Will and Testament (LWT)
        will_topic = f"ogosense/status/{self.device_id}"
        will_message = json.dumps({
            "device_id": self.device_id,
            "status": "offline",
            "timestamp": int(time.time())
        })
        self.client.will_set(will_topic, will_message, qos=1, retain=True)
    
    def _on_connect(self, client, userdata, flags, rc, properties=None):
        """ฟังก์ชันที่ทำงานเมื่อเชื่อมต่อ MQTT สำเร็จ"""
        if rc == 0:
            logger.info("เชื่อมต่อ MQTT สำเร็จ")
            self._connected = True
            
            # ส่งข้อความ online status
            status_topic = f"ogosense/status/{self.device_id}"
            status_message = json.dumps({
                "device_id": self.device_id,
                "status": "online",
                "timestamp": int(time.time())
            })
            client.publish(status_topic, status_message, qos=1, retain=True)
            
            # Subscribe ไปยัง topic สำหรับรับข้อความตอบกลับ
            client.subscribe(self.topic_resp, qos=1)
            logger.info(f"Subscribe topic: {self.topic_resp}")
        else:
            logger.error(f"เชื่อมต่อ MQTT ล้มเหลว ด้วยรหัส {rc}")
            self._connected = False
            self._print_mqtt_error(rc)
    
    # แก้ไขเมธอด _on_disconnect ในคลาส MQTTClient ใน mqtt_client.py
    def _on_disconnect(self, client, userdata, rc, properties=None):
        """ฟังก์ชันที่ทำงานเมื่อขาดการเชื่อมต่อ MQTT"""
        logger.warning(f"ขาดการเชื่อมต่อ MQTT (รหัส {rc})")
        self._connected = False 
    
    def _on_message(self, client, userdata, msg):
        """ฟังก์ชันที่ทำงานเมื่อได้รับข้อความ MQTT"""
        topic = msg.topic
        payload = msg.payload.decode('utf-8')
        logger.debug(f"ได้รับข้อความ MQTT จาก topic: {topic}, ข้อความ: {payload}")
        
        # เรียกใช้ callback ถ้ามี
        if self.message_callback:
            self.message_callback(client, topic, payload)
    
    def _print_mqtt_error(self, error_code):
        """แสดงข้อความอธิบายรหัสข้อผิดพลาด MQTT"""
        error_messages = {
            -4: "MQTT_CONNECTION_TIMEOUT - เกินเวลาการเชื่อมต่อ",
            -3: "MQTT_CONNECTION_LOST - การเชื่อมต่อขาดหาย",
            -2: "MQTT_CONNECT_FAILED - การเชื่อมต่อล้มเหลว",
            -1: "MQTT_DISCONNECTED - ถูกตัดการเชื่อมต่อ",
            1: "MQTT_CONNECT_BAD_PROTOCOL - โปรโตคอลไม่ถูกต้อง",
            2: "MQTT_CONNECT_BAD_CLIENT_ID - Client ID ไม่ถูกต้อง",
            3: "MQTT_CONNECT_UNAVAILABLE - Server ไม่พร้อมให้บริการ",
            4: "MQTT_CONNECT_BAD_CREDENTIALS - ข้อมูลผู้ใช้ไม่ถูกต้อง",
            5: "MQTT_CONNECT_UNAUTHORIZED - ไม่มีสิทธิ์เข้าถึง"
        }
        
        if error_code in error_messages:
            logger.error(error_messages[error_code])
        else:
            logger.error(f"MQTT ERROR - ข้อผิดพลาดที่ไม่ทราบสาเหตุ (รหัส {error_code})")
    
    def connect(self) -> bool:
        """เชื่อมต่อไปยัง MQTT Broker
        
        Returns:
            bool: True ถ้าเชื่อมต่อสำเร็จ, False ถ้าเชื่อมต่อล้มเหลว
        """
        try:
            logger.info(f"กำลังเชื่อมต่อ MQTT broker: {self.broker}:{self.port}")
            self.client.connect(self.broker, self.port, keepalive=60)
            self.client.loop_start()
            # รอการเชื่อมต่อสักครู่
            time.sleep(2)
            return self.is_connected()
        except Exception as e:
            logger.error(f"ไม่สามารถเชื่อมต่อ MQTT broker ได้: {e}")
            return False
    
    def reconnect(self) -> bool:
        """เชื่อมต่อใหม่ไปยัง MQTT Broker
    
        Returns:
            bool: True ถ้าเชื่อมต่อสำเร็จ, False ถ้าเชื่อมต่อล้มเหลว
        """
        try:
            # หยุด loop เดิมก่อน (ถ้ากำลังทำงานอยู่)
            if self.client and self.client._thread is not None:
                try:
                    logger.debug("กำลังหยุด MQTT client loop ก่อนเชื่อมต่อใหม่")
                    self.client.loop_stop()
                except Exception as e:
                    logger.warning(f"ไม่สามารถหยุด loop เดิมได้: {e}")
            
            logger.info(f"กำลังเชื่อมต่อใหม่ไปยัง MQTT broker: {self.broker}:{self.port}")
            
            # ลองใช้ reconnect ก่อน
            try:
                self.client.reconnect()
                self.client.loop_start()
                # รอการเชื่อมต่อสักครู่
                time.sleep(2)
                return self.is_connected()
            except Exception as reconnect_error:
                logger.warning(f"ไม่สามารถใช้ reconnect ได้: {reconnect_error}")
                
                # ถ้า reconnect ไม่ได้ ให้สร้าง client ใหม่และเชื่อมต่อใหม่
                logger.info("กำลังสร้าง MQTT client ใหม่...")
                self._setup_mqtt_client()
                try:
                    self.client.connect(self.broker, self.port, keepalive=60)
                    self.client.loop_start()
                    # รอการเชื่อมต่อสักครู่
                    time.sleep(2)
                    return self.is_connected()
                except Exception as connect_error:
                    logger.error(f"ไม่สามารถเชื่อมต่อใหม่กับ MQTT broker ได้: {connect_error}")
                    return False
        
        except Exception as e:
            logger.error(f"เกิดข้อผิดพลาดในการเชื่อมต่อใหม่: {e}")
            return False
    
    def disconnect(self):
        """ยกเลิกการเชื่อมต่อจาก MQTT Broker"""
        if self.client:
            try:
                # ส่งข้อความ offline status ก่อนตัดการเชื่อมต่อ
                status_topic = f"ogosense/status/{self.device_id}"
                status_message = json.dumps({
                    "device_id": self.device_id,
                    "status": "offline",
                    "timestamp": int(time.time())
                })
                self.client.publish(status_topic, status_message, qos=1, retain=True)
                
                # รอให้ข้อความส่งเสร็จ
                time.sleep(0.5)
                
                # ตัดการเชื่อมต่อ
                self.client.disconnect()
                self.client.loop_stop()
                logger.info("ยกเลิกการเชื่อมต่อ MQTT เรียบร้อย")
            except Exception as e:
                logger.error(f"เกิดข้อผิดพลาดขณะยกเลิกการเชื่อมต่อ MQTT: {e}")
    
    def publish(self, topic: str, payload: str, qos: int = 1, retain: bool = False) -> bool:
        """ส่งข้อความไปยัง MQTT Topic
        
        Args:
            topic: MQTT topic
            payload: ข้อความที่จะส่ง
            qos: Quality of Service
            retain: ตั้งค่า retain flag
            
        Returns:
            bool: True ถ้าส่งสำเร็จ, False ถ้าส่งล้มเหลว
        """
        if not self.is_connected():
            logger.warning("ไม่สามารถส่งข้อความ MQTT ได้: ไม่ได้เชื่อมต่อ")
            return False
        
        try:
            result = self.client.publish(topic, payload, qos=qos, retain=retain)
            success = result.rc == mqtt.MQTT_ERR_SUCCESS
            if success:
                logger.debug(f"ส่งข้อความ MQTT สำเร็จ: {topic}")
            else:
                logger.error(f"ส่งข้อความ MQTT ล้มเหลว: {topic}, รหัสข้อผิดพลาด: {result.rc}")
            return success
        except Exception as e:
            logger.error(f"เกิดข้อผิดพลาดขณะส่งข้อความ MQTT: {e}")
            return False
    
    def is_connected(self) -> bool:
        """ตรวจสอบว่ากำลังเชื่อมต่ออยู่หรือไม่
        
        Returns:
            bool: True ถ้าเชื่อมต่ออยู่, False ถ้าไม่ได้เชื่อมต่อ
        """
        return self._connected