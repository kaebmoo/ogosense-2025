# Ogosense Python App

A Python-based MQTT and Telegram bot service designed to replace the functionality of an ESP32 IoT controller. This app runs on DigitalOcean App Platform and listens to Telegram messages, forwards them to an MQTT broker, and relays device responses back to users via Telegram.

## 🚀 Features
- Secure MQTT over TLS
- Authorized Telegram chat control
- Command history tracking
- Real-time device response feedback
- Deployable to DigitalOcean App Platform

## 📦 Requirements
- Python 3.10+
- MQTT broker (e.g., EMQX)
- Telegram Bot Token

## 🛠 Setup

```bash
# Clone the repo
git clone https://github.com/yourusername/ogosense_bot/ogosense_mqtt_python.git
cd ogosense_bot/ogosense_mqtt_python

# Install dependencies
pip install -r requirements.txt

# Rename and configure .env
cp .env.example .env
nano .env

# Run
python main.py
