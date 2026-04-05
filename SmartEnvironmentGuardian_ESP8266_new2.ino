/*
 * ============================================================
 *   SMART ENVIRONMENT GUARDIAN — Indoor vs Outdoor Monitor
 *   Board : ESP8266 (ESP12-E / NodeMCU)
 * ============================================================
 *
 * FEATURES:
 *  - Fetches outdoor weather from OpenWeatherMap API
 *  - Reads indoor sensors: DHT11, Flame (analog), LDR, Sound (digital)
 *  - Pushes 8 fields to ThingSpeak
 *  - OLED dashboard — Indoor vs Outdoor comparison
 *  - Buzzer on flame detection
 *  - Telegram alerts — direct, no IFTTT needed
 *
 * THINGSPEAK FIELDS:
 *  Field 1 — Outdoor Temp (°C)
 *  Field 2 — Outdoor Humidity (%)
 *  Field 3 — Outdoor Pressure (hPa)
 *  Field 4 — Wind Speed (m/s)
 *  Field 5 — Indoor Temp (°C)
 *  Field 6 — Indoor Humidity (%)
 *  Field 7 — Flame Detected (1=Yes, 0=No)
 *  Field 8 — Light Status (1=Dark, 0=Bright)
 *
 * LIBRARIES TO INSTALL IN ARDUINO IDE:
 *  1. ESP8266WiFi        — comes with ESP8266 board package
 *  2. ESP8266HTTPClient  — comes with ESP8266 board package
 *  3. ThingSpeak         — by MathWorks (Library Manager)
 *  4. DHT sensor library — by Adafruit (Library Manager)
 *  5. Adafruit Unified Sensor — by Adafruit (Library Manager)
 *  6. Adafruit SSD1306   — by Adafruit (Library Manager)
 *  7. Adafruit GFX       — by Adafruit (Library Manager)
 *  8. Arduino_JSON       — install from the ZIP file provided
 *
 * ============================================================
 */

// ─── Core Libraries (ESP8266 versions) ────────────────────
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>        // needed for HTTPS (Telegram)
#include <Arduino_JSON.h>
#include "ThingSpeak.h"

// ─── Sensor Libraries ─────────────────────────────────────
#include <DHT.h>

// ─── OLED Libraries ───────────────────────────────────────
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================
//   PIN DEFINITIONS — ESP8266 (ESP12-E / NodeMCU)
// ============================================================
//
//  NodeMCU label → GPIO number mapping:
//  D0 = GPIO16   D1 = GPIO5    D2 = GPIO4
//  D3 = GPIO0    D4 = GPIO2    D5 = GPIO14
//  D6 = GPIO12   D7 = GPIO13   D8 = GPIO15
//  A0 = ADC0 (only analog pin)
//
#define DHTPIN        D2          // DHT11 data → D2 (GPIO4)
#define DHTTYPE       DHT11

#define FLAME_PIN     A0          // Flame sensor AO → A0 (only analog pin)
#define LDR_PIN       D5          // LDR DO (digital) → D5
#define SOUND_PIN     D6          // Sound sensor DO (digital) → D6
#define BUZZER_PIN    D7          // Buzzer → D7

// OLED — I2C
// SDA → D2... wait, D2 is used by DHT11
// So OLED uses default I2C pins:
// SDA → D2 is taken, so use:
// SDA → D4 (GPIO2)  SCL → D1 (GPIO5)
// Actually Wire uses D1=SCL, D2=SDA by default on NodeMCU
// Since D2 is used by DHT, we remap I2C to D3, D4
// See Wire.begin(SDA, SCL) in setup()
#define OLED_SDA      D3          // OLED SDA → D3 (GPIO0)
#define OLED_SCL      D4          // OLED SCL → D4 (GPIO2)
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C

// ============================================================
//   YOUR CREDENTIALS — Fill ALL of these before uploading
// ============================================================

// WiFi
const char* ssid     = "Redmi Note 13 5G";
const char* password = "12345678";

