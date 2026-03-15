/**************************************************************************
  Title:    T-Beam Scanner — GPS-tagged Surveillance Device Scanner
  Author:   Jeff Records / Circle6Systems
  Date:     14 Mar 2026
  Hardware: LILYGO T-Beam v1.2 (ESP32 + SX1262 + NEO-6M GPS + AXP2101)
  Software: Arduino IDE / arduino-cli
            BLE, WiFi, TinyGPSPlus, SPIFFS
  License:  GPL-3.0
  Based on: OUI-spy-TOO by colonelpanichacks / Circle Six Consulting

  Description:
    Mobile GPS-tagged passive BLE + WiFi scanner for surveillance
    device detection. Logs all MACs with GPS coordinates to SPIFFS.
    Outputs JSON over Serial (USB) and Serial1 (UART TX) for
    CYD display receiver.

    Designed for vehicle-mounted wardriving.
    Educational / security research tool. Passive receive-only.

  T-Beam v1.2 Pin Reference:
    GPS TX=34, RX=12 (Serial1)
    LoRa: SCK=5, MISO=19, MOSI=27, CS=18, RST=23, IRQ=26
    LED: GPIO 4
    PMU: AXP2101 on I2C (SDA=21, SCL=22)
    UART out to CYD: TX=GPIO 13 (Serial2)
**************************************************************************/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <TinyGPSPlus.h>
#include "oui_database.h"

// ---- T-Beam v1.2 pins ----
#define GPS_RX_PIN        34    // GPS module TX -> ESP32 RX
#define GPS_TX_PIN        12    // ESP32 TX -> GPS module RX
#define LED_PIN            4    // onboard LED
#define UART_OUT_TX       13    // Serial2 TX to CYD receiver
#define UART_OUT_RX       -1    // not used (TX only)

// ---- Scan Timing ----
#define BLE_SCAN_TIME       1   // seconds per BLE scan
#define BLE_SCAN_INTERVAL   5000
#define WIFI_SCAN_INTERVAL  8000
#define GPS_UPDATE_INTERVAL 1000
#define LED_BLINK_MS        100

// ---- Device tracking ----
#define MAX_DEVICES   300

struct DetectedDevice {
  char mac[18];
  const char* vendor;
  const char* deviceType;
  int8_t rssi;
  bool ble;
  bool wifi;
  double lat;
  double lon;
  uint32_t timestamp;     // millis
};

// ============ GLOBALS ============
BLEScan* pBLEScan = nullptr;
TinyGPSPlus gps;

DetectedDevice devices[MAX_DEVICES];
int deviceCount = 0;
int surveillanceCount = 0;

unsigned long lastBleScan = 0;
unsigned long lastWifiScan = 0;
unsigned long lastGpsUpdate = 0;
unsigned long startTime = 0;

// Current GPS position
double currentLat = 0.0;
double currentLon = 0.0;
bool gpsValid = false;
int gpsSats = 0;

// SPIFFS log
const char* logFilename = "/scan_log.csv";
bool spiffsReady = false;
int loggedCount = 0;

// ============ GPS ============

void updateGPS() {
  while (Serial1.available()) {
    gps.encode(Serial1.read());
  }
  if (gps.location.isValid()) {
    currentLat = gps.location.lat();
    currentLon = gps.location.lng();
    gpsValid = true;
    gpsSats = gps.satellites.value();
  }
}

// ============ SPIFFS ============

bool initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] Mount failed");
    return false;
  }

  bool isNew = !SPIFFS.exists(logFilename);
  File f = SPIFFS.open(logFilename, FILE_APPEND);
  if (!f) {
    Serial.println("[SPIFFS] Failed to open log");
    return false;
  }
  if (isNew) {
    f.println("elapsed_sec,lat,lon,vendor,device_type,mac,rssi,ble,wifi,surveillance");
  }
  f.println("# --- session start ---");
  f.close();

  Serial.printf("[SPIFFS] Ready. Used: %u / %u bytes\n",
    SPIFFS.usedBytes(), SPIFFS.totalBytes());
  return true;
}

