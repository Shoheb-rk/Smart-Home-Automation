/************************************************************
 *  Smart Home Automation & Security System
 *  ─────────────────────────────────────────────────────────
 *  MCU      : ESP32 DevKit V1
 *  Control  : Google Assistant → SinricPro
 *  Alerts   : Telegram Bot
 *  Sensors  : PIR, MQ2, DHT22, LDR
 *  Output   : LED, Buzzer, L298N + Toy Motor
 *
 *  ── CONFIGURATION ────────────────────────────────────────
 *  Public repository version: credentials are placeholders.
 *  Add your real credentials only in your private local copy.
 *
 *  ── GPIO MAP ─────────────────────────────────────────────
 *  GPIO 21 → LED
 *  GPIO 19 → PIR sensor OUT
 *  GPIO 16 → MQ2 DOUT  (LOW = gas detected)
 *  GPIO 25 → Buzzer
 *  GPIO 4  → DHT22 DATA
 *  GPIO 26 → L298N IN1
 *  GPIO 27 → L298N IN2
 *  GPIO 34 → LDR (analog, ADC only pin)
 ************************************************************/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SinricPro.h>
#include <SinricProSwitch.h>
#include <DHT.h>

// ─── WiFi ──────────────────────────────────────────────────
#define WIFI_SSID    "YOUR_WIFI_SSID"
#define WIFI_PASS    "YOUR_WIFI_PASSWORD"

// ─── Telegram ──────────────────────────────────────────────
#define BOT_TOKEN    "YOUR_TELEGRAM_BOT_TOKEN"
#define CHAT_ID      "YOUR_TELEGRAM_CHAT_ID"

// ─── SinricPro ─────────────────────────────────────────────
#define APP_KEY      "YOUR_SINRIC_APP_KEY"
#define APP_SECRET   "YOUR_SINRIC_APP_SECRET"
#define LIGHT_ID     "YOUR_SINRIC_LIGHT_DEVICE_ID"
#define SECURITY_ID  "YOUR_SINRIC_SECURITY_DEVICE_ID"
#define FAN_ID       "YOUR_SINRIC_FAN_DEVICE_ID"

// ─── GPIO Pins ─────────────────────────────────────────────
#define LED_PIN      21
#define PIR_PIN      19
#define GAS_PIN      16
#define BUZZER_PIN   25
#define DHT_PIN      4
#define MOTOR_PIN    26   // L298N IN1
#define MOTOR_PIN2   27   // L298N IN2
#define LDR_PIN      34   // ADC only — no pinMode needed

// ─── Sensor Config ─────────────────────────────────────────
#define DHT_TYPE       DHT22
#define TEMP_THRESHOLD 32     // °C — fan auto ON above this
#define DARK_THRESHOLD 1500   // LDR 0–4095, below = dark
                              // Open Serial Monitor and watch
                              // [LDR] value to calibrate this

// ─── DHT Object ────────────────────────────────────────────
DHT dht(DHT_PIN, DHT_TYPE);

// ─── State Variables ───────────────────────────────────────
bool lightOn       = false;
bool securityMode  = false;
bool fanOn         = false;
bool fanAutoMode   = false;   // true = fan turned ON by temperature
bool lightAutoMode = false;   // true = light turned ON by LDR

// ─── Timers ────────────────────────────────────────────────
unsigned long lastMotionAlert = 0;
unsigned long lastGasAlert    = 0;
unsigned long lastTempAlert   = 0;
unsigned long lastTempRead    = 0;
unsigned long lastLDRRead     = 0;

const unsigned long COOLDOWN      = 10000UL; // 10s between alerts
const unsigned long TEMP_INTERVAL = 5000UL;  // read DHT22 every 5s
const unsigned long LDR_INTERVAL  = 3000UL;  // read LDR every 3s

// ═══════════════════════════════════════════════════════════
//  TELEGRAM
//  Sends a message to your phone instantly
// ═══════════════════════════════════════════════════════════
void sendTelegram(String message) {
  WiFiClientSecure client;
  client.setInsecure(); // skip SSL cert — fine for IoT

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("[Telegram] Connection failed");
    return;
  }

  // URL-encode special characters that break GET requests
  message.replace("%",  "%25"); // must be first
  message.replace(" ",  "%20");
  message.replace("!",  "%21");
  message.replace("#",  "%23");
  message.replace("&",  "%26");
  message.replace("+",  "%2B");
  message.replace(":",  "%3A");
  message.replace(";",  "%3B");
  message.replace("=",  "%3D");
  message.replace("?",  "%3F");
  message.replace("@",  "%40");

  String url = "/bot" + String(BOT_TOKEN) +
               "/sendMessage?chat_id=" + String(CHAT_ID) +
               "&text=" + message;

  client.print("GET " + url + " HTTP/1.1\r\n");
  client.print("Host: api.telegram.org\r\n");
  client.print("Connection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0 && millis() - timeout < 5000)
    delay(10);

  Serial.println("[Telegram] Sent OK");
  client.stop();
}

