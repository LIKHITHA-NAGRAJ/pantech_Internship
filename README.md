# Smart Environment Guardian

A multi-sensor indoor/outdoor environment monitoring system built on the ESP8266 (ESP-12E). Fetches live outdoor weather from OpenWeatherMap, reads indoor sensor data in real time, logs everything to ThingSpeak, and delivers instant Telegram alerts when thresholds are breached.

---

## Overview

Most IoT environment monitors track a single parameter — temperature, or humidity in isolation. This project takes a different approach: it aggregates four indoor sensors alongside a live outdoor weather feed, giving a complete picture of your environment and enabling meaningful comparisons like indoor vs outdoor temperature differential.

Built as part of a structured IoT learning roadmap covering cloud integration, REST API consumption, JSON parsing, and multi-sensor data fusion on a resource-constrained microcontroller.

---

## Features

- Live outdoor weather via OpenWeatherMap REST API with JSON parsing
- Indoor monitoring — temperature, humidity, flame detection, ambient light, sound
- Eight-field ThingSpeak dashboard with real-time graphs
- Telegram bot alerts on threshold breach — no third-party automation platform required
- ThingSpeak React and ThingHTTP configured as a secondary alert layer
- OLED display showing indoor vs outdoor comparison with temperature differential
- Local buzzer on flame detection — no cloud dependency for safety-critical response
- Automatic WiFi reconnection on disconnect
- Alert cooldown system — prevents notification spam on sustained threshold breach

---

## Hardware

| Component | Purpose |
|---|---|
| ESP8266 ESP-12E (NodeMCU) | Main microcontroller |
| DHT11 | Indoor temperature and humidity |
| Flame sensor module | Fire/IR detection via analog output |
| LDR module | Ambient light detection via digital output |
| Sound sensor module | Noise detection via digital output |
| SSD1306 OLED (128x64, I2C) | Live data display |
| Buzzer | Local flame alert |
| Breadboard + jumper wires | Prototyping |

---

## Wiring

| ESP8266 Pin | Component | Connection |
|---|---|---|
| 3.3V | DHT11 | VCC |
| GND | DHT11 | GND |
| D2 (GPIO4) | DHT11 | DATA (with 10K pull-up to VCC) |
| 3.3V | Flame sensor | VCC |
| GND | Flame sensor | GND |
| A0 | Flame sensor | AO (analog output) |
| 3.3V | LDR module | VCC |
| GND | LDR module | GND |
| D5 (GPIO14) | LDR module | DO (digital output) |
| 3.3V | Sound sensor | VCC |
| GND | Sound sensor | GND |
| D6 (GPIO12) | Sound sensor | DO (digital output) |
| 3.3V | OLED | VCC |
| GND | OLED | GND |
| D3 (GPIO0) | OLED | SDA |
| D4 (GPIO2) | OLED | SCL |
| D7 (GPIO13) | Buzzer | Positive |
| GND | Buzzer | Negative |

> Note: The ESP8266 has a single analog input (A0). Flame is the only analog sensor. LDR and sound use their digital output pins, giving binary detection rather than percentage values.

---

## ThingSpeak Channel Fields

| Field | Data | Source |
|---|---|---|
| Field 1 | Outdoor temperature (°C) | OpenWeatherMap API |
| Field 2 | Outdoor humidity (%) | OpenWeatherMap API |
| Field 3 | Outdoor pressure (hPa) | OpenWeatherMap API |
| Field 4 | Wind speed (m/s) | OpenWeatherMap API |
| Field 5 | Indoor temperature (°C) | DHT11 |
| Field 6 | Indoor humidity (%) | DHT11 |
| Field 7 | Flame detected (1/0) | Flame sensor |
| Field 8 | Light status (1=dark, 0=bright) | LDR module |

---

## Alert System

### Layer 1 — Direct from ESP8266 (Telegram)
The microcontroller itself sends Telegram messages via the Bot API using HTTP POST. No IFTTT or intermediate service required.

| Condition | Alert |
|---|---|
| Flame sensor analog < 500 | Immediate buzzer + Telegram (fires every loop, not on timer) |
| Indoor temp > 35°C | Telegram alert (5 minute cooldown) |
| Indoor humidity > 75% | Telegram alert (5 minute cooldown) |