void logToSPIFFS(const DetectedDevice& d) {
  if (!spiffsReady) return;

  // Check space — leave 4KB buffer
  if (SPIFFS.usedBytes() > SPIFFS.totalBytes() - 4096) {
    Serial.println("[SPIFFS] Full — logging stopped");
    spiffsReady = false;
    return;
  }

  File f = SPIFFS.open(logFilename, FILE_APPEND);
  if (!f) return;

  unsigned long elapsed = (millis() - startTime) / 1000;
  bool isSurveillance = (d.vendor != NULL);
  f.printf("%lu,%.6f,%.6f,%s,%s,%s,%d,%d,%d,%d\n",
    elapsed,
    d.lat, d.lon,
    d.vendor ? d.vendor : "",
    d.deviceType ? d.deviceType : "",
    d.mac,
    d.rssi,
    d.ble ? 1 : 0,
    d.wifi ? 1 : 0,
    isSurveillance ? 1 : 0);
  f.close();
  loggedCount++;
}

// ============ JSON OUTPUT (to USB Serial + UART to CYD) ============

void sendJSON(const DetectedDevice& d) {
  bool isSurveillance = (d.vendor != NULL);
  char json[256];
  snprintf(json, sizeof(json),
    "{\"type\":\"device\",\"mac\":\"%s\",\"vendor\":\"%s\","
    "\"deviceType\":\"%s\",\"rssi\":%d,\"isSurveillance\":%s,"
    "\"ble\":%s,\"wifi\":%s,\"lat\":%.6f,\"lon\":%.6f,\"ts\":%lu}",
    d.mac,
    d.vendor ? d.vendor : "",
    d.deviceType ? d.deviceType : "",
    d.rssi,
    isSurveillance ? "true" : "false",
    d.ble ? "true" : "false",
    d.wifi ? "true" : "false",
    d.lat, d.lon,
    (millis() - startTime) / 1000);

  Serial.println(json);     // USB serial (debug / laptop capture)
  Serial2.println(json);    // UART to CYD display
}

void sendStatus() {
  char json[200];
  snprintf(json, sizeof(json),
    "{\"type\":\"status\",\"devices\":%d,\"surveillance\":%d,"
    "\"gpsValid\":%s,\"sats\":%d,\"lat\":%.6f,\"lon\":%.6f,"
    "\"logged\":%d,\"uptime\":%lu}",
    deviceCount, surveillanceCount,
    gpsValid ? "true" : "false", gpsSats,
    currentLat, currentLon,
    loggedCount,
    (millis() - startTime) / 1000);

  Serial.println(json);
  Serial2.println(json);
}

// ============ OUI MATCHING ============

int matchOUI(const char* mac) {
  char prefix[9];
  strncpy(prefix, mac, 8);
  prefix[8] = '\0';
  for (int i = 0; i < 8; i++) {
    if (prefix[i] >= 'a' && prefix[i] <= 'z')
      prefix[i] -= 32;
  }
  for (int i = 0; knownSurveillanceOUIs[i].oui != NULL; i++) {
    if (strncasecmp(prefix, knownSurveillanceOUIs[i].oui, 8) == 0)
      return i;
  }
  return -1;
}

// ============ DEVICE TRACKING ============

bool addDevice(const char* mac, int8_t rssi, bool isBle, bool isWifi) {
  for (int i = 0; i < deviceCount; i++) {
    if (strncasecmp(devices[i].mac, mac, 17) == 0) {
      devices[i].timestamp = millis();
      if (abs(devices[i].rssi - rssi) > 3)
        devices[i].rssi = rssi;
      if (isBle) devices[i].ble = true;
      if (isWifi) devices[i].wifi = true;
      return false;
    }
  }
  if (deviceCount >= MAX_DEVICES) return false;

  int idx = matchOUI(mac);
  DetectedDevice& d = devices[deviceCount];
  strncpy(d.mac, mac, 17);
  d.mac[17] = '\0';
  d.rssi = rssi;
  d.ble = isBle;
  d.wifi = isWifi;
  d.lat = currentLat;
  d.lon = currentLon;
  d.timestamp = millis();

  if (idx >= 0) {
    d.vendor = knownSurveillanceOUIs[idx].vendor;
    d.deviceType = knownSurveillanceOUIs[idx].deviceType;
    surveillanceCount++;
  } else {
    d.vendor = NULL;
    d.deviceType = NULL;
  }
  deviceCount++;

  logToSPIFFS(d);
  sendJSON(d);

  if (d.vendor) {
    // Blink LED for surveillance detection
    digitalWrite(LED_PIN, HIGH);
    delay(LED_BLINK_MS);
    digitalWrite(LED_PIN, LOW);
  }

  return (d.vendor != NULL);
}