// ═══════════════════════════════════════════════════════════
//  LIGHT CONTROL
//  Used by both Google Assistant and LDR auto mode
// ═══════════════════════════════════════════════════════════
void setLight(bool state, bool fromAuto = false) {
  lightOn = state;
  digitalWrite(LED_PIN, lightOn ? HIGH : LOW);

  // Keep SinricPro in sync so Google Assistant shows real state
  SinricProSwitch &light = SinricPro[LIGHT_ID];
  light.sendPowerStateEvent(lightOn);

  Serial.printf("[Light] %s (%s)\n",
                lightOn ? "ON" : "OFF",
                fromAuto ? "Auto/LDR" : "Manual");
}

// ═══════════════════════════════════════════════════════════
//  FAN CONTROL
//  L298N needs IN1 + IN2 — only using IN1/IN2 for forward/stop
//  Fan ON  → IN1 HIGH, IN2 LOW
//  Fan OFF → IN1 LOW,  IN2 LOW
// ═══════════════════════════════════════════════════════════
void setFan(bool state, bool fromAuto = false) {
  fanOn = state;

  if (fanOn) {
    digitalWrite(MOTOR_PIN,  HIGH); // IN1 HIGH
    digitalWrite(MOTOR_PIN2, LOW);  // IN2 LOW → forward
  } else {
    digitalWrite(MOTOR_PIN,  LOW);  // IN1 LOW
    digitalWrite(MOTOR_PIN2, LOW);  // IN2 LOW → stop
  }

  // Keep SinricPro in sync
  SinricProSwitch &fan = SinricPro[FAN_ID];
  fan.sendPowerStateEvent(fanOn);

  Serial.printf("[Fan] %s (%s)\n",
                fanOn ? "ON" : "OFF",
                fromAuto ? "Auto/Temp" : "Manual");
}

// ═══════════════════════════════════════════════════════════
//  SINRIC CALLBACKS — triggered by Google Assistant
// ═══════════════════════════════════════════════════════════

// "Hey Google, turn on the light"
bool onLightPowerState(const String &deviceId, bool &state) {
  lightAutoMode = false; // manual overrides auto
  setLight(state, false);
  return true;
}

// "Hey Google, turn on security"
bool onSecurityPowerState(const String &deviceId, bool &state) {
  securityMode = state;
  Serial.printf("[Security] %s\n",
                securityMode ? "ARMED" : "DISARMED");

  sendTelegram(securityMode
               ? "Security System ARMED"
               : "Security System DISARMED");

  // When ARMED — turn off anything that was auto-controlled
  if (securityMode) {
    if (lightAutoMode && lightOn) {
      lightAutoMode = false;
      setLight(false, false);
      Serial.println("[Security] Light OFF — was auto ON");
    }
    if (fanAutoMode && fanOn) {
      fanAutoMode = false;
      setFan(false, false);
      Serial.println("[Security] Fan OFF — was auto ON");
    }
  }

  // When DISARMED — silence buzzer only if no gas present
  // MQ2 DOUT: LOW = gas, HIGH = clean air
  if (!securityMode && digitalRead(GAS_PIN) != LOW) {
    digitalWrite(BUZZER_PIN, LOW);
  }

  return true;
}

// "Hey Google, turn on fan"
bool onFanPowerState(const String &deviceId, bool &state) {
  fanAutoMode = false; // manual overrides auto
  setFan(state, false);
  return true;
}

// ═══════════════════════════════════════════════════════════
//  WIFI — connect with timeout, safe to call anytime
// ═══════════════════════════════════════════════════════════
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("[WiFi] Connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 15000UL) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\n[WiFi] Connected: %s\n",
                  WiFi.localIP().toString().c_str());
  else
    Serial.println("\n[WiFi] Failed — will retry");
}

// ═══════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  // GPIO setup
  pinMode(LED_PIN,    OUTPUT);
  pinMode(PIR_PIN,    INPUT);
  pinMode(GAS_PIN,    INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MOTOR_PIN,  OUTPUT);
  pinMode(MOTOR_PIN2, OUTPUT);
  // GPIO 34 (LDR) is ADC input-only — no pinMode needed

  // Safe startup — everything OFF
  digitalWrite(LED_PIN,    LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(MOTOR_PIN,  LOW);
  digitalWrite(MOTOR_PIN2, LOW);

  dht.begin();
  connectWiFi();

  // Register SinricPro devices
  SinricProSwitch &light    = SinricPro[LIGHT_ID];
  light.onPowerState(onLightPowerState);

  SinricProSwitch &security = SinricPro[SECURITY_ID];
  security.onPowerState(onSecurityPowerState);

  SinricProSwitch &fan      = SinricPro[FAN_ID];
  fan.onPowerState(onFanPowerState);

  // Restore last known states from SinricPro cloud
  SinricPro.restoreDeviceStates(true);
  SinricPro.begin(APP_KEY, APP_SECRET);
  Serial.println("[SinricPro] Connected");

  // Startup Telegram message
  sendTelegram("Smart Home System Online");

  // PIR needs 30s warmup, MQ2 needs 60s — use 60s for both
  Serial.println("[Sensors] Warming up — 60 seconds...");
  for (int i = 60; i > 0; i--) {
    Serial.printf("  %d s remaining\n", i);
    delay(1000);
  }
  Serial.println("[Sensors] Ready — System Active");
}