### Layer 2 — ThingSpeak React + ThingHTTP (Backup)
Configured on the ThingSpeak dashboard as a secondary alert path. React monitors Field 7 (flame) and triggers a ThingHTTP GET request to the Telegram Bot API when value equals 1.

---

## Software Setup

### Arduino IDE Configuration

1. Add ESP8266 board support — paste this URL in File > Preferences > Additional Board Manager URLs:
   ```
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
2. Install board: Tools > Board > Boards Manager > search `esp8266` > Install
3. Select board: Tools > Board > ESP8266 Boards > **NodeMCU 1.0 (ESP-12E Module)**
4. Set upload speed: 115200

### Libraries Required

Install via Library Manager (Sketch > Include Library > Manage Libraries):

- ThingSpeak — by MathWorks
- DHT sensor library — by Adafruit
- Adafruit Unified Sensor — by Adafruit
- Adafruit SSD1306 — by Adafruit
- Adafruit GFX Library — by Adafruit

Install via ZIP (Sketch > Include Library > Add .ZIP Library):

- Arduino_JSON — from the provided zip file

---

## API Keys and Credentials

You will need accounts and keys from the following services — all free tier:

**OpenWeatherMap**
- Sign up at openweathermap.org
- Navigate to API Keys and copy your key
- Free tier allows 60 calls/minute — more than sufficient

**ThingSpeak**
- Create a channel at thingspeak.com with 8 fields
- Copy the Channel ID and Write API Key from the API Keys tab

**Telegram**
- Create a bot via @BotFather — send `/newbot` and follow prompts — copy the token
- Get your chat ID via @userinfobot — send any message and it replies with your ID
- Send `/start` to your bot before first use

---

## Configuration

Open `SmartEnvironmentGuardian_ESP8266.ino` and fill in the credentials section at the top:

```cpp
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

unsigned long myChannelNumber = 0;              // ThingSpeak channel ID
const char*   myWriteAPIKey   = "XXXXXXXXXX";  // ThingSpeak write key

String openWeatherMapApiKey = "XXXXXXXXXX";
String city        = "Bengaluru";               // Your city name
String countryCode = "IN";

String botToken = "XXXXXXXXXX";                 // Telegram bot token
String chatID   = "XXXXXXXXXX";                 // Telegram chat ID
```

Upload interval is set to 30 seconds by default. ThingSpeak free tier requires a minimum of 15 seconds between updates.

---

## ThingSpeak React Configuration

1. Go to Apps > ThingHTTP > New ThingHTTP
   - URL: `https://api.telegram.org/botYOUR_TOKEN/sendMessage?chat_id=YOUR_CHAT_ID&text=Flame+Alert`
   - Method: GET
2. Go to Apps > React > New React
   - Test frequency: On Data Insertion
   - Condition: Field 7 equals 1
   - Action: ThingHTTP — select the one created above

---

## Serial Monitor Output

At 115200 baud you will see structured output each cycle:

```
── Indoor ──
Temp     : 29.0 C
Humidity : 58.0 %
Flame Raw: 987 → Clear
LDR      : Bright
Sound    : Quiet
── Fetching Outdoor Weather ──
Weather API response: 200
── Outdoor ──
Temp     : 24 C
Humidity : 72 %
Pressure : 1012 hPa
Wind     : 3.2 m/s
── Uploading to ThingSpeak ──
ThingSpeak upload OK
```

---

## Project Context

This project is part of a 35-project IoT learning roadmap progressing from basic GPIO through cloud integrations, RTOS, STM32 bare metal programming, and edge AI. It covers Pantech IoT internship concepts P4 and P5 — ThingSpeak React alerting and OpenWeatherMap integration — implemented on original hardware with a different use case.

Skills demonstrated: REST API consumption, JSON parsing on a microcontroller, multi-sensor data fusion, HTTPS POST on ESP8266, ThingSpeak field management, React and ThingHTTP configuration, Telegram Bot API integration.

---

## License

MIT License. Free to use, modify, and distribute with attribution.