// ============ BLE SCANNING ============

void doBLEScan() {
  BLEScanResults* results = pBLEScan->start(BLE_SCAN_TIME, false);
  if (!results) return;
  int count = results->getCount();

  for (int i = 0; i < count; i++) {
    BLEAdvertisedDevice dev = results->getDevice(i);
    String macStr = dev.getAddress().toString();
    char mac[18];
    strncpy(mac, macStr.c_str(), 17);
    mac[17] = '\0';
    for (int j = 0; j < 17 && mac[j]; j++) {
      if (mac[j] >= 'a' && mac[j] <= 'z') mac[j] -= 32;
    }
    addDevice(mac, dev.getRSSI(), true, false);
  }
  pBLEScan->clearResults();
}

// ============ WIFI SCANNING ============

void doWifiScan() {
  int n = WiFi.scanNetworks(false, true);
  for (int i = 0; i < n; i++) {
    uint8_t* bssid = WiFi.BSSID(i);
    if (!bssid) continue;
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
      bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    addDevice(mac, WiFi.RSSI(i), false, true);
  }
  WiFi.scanDelete();
}

// ============ SPIFFS DUMP (serial command) ============

void dumpLog() {
  if (!spiffsReady) {
    Serial.println("[SPIFFS] Not available");
    return;
  }
  File f = SPIFFS.open(logFilename, FILE_READ);
  if (!f) {
    Serial.println("[SPIFFS] Read failed");
    return;
  }
  Serial.println("--- BEGIN LOG DUMP ---");
  while (f.available()) {
    Serial.write(f.read());
  }
  Serial.println("--- END LOG DUMP ---");
  f.close();
}

void clearLog() {
  SPIFFS.remove(logFilename);
  loggedCount = 0;
  Serial.println("[SPIFFS] Log cleared");
  initSPIFFS();
}

// ============ SETUP ============

void setup() {
  Serial.begin(115200);
  Serial.println("\n[T-Beam Scanner] Starting...");
  startTime = millis();

  // LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // GPS on Serial1
  Serial1.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // UART output to CYD on Serial2 (TX only)
  Serial2.begin(115200, SERIAL_8N1, UART_OUT_RX, UART_OUT_TX);

  // SPIFFS
  spiffsReady = initSPIFFS();

  // BLE
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  // WiFi (STA mode, no connection)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Startup blink
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(150);
    digitalWrite(LED_PIN, LOW);
    delay(150);
  }

  Serial.println("[T-Beam Scanner] Ready. Scanning...");
  Serial.println("[T-Beam Scanner] Commands: 'dump' = download log, 'clear' = erase log");
}

// ============ LOOP ============

void loop() {
  unsigned long now = millis();

  // GPS update
  updateGPS();

  // BLE scan
  if (now - lastBleScan >= BLE_SCAN_INTERVAL) {
    lastBleScan = now;
    doBLEScan();
  }

  // WiFi scan (skip during BLE)
  if (now - lastWifiScan >= WIFI_SCAN_INTERVAL && !pBLEScan->isScanning()) {
    lastWifiScan = now;
    doWifiScan();
  }

  // Status update every 10 seconds
  if (now - lastGpsUpdate >= 10000) {
    lastGpsUpdate = now;
    sendStatus();
  }

  // Serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "dump") dumpLog();
    else if (cmd == "clear") clearLog();
  }

  delay(10);
}