// ═══════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════
void loop() {

  // Auto-reconnect WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected — reconnecting...");
    connectWiFi();
  }

  SinricPro.handle();

  // ── Read sensors ─────────────────────────────────────
  bool motionDetected = digitalRead(PIR_PIN) == HIGH;
  // MQ2 active LOW: LOW = gas present, HIGH = clean air
  bool gasDetected    = digitalRead(GAS_PIN) == LOW;

  bool buzzerOn     = false;
  unsigned long now = millis();

  // ── Motion alarm (only when security is ARMED) ───────
  if (securityMode && motionDetected) {
    buzzerOn = true;
    if (now - lastMotionAlert > COOLDOWN) {
      sendTelegram("ALERT Motion Detected at Home");
      lastMotionAlert = now;
      Serial.println("[Alert] Motion — Telegram sent");
    }
  }

  // ── Gas alarm (always active — never blocked) ────────
  if (gasDetected) {
    buzzerOn = true;
    if (now - lastGasAlert > COOLDOWN) {
      sendTelegram("DANGER Gas Leakage Detected");
      lastGasAlert = now;
      Serial.println("[Alert] Gas — Telegram sent");
    }
  }

  // Update buzzer
  digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);

  // ══════════════════════════════════════════════════════
  //  AUTOMATION BLOCK
  //  Entirely skipped when security mode is ARMED
  //  Security mode = surveillance only, no automation
  // ══════════════════════════════════════════════════════
  if (!securityMode) {

    // ── LDR: Auto light control (every 3 seconds) ──────
    if (now - lastLDRRead > LDR_INTERVAL) {
      lastLDRRead = now;

      int  ldrValue = analogRead(LDR_PIN); // 0=dark, 4095=bright
      bool isDark   = (ldrValue < DARK_THRESHOLD);

      Serial.printf("[LDR] %d — %s\n",
                    ldrValue, isDark ? "DARK" : "BRIGHT");

      // Dark and light is off → auto ON
      if (isDark && !lightOn) {
        lightAutoMode = true;
        setLight(true, true);
      }

      // Bright and light was auto-turned ON → auto OFF
      // If user manually turned light ON, lightAutoMode = false
      // so this block never touches it
      if (!isDark && lightOn && lightAutoMode) {
        lightAutoMode = false;
        setLight(false, true);
      }
    }

    // ── DHT22: Auto fan control (every 5 seconds) ──────
    if (now - lastTempRead > TEMP_INTERVAL) {
      lastTempRead = now;

      float temp = dht.readTemperature(); // Celsius

      if (isnan(temp)) {
        Serial.println("[DHT] Read failed — check wiring on GPIO 4");
      } else {
        Serial.printf("[Temp] %.1f C\n", temp);

        // Hot and fan is off → auto ON
        if (temp > TEMP_THRESHOLD && !fanOn) {
          fanAutoMode = true;
          setFan(true, true);

          if (now - lastTempAlert > COOLDOWN) {
            String msg = "TEMP ALERT " + String(temp, 1) +
                         "C Fan turned ON automatically";
            sendTelegram(msg);
            lastTempAlert = now;
          }
        }

        // Cool and fan was auto-turned ON → auto OFF
        // If user manually turned fan ON, fanAutoMode = false
        // so this block never touches it
        if (temp <= TEMP_THRESHOLD && fanOn && fanAutoMode) {
          fanAutoMode = false;
          setFan(false, true);
          sendTelegram("Temp normal Fan turned OFF automatically");
        }
      }
    }

  } else {
    // Security ARMED — still read sensors but skip automation
    // Timers still update so automation resumes immediately on disarm
    if (now - lastLDRRead > LDR_INTERVAL) {
      lastLDRRead = now;
      int ldrValue = analogRead(LDR_PIN);
      Serial.printf("[LDR] %d — skipped (security armed)\n", ldrValue);
    }
    if (now - lastTempRead > TEMP_INTERVAL) {
      lastTempRead = now;
      float temp = dht.readTemperature();
      if (!isnan(temp))
        Serial.printf("[Temp] %.1f C — skipped (security armed)\n", temp);
    }
  }
}