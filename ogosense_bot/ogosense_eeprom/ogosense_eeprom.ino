#include <EEPROM.h>
#include <ESP8266WiFi.h>         // สำหรับ ESP8266 (Wemos D1 mini pro)
#include <WiFiManager.h>
#include "ogosense_eeprom.h"

float lowTemp  = 26.0;   // เมื่ออุณหภูมิต่ำกว่าค่านี้ให้ปิด Relay
float highTemp = 30.0;   // เมื่ออุณหภูมิสูงกว่าค่านี้ให้เปิด Relay
float lowHumidity = 55;
float highHumidity = 60;
int options = 1;          // options : 0 = humidity only, 1 = temperature only, 2 = temperature & humidity
int COOL = 1;             // COOL: 1 = COOL mode (relay ON เมื่ออุณหภูมิสูงเกิน highTemp), 0 = HEAT mode (relay OFF เมื่ออุณหภูมิต่ำกว่า lowTemp)
int MOISTURE = 0;         // MOISTURE: 0 = moisture mode (relay ON เมื่อความชื้นต่ำกว่า lowHumidity), 0 = dehumidifier mode (relay OFF เมื่อความชื้นสูงกว่า highHumidity)

const char* allowedChatIDs[MAX_ALLOWED_CHATIDS] = {"REDACTED", "REDACTED"};
int numAllowedChatIDs = 2;

// ค่าตำแหน่ง EEPROM (ต้องตรงกับที่ใช้ในโปรแกรมหลัก)
#define EEPROM_ADDR_HIGH_HUMIDITY     0   // 4 bytes
#define EEPROM_ADDR_LOW_HUMIDITY      4   // 4 bytes
#define EEPROM_ADDR_HIGH_TEMP         8   // 4 bytes
#define EEPROM_ADDR_LOW_TEMP         12   // 4 bytes
#define EEPROM_ADDR_OPTIONS          16   // 4 bytes (int)
#define EEPROM_ADDR_COOL             20   // 4 bytes (int)
#define EEPROM_ADDR_MOISTURE         24   // 4 bytes (int)
#define EEPROM_ADDR_WRITE_APIKEY     28   // 16 bytes
#define EEPROM_ADDR_READ_APIKEY      44   // 16 bytes
#define EEPROM_ADDR_AUTH             60   // 32 bytes
#define EEPROM_ADDR_CHANNEL_ID       92   // 4 bytes
#define EEPROM_ADDR_DEVICE_NAME     96   // 201 bytes (UTF-8 with Thai, safe length)
#define EEPROM_ADDR_VERSION_MARK   500   // 4 bytes (for config version marker or flag)

// เพิ่มตำแหน่ง EEPROM ใหม่
#define EEPROM_ADDR_DEVICE_ID       300   // 4 bytes (int)

// #define EEPROM_ADDR_TELEGRAM_TOKEN  304   // 50 bytes
// #define EEPROM_ADDR_ALLOWED_CHATIDS 354   // 100 bytes (คั่นด้วย comma)

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(100);
  Serial.println("Starting:");
  
  autoWifiConnect();
  Serial.println("Setup config:");
  Serial.println("Default config:");
  printConfig();
  Serial.println("Saving config:");
  saveConfig();
  delay(100);
  Serial.println("Reading config:");
  getConfig();

  EEPROM.begin(512);
  int saved = eeGetInt(EEPROM_ADDR_VERSION_MARK);
  if (saved == 6550) {
    Serial.println("Saved config:");
    printConfig();
  }
  else {
    Serial.println("Can't read 6550");
  }
  EEPROM.end();

}

