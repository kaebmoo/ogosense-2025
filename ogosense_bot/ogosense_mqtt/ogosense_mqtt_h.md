// ตั้งค่า WiFi
const char *ssid = "xxxx";             // แทนที่ด้วยชื่อ WiFi ของคุณ
const char *password = "xxxxx";     // แทนที่ด้วยรหัส WiFi ของคุณ

// ตั้งค่า Telegram Bot
#ifdef ESP8266
  X509List cert(TELEGRAM_CERTIFICATE_ROOT);
#endif
#define BOT_TOKEN "xxxxx:x"  // แทนที่ด้วย Token ของ Telegram Bot ของคุณ
#define CHAT_ID "xxx"  // Replace with your actual chat ID
WiFiClientSecure clientSecure;
UniversalTelegramBot bot(BOT_TOKEN, clientSecure);

// ตั้งค่า MQTT with TLS/SSL
const char *mqtt_broker = "i31286ee.ala.eu-central-1.emqxsl.com";  // แทนที่ด้วย EMQX broker endpoint ของคุณ
const int mqtt_port = 8883;  // MQTT port (TLS)
const char *mqtt_username = "xxx";  // แทนที่ด้วย username MQTT ของคุณ
const char *mqtt_password = "xxxx";  // แทนที่ด้วย password MQTT ของคุณ
const char *mqtt_topic = "command/devices";  // แทนที่ด้วย MQTT topic ของคุณ

// SSL certificate for MQTT broker
// ใบรับรองจาก EMQX Cloud
static const char ca_cert[]
PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----

-----END CERTIFICATE-----
)EOF";