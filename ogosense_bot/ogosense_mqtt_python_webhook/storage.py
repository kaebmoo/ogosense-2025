#!/usr/bin/env python3
"""Storage module สำหรับการจัดการข้อมูลที่ต้องเก็บไว้ใช้งาน"""

import os
import time
import pickle
import logging
from typing import List, Dict, Any, Optional
from dotenv import load_dotenv
load_dotenv()

logger = logging.getLogger(__name__)

class Storage:
    """คลาสสำหรับจัดการการเก็บข้อมูล Chat IDs ที่ได้รับอนุญาต"""
    
    def __init__(self, max_chat_ids: int, default_chat_ids: List[str] = None):
        """
        Args:
            max_chat_ids: จำนวนสูงสุดของ Chat IDs ที่สามารถเก็บได้
            default_chat_ids: รายการเริ่มต้นของ Chat IDs
        """
        # ดึงค่าจาก environment variable หรือใช้ค่าเริ่มต้น
        self.storage_file = os.getenv("STORAGE_FILE", "authorized_chatids.pkl")
        self.max_chat_ids = max_chat_ids
        self.authorized_chatids = []
        self.num_authorized_chatids = 0
        
        # ถ้ามีข้อมูลเริ่มต้น
        if default_chat_ids:
            self.authorized_chatids = default_chat_ids.copy()
            self.num_authorized_chatids = len(default_chat_ids)
        
        # เพิ่มช่องว่างให้ครบตามจำนวนสูงสุด
        while len(self.authorized_chatids) < self.max_chat_ids:
            self.authorized_chatids.append("")
        
        # โหลดข้อมูลจากไฟล์ (ถ้ามี)
        self.load()
    
    def save(self) -> bool:
        """บันทึกข้อมูลลงไฟล์
        
        Returns:
            bool: True ถ้าบันทึกสำเร็จ, False ถ้าบันทึกไม่สำเร็จ
        """
        try:
            data = {
                "chatids": self.authorized_chatids,
                "num_chatids": self.num_authorized_chatids
            }
            with open(self.storage_file, "wb") as f:
                pickle.dump(data, f)
            logger.info(f"บันทึกข้อมูล Chat ID ลงไฟล์แล้ว: {self.authorized_chatids[:self.num_authorized_chatids]}")
            return True
        except Exception as e:
            logger.error(f"เกิดข้อผิดพลาดในการบันทึกข้อมูล Chat ID: {e}")
            return False
    
    def load(self) -> bool:
        """โหลดข้อมูลจากไฟล์
        
        Returns:
            bool: True ถ้าโหลดสำเร็จ, False ถ้าโหลดไม่สำเร็จหรือไม่มีไฟล์
        """
        try:
            if os.path.exists(self.storage_file):
                with open(self.storage_file, "rb") as f:
                    data = pickle.load(f)
                
                self.authorized_chatids = data.get("chatids", self.authorized_chatids)
                self.num_authorized_chatids = data.get("num_chatids", self.num_authorized_chatids)
                
                # เพิ่มช่องว่างให้ครบตามจำนวนสูงสุด (กรณีที่ไฟล์เก่ามีจำนวนน้อยกว่า)
                while len(self.authorized_chatids) < self.max_chat_ids:
                    self.authorized_chatids.append("")
                
                logger.info(f"โหลดข้อมูล Chat ID จากไฟล์แล้ว: {self.authorized_chatids[:self.num_authorized_chatids]}")
                return True
            else:
                logger.info("ไม่พบไฟล์เก็บข้อมูล Chat ID ใช้ค่าเริ่มต้น")
                self.save()  # บันทึกข้อมูลเริ่มต้น
                return False
        except Exception as e:
            logger.error(f"เกิดข้อผิดพลาดในการโหลดข้อมูล Chat ID: {e}")
            return False
    
    def add_chat_id(self, chat_id: str) -> bool:
        """เพิ่ม Chat ID ใหม่
        
        Args:
            chat_id: Chat ID ที่ต้องการเพิ่ม
            
        Returns:
            bool: True ถ้าเพิ่มสำเร็จ, False ถ้าเพิ่มไม่สำเร็จ (เช่น มีอยู่แล้ว หรือเต็ม)
        """
        # ตรวจสอบว่ามีอยู่แล้วหรือไม่
        if chat_id in self.authorized_chatids[:self.num_authorized_chatids]:
            return False
        
        # ตรวจสอบว่าเต็มหรือไม่
        if self.num_authorized_chatids >= self.max_chat_ids:
            return False
        
        # เพิ่ม Chat ID
        self.authorized_chatids[self.num_authorized_chatids] = chat_id
        self.num_authorized_chatids += 1
        
        # บันทึกลงไฟล์
        self.save()
        return True
    
    def remove_chat_id(self, index: int, old_chat_id: str) -> bool:
        """ลบ Chat ID ตามตำแหน่ง
        
        Args:
            index: ตำแหน่งของ Chat ID (เริ่มจาก 1)
            old_chat_id: Chat ID เดิมที่ต้องตรงกัน
            
        Returns:
            bool: True ถ้าลบสำเร็จ, False ถ้าลบไม่สำเร็จ
        """
        # ปรับตำแหน่งให้เริ่มจาก 0
        array_index = index - 1
        
        # ตรวจสอบว่าตำแหน่งถูกต้องหรือไม่
        if array_index < 0 or array_index >= self.num_authorized_chatids:
            return False
        
        # ตรวจสอบว่า Chat ID ตรงกันหรือไม่
        if self.authorized_chatids[array_index] != old_chat_id:
            return False
        
        # ย้าย Chat IDs ที่เหลือมาแทนที่
        for i in range(array_index, self.num_authorized_chatids - 1):
            self.authorized_chatids[i] = self.authorized_chatids[i + 1]
        
        # ตั้งค่าตำแหน่งสุดท้ายเป็นค่าว่าง
        self.authorized_chatids[self.num_authorized_chatids - 1] = ""
        self.num_authorized_chatids -= 1
        
        # บันทึกลงไฟล์
        self.save()
        return True
    
    def update_chat_id(self, index: int, old_chat_id: str, new_chat_id: str) -> bool:
        """อัปเดต Chat ID ตามตำแหน่ง
        
        Args:
            index: ตำแหน่งของ Chat ID (เริ่มจาก 1)
            old_chat_id: Chat ID เดิมที่ต้องตรงกัน
            new_chat_id: Chat ID ใหม่
            
        Returns:
            bool: True ถ้าอัปเดตสำเร็จ, False ถ้าอัปเดตไม่สำเร็จ
        """
        # ปรับตำแหน่งให้เริ่มจาก 0
        array_index = index - 1
        
        # ตรวจสอบว่าตำแหน่งถูกต้องหรือไม่
        if array_index < 0 or array_index >= self.num_authorized_chatids:
            return False
        
        # ตรวจสอบว่า Chat ID ตรงกันหรือไม่
        if self.authorized_chatids[array_index] != old_chat_id:
            return False
        
        # อัปเดต Chat ID
        self.authorized_chatids[array_index] = new_chat_id
        
        # บันทึกลงไฟล์
        self.save()
        return True
    
    def is_authorized(self, chat_id: str) -> bool:
        """ตรวจสอบว่า Chat ID ได้รับอนุญาตหรือไม่
        
        Args:
            chat_id: Chat ID ที่ต้องการตรวจสอบ
            
        Returns:
            bool: True ถ้าได้รับอนุญาต, False ถ้าไม่ได้รับอนุญาต
        """
        return chat_id in self.authorized_chatids[:self.num_authorized_chatids]
    
    def get_all_chat_ids(self) -> List[str]:
        """ดึงรายการ Chat IDs ทั้งหมดที่ได้รับอนุญาต
        
        Returns:
            List[str]: รายการ Chat IDs ที่ได้รับอนุญาตทั้งหมด
        """
        return self.authorized_chatids[:self.num_authorized_chatids]