void printConfig()
{
  Serial.println("Configuration");

  Serial.print("Temperature High: ");
  Serial.println(highTemp);
  Serial.print("Temperature Low: ");
  Serial.println(lowTemp);

  Serial.print("Humidity High: ");
  Serial.println(highHumidity);
  Serial.print("Humidity Low: ");
  Serial.println(lowHumidity);

  Serial.print("Option: ");
  Serial.println(options);

  Serial.print("COOL: ");
  Serial.println(COOL);

  Serial.print("MOISTURE: ");
  Serial.println(MOISTURE);

  Serial.print("Write API Key: ");
  Serial.println(writeAPIKey);

  Serial.print("Read API Key: ");
  Serial.println(readAPIKey);

  Serial.print("Channel ID: ");
  Serial.println(channelID);
  
  Serial.print("Device ID: ");
  Serial.println(DEVICE_ID);
  
  Serial.print("Device Name: ");
  Serial.println(deviceName);
  
  // แสดงรายการ Chat IDs
  Serial.print("Allowed Chat IDs: ");
  for (int i = 0; i < numAllowedChatIDs; i++) {
    if (i > 0) Serial.print(", ");
    Serial.print(allowedChatIDs[i]);
  }
  Serial.println();
}

void saveConfig()
{
  EEPROM.begin(512);
  
  // ค่าเดิม
  EEPROMWritelong(EEPROM_ADDR_HIGH_HUMIDITY, (long) highHumidity);
  EEPROMWritelong(EEPROM_ADDR_LOW_HUMIDITY, (long) lowHumidity);
  EEPROMWritelong(EEPROM_ADDR_HIGH_TEMP, (long) highTemp);
  EEPROMWritelong(EEPROM_ADDR_LOW_TEMP, (long) lowTemp);
  eeWriteInt(EEPROM_ADDR_OPTIONS, options);
  eeWriteInt(EEPROM_ADDR_COOL, COOL);
  eeWriteInt(EEPROM_ADDR_MOISTURE, MOISTURE);
  writeEEPROM(writeAPIKey, EEPROM_ADDR_WRITE_APIKEY, 16);
  writeEEPROM(readAPIKey, EEPROM_ADDR_READ_APIKEY, 16);
  writeEEPROM(auth, EEPROM_ADDR_AUTH, 32);
  EEPROMWritelong(EEPROM_ADDR_CHANNEL_ID, (long) channelID);
  
  // เพิ่มส่วนใหม่
  EEPROMWritelong(EEPROM_ADDR_DEVICE_ID, (long) DEVICE_ID);
  writeEEPROM(deviceName, EEPROM_ADDR_DEVICE_NAME, DEVICE_NAME_MAX_BYTES);
  
  // เพิ่มส่วนของ telegramToken (ใช้ค่าจาก ogosense_eeprom.h)
  // writeEEPROM((char*)telegramToken, EEPROM_ADDR_TELEGRAM_TOKEN, 50);
  
  // สร้าง string ของ allowedChatIDs ที่คั่นด้วย comma
  /*
  char chatIDsStr[100] = "";
  for (int i = 0; i < numAllowedChatIDs; i++) {
    if (i > 0) strcat(chatIDsStr, ",");
    strcat(chatIDsStr, allowedChatIDs[i]);
  }
  writeEEPROM(chatIDsStr, EEPROM_ADDR_ALLOWED_CHATIDS, 100);
  */
  
  // ส่วนของ Version Mark (ต้องเป็นตัวสุดท้าย)
  eeWriteInt(EEPROM_ADDR_VERSION_MARK, 6550);
  EEPROM.end();

  delay(100);
  EEPROM.begin(512);
  int check = eeGetInt(EEPROM_ADDR_VERSION_MARK);
  Serial.print("Immediate check version mark: ");
  Serial.println(check);
  EEPROM.end();
  Serial.println("Configuration saved to EEPROM");
}