// ThingSpeak
unsigned long myChannelNumber = 3326546;            // Your Channel ID (numbers only)
const char*   myWriteAPIKey   = "AQXD4T93NQTWCY2S";

// OpenWeatherMap
String openWeatherMapApiKey = "Open_api_keys";
String city        = "Bengaluru";             // Change to your city
String countryCode = "IN";

// Telegram
String botToken = "7906308499:AAHt5afAxQkR2nMIPL6veO7WgFzWrXthHTE";           // From @BotFather
String chatID   = "7908774265";             // From @userinfobot

// ============================================================
//   TIMING
// ============================================================
unsigned long lastUploadTime  = 0;
unsigned long uploadInterval  = 30000;        // Upload every 30 seconds

// Alert cooldown — 5 minutes between same type of alert
unsigned long lastFlameAlert  = 0;
unsigned long lastTempAlert   = 0;
unsigned long lastHumidAlert  = 0;
unsigned long alertCooldown   = 300000;

// ============================================================
//   OBJECTS
// ============================================================
WiFiClient          wifiClient;
DHT                 dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306    display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
String              jsonBuffer;

// ============================================================
//   SENSOR DATA VARIABLES
// ============================================================
// Outdoor — from OpenWeatherMap
int   outdoorTemp     = 0;
int   outdoorHumidity = 0;
int   outdoorPressure = 0;
double windSpeed      = 0.0;

// Indoor — from sensors
float indoorTemp      = 0.0;
float indoorHumidity  = 0.0;
int   flameRaw        = 0;      // 0-1023 analog value
int   flameDetected   = 0;      // 1 = flame, 0 = clear
int   ldrStatus       = 0;      // 1 = dark, 0 = bright (digital)
int   soundStatus     = 0;      // 1 = sound detected, 0 = quiet (digital)

// ============================================================
//   SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\nSmart Environment Guardian — Starting...");

  // ── Pin Modes ──
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(LDR_PIN,   INPUT);
  pinMode(SOUND_PIN, INPUT);
  // FLAME_PIN (A0) and DHTPIN are input by default

  // ── I2C for OLED on custom pins ──
  Wire.begin(OLED_SDA, OLED_SCL);  // SDA=D3, SCL=D4

  // ── DHT Start ──
  dht.begin();

  // ── OLED Start ──
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED not found! Check wiring and address (0x3C or 0x3D).");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(" Smart Environment");
    display.println("     Guardian");
    display.println("   ESP8266 v1.0");
    display.println(" Connecting WiFi...");
    display.display();
  }

  // ── WiFi Connect ──
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    if (attempts > 40) {
      Serial.println("\nWiFi failed! Check credentials. Restarting...");
      ESP.restart();
    }
  }

  Serial.println("\nWiFi Connected!");
  Serial.println("IP Address: " + WiFi.localIP().toString());

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.println(WiFi.localIP().toString());
  display.display();
  delay(2000);

  // ── ThingSpeak Start ──
  ThingSpeak.begin(wifiClient);

  sendTelegram("ESP8266 Smart Guardian is Online!");
}

