/*
  Water monitor: ESP32-CAM (AI Thinker) + ADS1115 + analog turbidity sensor + DS18B20
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_ADS1X15.h>
#include "esp_camera.h"
#include "img_converters.h"

// ---------- USER SETTINGS ----------
const char* WIFI_SSID     = "Ash";
const char* WIFI_PASSWORD = "ash10022008";

// Backend URL where the node posts telemetry
const char* BACKEND_URL = "http://localhost:8000";

#define I2C_SDA        14
#define I2C_SCL        15
#define ADS_ADDR       0x48
#define ONE_WIRE_BUS   13

#define DIVIDER_RATIO_DEFAULT 2.0
#if defined(ESP32) && defined(PLATFORMIO)
const float DIVIDER_RATIO = DIVIDER_RATIO_DEFAULT;
#else
// Production: load from nvs or config; fallback to 2.0 for prototype
const float DIVIDER_RATIO = DIVIDER_RATIO_DEFAULT;
#endif
const int   NUM_SAMPLES   = 10;    // ADC readings averaged per update
const unsigned long UPDATE_MS = 2000;
// -----------------------------------

// AI-Thinker camera pins (internal to the board, do not reuse)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
Adafruit_ADS1115 ads;
WebServer server(80);

// Cached readings
bool   adsOk = false;
bool   tempOk = false;
float  currentTemp = 0.0;
float  currentTurbVolts = 0.0;   // voltage at the sensor output (after undoing divider)
float  currentNTU = 0.0;
String waterColorDesc = "Detecting...";
int    currentHue = 0;

unsigned long lastUpdate = 0;
unsigned long lastWifiTry = 0;

// ---------- WEB PAGE ----------
const char MAIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Hydromind - Testing</title>
  <style>
    body { background:#000; color:#fff; font-family:monospace; text-align:center; margin-top:20px; }
    h1 { border-bottom:2px solid #333; display:inline-block; padding-bottom:5px; }
    .card { background:#111; border:1px solid #333; padding:15px; margin:15px auto; width:320px; max-width:90%; border-radius:8px; }
    .label { font-size:13px; color:#888; }
    .value { font-size:20px; font-weight:bold; margin-bottom:10px; color:#0ff; }
    img { width:100%; border-radius:4px; border:1px solid #444; margin-top:8px; }
  </style>
</head>
<body>
  <h1>TESTING</h1>

  <div class="card">
    <div class="label">Captured Frame (Proof):</div>
    <img id="cam" src="/capture" alt="Captured frame">
  </div>

  <div class="card">
    <div class="label">Water Color (RGB to Hue)</div>
    <div class="value" id="colorVal">...</div>

    <div class="label">Turbidity</div>
    <div class="value" id="turbVal">...</div>

    <div class="label">Temperature</div>
    <div class="value" id="tempVal">...</div>
  </div>

  <script>
    function refresh() {
      fetch('/data').then(r => r.json()).then(d => {
        document.getElementById('colorVal').innerText = d.colorDesc + " (" + d.hue + "\u00B0)";
        document.getElementById('turbVal').innerText =
          d.ntu === null ? "N/A (ADS1115 not found)" : d.ntu + " NTU (" + d.turbVolts.toFixed(2) + " V)";
        document.getElementById('tempVal').innerText =
          d.temperature === null ? "N/A (check DS18B20)" : d.temperature.toFixed(1) + " \u00B0C";
      }).catch(() => {});
      document.getElementById('cam').src = '/capture?t=' + Date.now();
    }
    refresh();
    setInterval(refresh, 3000);
  </script>
</body>
</html>
)rawliteral";

// ---------- CAMERA ----------
bool initCamera() {
  camera_config_t config;
  memset(&config, 0, sizeof(config));   // avoid garbage in fields added by newer camera drivers

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;   // raw pixels for on-chip colour analysis
  config.frame_size   = psramFound() ? FRAMESIZE_QVGA : FRAMESIZE_QQVGA;
  config.jpeg_quality = 12;
  config.fb_count     = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

// Discard one possibly-stale frame, then return a fresh one
camera_fb_t* grabFreshFrame() {
  camera_fb_t* old = esp_camera_fb_get();
  if (old) esp_camera_fb_return(old);
  return esp_camera_fb_get();
}

// ---------- COLOUR ANALYSIS ----------
void analyzeWaterColor() {
  camera_fb_t* fb = grabFreshFrame();
  if (!fb) {
    waterColorDesc = "Capture failed";
    currentHue = 0;
    return;
  }

  // RGB565 from the camera driver is big-endian: first byte = RRRRRGGG, second = GGGBBBBB
  const uint8_t* buf = fb->buf;
  size_t pixelCount = (size_t)fb->width * fb->height;
  uint32_t sumR = 0, sumG = 0, sumB = 0, n = 0;

  for (size_t i = 0; i < pixelCount; i += 10) {   // sample every 10th pixel
    uint8_t hb = buf[i * 2];
    uint8_t lb = buf[i * 2 + 1];
    sumR += (hb & 0xF8);
    sumG += ((hb & 0x07) << 5) | ((lb & 0xE0) >> 3);
    sumB += ((lb & 0x1F) << 3);
    n++;
  }
  esp_camera_fb_return(fb);
  if (n == 0) return;

  float rf = (sumR / (float)n) / 255.0;
  float gf = (sumG / (float)n) / 255.0;
  float bf = (sumB / (float)n) / 255.0;

  float cmax = max(rf, max(gf, bf));
  float cmin = min(rf, min(gf, bf));
  float delta = cmax - cmin;
  float hue = 0;
  if (delta > 0) {
    if (cmax == rf)      hue = 60.0 * fmodf(((gf - bf) / delta), 6.0);
    else if (cmax == gf) hue = 60.0 * (((bf - rf) / delta) + 2.0);
    else                 hue = 60.0 * (((rf - gf) / delta) + 4.0);
  }
  if (hue < 0) hue += 360.0;
  currentHue = (int)hue;

  float sat = (cmax == 0) ? 0 : delta / cmax;
  float val = cmax;

  if (val < 0.15)                       waterColorDesc = "Dark / low light";
  else if (sat < 0.12)                  waterColorDesc = "Clear / grey-white";
  else if (hue < 15 || hue >= 330)      waterColorDesc = "Reddish / rusty";
  else if (hue < 45)                    waterColorDesc = "Yellow-brown / muddy";
  else if (hue < 85)                    waterColorDesc = "Yellow-green / algae";
  else if (hue < 170)                   waterColorDesc = "Green";
  else if (hue < 260)                   waterColorDesc = "Blue";
  else                                  waterColorDesc = "Purple / other";
}

// ---------- TURBIDITY ----------
float readTurbidityVolts() {
  long sum = 0;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    sum += ads.readADC_SingleEnded(0);
  }
  int16_t avgRaw = (int16_t)(sum / NUM_SAMPLES);
  return ads.computeVolts(avgRaw) * DIVIDER_RATIO;
}

// Approximate NTU for the common 0-4.5V turbidity module (uncalibrated)
float voltageToNTU(float v) {
  if (v < 2.5) return 3000.0;
  float ntu = -1120.4 * v * v + 5742.3 * v - 4352.9;
  if (ntu < 0) ntu = 0;
  if (ntu > 3000) ntu = 3000;
  return ntu;
}

// ---------- PERIODIC UPDATE ----------
void updateSensors() {
  // Temperature: read the result requested last cycle, then start the next conversion
  float t = sensors.getTempCByIndex(0);
  tempOk = (t > -50.0 && t < 125.0);
  if (tempOk) currentTemp = t;
  sensors.requestTemperatures();

  if (adsOk) {
    currentTurbVolts = readTurbidityVolts();
    currentNTU = voltageToNTU(currentTurbVolts);
  }

  analyzeWaterColor();

  Serial.printf("Temp: %s | Turbidity: %s | Color: %s (%d deg)\n",
                tempOk ? String(currentTemp, 1).c_str() : "N/A",
                adsOk ? (String(currentTurbVolts, 3) + " V / " + String(currentNTU, 0) + " NTU").c_str() : "N/A",
                waterColorDesc.c_str(), currentHue);
}

// ---------- WEB HANDLERS ----------
void handleRoot() {
  server.send_P(200, "text/html", MAIN_PAGE);
}

void handleDataJson() {
  String json = "{";
  json += "\"colorDesc\":\"" + waterColorDesc + "\",";
  json += "\"hue\":" + String(currentHue) + ",";
  if (adsOk) {
    json += "\"turbVolts\":" + String(currentTurbVolts, 3) + ",";
    json += "\"ntu\":" + String(currentNTU, 0) + ",";
  } else {
    json += "\"turbVolts\":null,\"ntu\":null,";
  }
  if (tempOk) {
    json += "\"temperature\":" + String(currentTemp, 1);
  } else {
    json += "\"temperature\":null";
  }
  json += "}";
  server.send(200, "application/json", json);
}

void handleCapture() {
  camera_fb_t* fb = grabFreshFrame();
  if (!fb) {
    server.send(503, "text/plain", "Camera capture failed");
    return;
  }

  // Frame is raw RGB565, so convert to JPEG for the browser
  uint8_t* jpg = nullptr;
  size_t jpgLen = 0;
  bool ok = frame2jpg(fb, 80, &jpg, &jpgLen);
  esp_camera_fb_return(fb);

  if (!ok || !jpg) {
    server.send(500, "text/plain", "JPEG conversion failed");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "image/jpeg", (const char*)jpg, jpgLen);
  free(jpg);
}

void handleFavicon() {
  server.send(204, "text/plain", "");
}

// ---------- WIFI ----------
bool connectWiFi() {
  Serial.printf("Connecting to '%s' ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);   // keep the web server responsive for the laptop
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected! Open: http://");
    Serial.println(WiFi.localIP());
    return true;
  }
  Serial.println("WiFi failed (hotspot must be 2.4 GHz). Will keep retrying.");
  return false;
}

// ---------- SETUP / LOOP ----------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Water monitor starting ===");

  // I2C + ADS1115
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  adsOk = ads.begin(ADS_ADDR, &Wire);
  if (adsOk) {
    ads.setGain(GAIN_ONE);   // +/-4.096V range
    Serial.println("ADS1115 OK");
  } else {
    Serial.println("ADS1115 NOT found - check SDA(14)/SCL(15), power and ADDR->GND");
  }

  // DS18B20 (non-blocking conversions)
  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  Serial.printf("DS18B20 devices found: %d\n", sensors.getDeviceCount());

  // Camera
  if (!initCamera()) {
    Serial.println("Camera init failed - restarting in 3s");
    delay(3000);
    ESP.restart();
  }

  connectWiFi();
  
  if (MDNS.begin("hydromind")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS ready: http://hydromind.local");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleDataJson);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/favicon.ico", HTTP_GET, handleFavicon);
  server.begin();
  Serial.println("Web server started on port 80");
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - lastUpdate >= UPDATE_MS) {
    lastUpdate = now;
    updateSensors();
  }

  if (WiFi.status() != WL_CONNECTED && now - lastWifiTry > 10000) {
    lastWifiTry = now;
    Serial.println("WiFi lost, reconnecting...");
    WiFi.reconnect();
  }
}