void getConfig()
{
  // read config from eeprom
  EEPROM.begin(512);
  
  // อ่านค่าเดิม
  highHumidity = (float) EEPROMReadlong(EEPROM_ADDR_HIGH_HUMIDITY);
  lowHumidity = (float) EEPROMReadlong(EEPROM_ADDR_LOW_HUMIDITY);
  highTemp = (float) EEPROMReadlong(EEPROM_ADDR_HIGH_TEMP);
  lowTemp = (float) EEPROMReadlong(EEPROM_ADDR_LOW_TEMP);
  options = eeGetInt(EEPROM_ADDR_OPTIONS);
  COOL = eeGetInt(EEPROM_ADDR_COOL);
  MOISTURE = eeGetInt(EEPROM_ADDR_MOISTURE);
  readEEPROM(writeAPIKey, EEPROM_ADDR_WRITE_APIKEY, 16);
  readEEPROM(readAPIKey, EEPROM_ADDR_READ_APIKEY, 16);
  readEEPROM(auth, EEPROM_ADDR_AUTH, 32);
  channelID = (unsigned long) EEPROMReadlong(EEPROM_ADDR_CHANNEL_ID);
  
  // อ่านค่าใหม่
  int deviceID = (int) EEPROMReadlong(EEPROM_ADDR_DEVICE_ID);
  if (deviceID > 0) { // ตรวจสอบความถูกต้องเบื้องต้น
    DEVICE_ID = deviceID;
  }
  
  char tempDeviceName[DEVICE_NAME_MAX_BYTES + 1];
  readEEPROM(tempDeviceName, EEPROM_ADDR_DEVICE_NAME, DEVICE_NAME_MAX_BYTES);
  if (strlen(tempDeviceName) > 0) { // ตรวจสอบความถูกต้องเบื้องต้น
    strcpy(deviceName, tempDeviceName);
  }
  
  // อ่านและแปลง allowedChatIDs
  /*
  char chatIDsStr[100];
  readEEPROM(chatIDsStr, EEPROM_ADDR_ALLOWED_CHATIDS, 100);
  
  // แสดงค่าที่อ่านได้
  Serial.print("Read Chat IDs: ");
  Serial.println(chatIDsStr);
  */
  
  EEPROM.end();
}

void autoWifiConnect()
{
  WiFiManager wifiManager;
  bool res;

  //first parameter is name of access point, second is the password
  res = wifiManager.autoConnect("ogosense-config", "12345678");

  if(!res) {
      Serial.println("Failed to connect");
      delay(3000);
      ESP.restart();
      delay(5000);
  } 
  else {
      //if you get here you have connected to the WiFi    
      Serial.println("connected...yeey :)");
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  delay(1000);
}

void eeWriteInt(int pos, int val) {
    byte* p = (byte*) &val;
    EEPROM.write(pos, *p);
    EEPROM.write(pos + 1, *(p + 1));
    EEPROM.write(pos + 2, *(p + 2));
    EEPROM.write(pos + 3, *(p + 3));
    EEPROM.commit();
}

int eeGetInt(int pos) {
  int val;
  byte* p = (byte*) &val;
  *p        = EEPROM.read(pos);
  *(p + 1)  = EEPROM.read(pos + 1);
  *(p + 2)  = EEPROM.read(pos + 2);
  *(p + 3)  = EEPROM.read(pos + 3);
  return val;
}

void EEPROMWritelong(int address, long value)
{
  //Decomposition from a long to 4 bytes by using bitshift.
  //One = Most significant -> Four = Least significant byte
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);

  Serial.print(four);
  Serial.print(" ");
  Serial.print(three);
  Serial.print(" ");
  Serial.print(two);
  Serial.print(" ");
  Serial.print(one);
  Serial.println();

  //Write the 4 bytes into the eeprom memory.
  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
  EEPROM.commit();
}

long EEPROMReadlong(int address)
{
  //Read the 4 bytes from the eeprom memory.
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);

  Serial.print(four);
  Serial.print(" ");
  Serial.print(three);
  Serial.print(" ");
  Serial.print(two);
  Serial.print(" ");
  Serial.print(one);
  Serial.println();

  //Return the recomposed long by using bitshift.
  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

void readEEPROM(char* buff, int offset, int len) {
    int i;
    for (i=0;i<len;i++) {
        buff[i] = (char)EEPROM.read(offset+i);
    }
    buff[len] = '\0';
}

void writeEEPROM(char* buff, int offset, int len) {
    int i;
    for (i=0;i<len;i++) {
        EEPROM.write(offset+i,buff[i]);
    }
    EEPROM.commit();
}