// ============================================================
//   MAIN LOOP
// ============================================================
void loop() {

  // ── Read sensors every loop cycle ──
  readIndoorSensors();

  // ── Flame — immediate buzzer (no timer needed) ──
  if (flameDetected == 1) {
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("*** FLAME DETECTED — Buzzer ON ***");

    // Telegram flame alert with cooldown
    if ((millis() - lastFlameAlert) > alertCooldown) {
      sendTelegram("FIRE ALERT! Flame detected! Check immediately!");
      lastFlameAlert = millis();
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }

  // ── Update OLED ──
  updateOLED();

  // ── Timed upload to ThingSpeak ──
  if ((millis() - lastUploadTime) > uploadInterval) {

    if (WiFi.status() == WL_CONNECTED) {
      fetchOutdoorWeather();     // Get outdoor data
      uploadToThingSpeak();      // Push all 8 fields
      checkAndAlert();           // Check thresholds → Telegram
    } else {
      Serial.println("WiFi lost! Reconnecting...");
      WiFi.begin(ssid, password);
      delay(5000);
    }

    lastUploadTime = millis();
  }

  delay(1000);
}

// ============================================================
//   READ INDOOR SENSORS
// ============================================================
void readIndoorSensors() {

  // DHT11 — temperature and humidity
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (!isnan(h) && !isnan(t)) {
    indoorTemp     = t;
    indoorHumidity = h;
  } else {
    Serial.println("DHT11 read failed — retrying next cycle");
  }

  // Flame sensor — analog on A0
  // Lower value = more flame (IR reflection logic)
  // Typical: no flame ~900-1023, flame ~0-400
  flameRaw      = analogRead(FLAME_PIN);
  flameDetected = (flameRaw < 500) ? 1 : 0;

  // LDR — digital DO pin
  // DO = 0 means light detected (bright), 1 means dark
  ldrStatus = digitalRead(LDR_PIN);

  // Sound sensor — digital DO pin
  // DO = 1 means sound detected
  soundStatus = digitalRead(SOUND_PIN);

  // Serial output
  Serial.println("── Indoor ──");
  Serial.println("Temp     : " + String(indoorTemp, 1)     + " C");
  Serial.println("Humidity : " + String(indoorHumidity, 1) + " %");
  Serial.println("Flame Raw: " + String(flameRaw) + " → " + (flameDetected ? "DETECTED" : "Clear"));
  Serial.println("LDR      : " + String(ldrStatus  ? "Dark" : "Bright"));
  Serial.println("Sound    : " + String(soundStatus ? "Detected" : "Quiet"));
}

// ============================================================
//   FETCH OUTDOOR WEATHER — OpenWeatherMap API
// ============================================================
void fetchOutdoorWeather() {
  Serial.println("── Fetching Outdoor Weather ──");

  WiFiClient httpClient;
  HTTPClient http;

  String serverPath = "http://api.openweathermap.org/data/2.5/weather?q="
                      + city + "," + countryCode
                      + "&APPID=" + openWeatherMapApiKey;

  http.begin(httpClient, serverPath);
  int httpCode = http.GET();

  if (httpCode > 0) {
    jsonBuffer = http.getString();
    Serial.println("Weather API response: " + String(httpCode));

    JSONVar myObject = JSON.parse(jsonBuffer);

    if (JSON.typeof(myObject) == "undefined") {
      Serial.println("JSON parse failed!");
      http.end();
      return;
    }

    // Extract values
    JSONVar temp     = myObject["main"]["temp"];
    JSONVar pressure = myObject["main"]["pressure"];
    JSONVar humidity = myObject["main"]["humidity"];
    JSONVar wind     = myObject["wind"]["speed"];

    // Kelvin → Celsius
    outdoorTemp     = (int)temp - 273;
    outdoorPressure = (int)pressure;
    outdoorHumidity = (int)humidity;
    windSpeed       = (double)wind;

    Serial.println("── Outdoor ──");
    Serial.println("Temp     : " + String(outdoorTemp)     + " C");
    Serial.println("Humidity : " + String(outdoorHumidity) + " %");
    Serial.println("Pressure : " + String(outdoorPressure) + " hPa");
    Serial.println("Wind     : " + String(windSpeed, 1)    + " m/s");

  } else {
    Serial.println("Weather API failed. Code: " + String(httpCode));
  }

  http.end();
}

// ============================================================
//   UPLOAD TO THINGSPEAK — All 8 Fields
// ============================================================
void uploadToThingSpeak() {
  Serial.println("── Uploading to ThingSpeak ──");

  // Outdoor fields
  ThingSpeak.setField(1, outdoorTemp);
  ThingSpeak.setField(2, outdoorHumidity);
  ThingSpeak.setField(3, outdoorPressure);
  ThingSpeak.setField(4, (float)windSpeed);

  // Indoor fields
  ThingSpeak.setField(5, indoorTemp);
  ThingSpeak.setField(6, indoorHumidity);
  ThingSpeak.setField(7, flameDetected);
  ThingSpeak.setField(8, ldrStatus);

  int result = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

  if (result == 200) {
    Serial.println("ThingSpeak upload OK");
  } else {
    Serial.println("ThingSpeak failed. Error: " + String(result));
  }
}

// ============================================================
//   SEND TELEGRAM MESSAGE
// ============================================================
void sendTelegram(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Telegram skipped - no WiFi");
    return;
  }

  Serial.println("Sending Telegram message...");

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;

  String url = "https://api.telegram.org/bot" + botToken + "/sendMessage";

  http.begin(secureClient, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String cleanMsg = message;
  cleanMsg.replace(" ", "+");
  cleanMsg.replace("!", "%21");
  cleanMsg.replace("|", "%7C");

  String postData = "chat_id=" + chatID + "&text=" + cleanMsg;
  Serial.println("Data: " + postData);

  int responseCode = http.POST(postData);

  if (responseCode > 0) {
    Serial.println("Telegram sent! Code: " + String(responseCode));
    Serial.println("Response: " + http.getString());
  } else {
    Serial.println("Telegram failed. Code: " + String(responseCode));
    Serial.println("Error: " + http.errorToString(responseCode));
  }

  http.end();
}

// ============================================================
//   CHECK THRESHOLDS — Send Telegram alerts if needed
// ============================================================
void checkAndAlert() {

  // 1. High indoor temperature
  if (indoorTemp > 35) {
    if ((millis() - lastTempAlert) > alertCooldown) {
      String msg = "HIGH TEMP ALERT! Indoor: " + String(indoorTemp, 1) +
                   "C | Outdoor: " + String(outdoorTemp) + "C";
      sendTelegram(msg);
      lastTempAlert = millis();
      Serial.println("Telegram: High temp alert sent");
    }
  }

  // 2. High indoor humidity
  if (indoorHumidity > 75) {
    if ((millis() - lastHumidAlert) > alertCooldown) {
      String msg = "HIGH HUMIDITY ALERT! Indoor: " +
                   String(indoorHumidity, 0) + "% — Check ventilation!";
      sendTelegram(msg);
      lastHumidAlert = millis();
      Serial.println("Telegram: High humidity alert sent");
    }
  }

  // 3. Indoor much hotter than outdoor
  if ((indoorTemp - outdoorTemp) > 8) {
    Serial.println("Note: Indoor " + String(indoorTemp - outdoorTemp, 1) +
                   "C hotter than outdoor");
  }
}

// ============================================================
//   UPDATE OLED DISPLAY
// ============================================================
void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setCursor(0, 0);
  display.println("==Smart Guardian==");

  // Indoor readings
  display.setCursor(0, 12);
  display.print("IN  T:");
  display.print(indoorTemp, 1);
  display.print("C H:");
  display.print(indoorHumidity, 0);
  display.println("%");

  // Outdoor readings
  display.setCursor(0, 22);
  display.print("OUT T:");
  display.print(outdoorTemp);
  display.print("C H:");
  display.print(outdoorHumidity);
  display.println("%");

  // Temp difference
  display.setCursor(0, 32);
  float diff = indoorTemp - outdoorTemp;
  display.print("Diff:");
  if (diff > 0) display.print("+");
  display.print(diff, 1);
  display.print("C W:");
  display.print(windSpeed, 1);

  // LDR + Sound status
  display.setCursor(0, 42);
  display.print("Light:");
  display.print(ldrStatus ? "Dark " : "Bright");
  display.print(" Snd:");
  display.println(soundStatus ? "YES" : "No");

  // Bottom row — flame alert or pressure
  display.setCursor(0, 54);
  if (flameDetected == 1) {
    display.println("*** FLAME ALERT! ***");
  } else {
    display.print("Press:");
    display.print(outdoorPressure);
    display.println("hPa");
  }

  display.display();
}