class CommandHistory:
    """คลาสสำหรับจัดการประวัติคำสั่ง"""
    
    def __init__(self, max_history: int = 20):
        """
        Args:
            max_history: จำนวนสูงสุดของประวัติคำสั่งที่จะเก็บ
        """
        self.max_history = max_history
        self.history = []
    
    # ในเมธอด record_command ของคลาส CommandHistory
    def record_command(self, device_id: str, command: str, chat_id: str):
        """บันทึกคำสั่งลงในประวัติ"""
        command_info = {
            "device_id": device_id,
            "command": command,
            "chat_id": chat_id,
            "timestamp": time.time()
        }
        
        # เพิ่มข้อมูลลงในประวัติ
        self.history.append(command_info)
        
        # แสดง logging
        logger.debug(f"บันทึกคำสั่ง: device_id={device_id}, command={command}, chat_id={chat_id}")
        
        # ตัดข้อมูลเก่าออกถ้าเกินจำนวนสูงสุด
        if len(self.history) > self.max_history:
            self.history = self.history[-self.max_history:]
    
    def get_last_chat_id(self, device_id: str, command: str = None) -> Optional[str]:
        """ค้นหา chat_id ล่าสุดที่ส่งคำสั่งไปยังอุปกรณ์"""
        newest_time = 0
        best_match = None
        
        logger.debug(f"กำลังค้นหา chat_id สำหรับ device_id={device_id}, command={command}")
        logger.debug(f"ประวัติทั้งหมด: {self.history}")
        
        for cmd_info in self.history:
            if cmd_info["device_id"] == device_id:
                if command is None or cmd_info["command"] == command:
                    if cmd_info["timestamp"] > newest_time:
                        newest_time = cmd_info["timestamp"]
                        best_match = cmd_info
        
        if best_match:
            logger.debug(f"พบ chat_id={best_match['chat_id']} สำหรับ device_id={device_id}")
            return best_match["chat_id"]
        
        logger.debug(f"ไม่พบ chat_id สำหรับ device_id={device_id}")
        return None
    
    def get_device_history(self, device_id: str) -> List[Dict]:
        """ดึงประวัติทั้งหมดที่เกี่ยวข้องกับอุปกรณ์
        
        Args:
            device_id: Device ID ที่ต้องการค้นหา
            
        Returns:
            List[Dict]: รายการประวัติทั้งหมดของอุปกรณ์
        """
        return [cmd for cmd in self.history if cmd["device_id"] == device_id]