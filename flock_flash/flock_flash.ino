/**************************************************************************
  Title:    Flock Flash — Surveillance Device Scanner for ESP32 CYD
  Author:   Jeff Records / Circle6Systems
  Date:     14 Mar 2026
  Hardware: ESP32 CYD (Cheap Yellow Display) with ILI9341 320x240
  Software: Arduino IDE / arduino-cli
            TFT_eSPI, BLE, WiFi
  License:  GPL-3.0
  Based on: OUI-spy-TOO by colonelpanichacks / Circle Six Consulting

  Description:
    Standalone passive BLE + WiFi scanner that detects Flock Safety
    ALPR cameras and other surveillance devices by MAC prefix (OUI).
    Displays a running count and log on the CYD screen.
    No Pi required — fully self-contained.

    Educational / security research tool. Passive receive-only.
**************************************************************************/

#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <WiFi.h>
#include "oui_database.h"

// ---- C6S "Warm & Grounded" palette (RGB565) ----
#define C6S_NAVY          0x2945    // #2d2a2e - primary background
#define C6S_NAVY_LIGHT    0x3A08    // #3a3640 - card/label background
#define C6S_ACCENT        0xA60B    // #a8c196 - sage green accent
#define C6S_WHITE         0xF79D    // #f5f0e8 - warm white
#define C6S_SLATE_LIGHT   0xD657    // #d4cab9 - body text
#define C6S_NAVY_MID      0x4A49    // #504a52 - borders
#define C6S_RED           0xF800    // alert red
#define C6S_ORANGE        0xFD20    // warning orange

// ---- Display ----
#define SCREEN_ORIENTATION  1       // landscape, cable right

// ---- Scan Timing ----
#define BLE_SCAN_TIME       5       // seconds per BLE scan
#define BLE_SCAN_INTERVAL   8000    // ms between BLE scans
#define WIFI_SCAN_INTERVAL  10000   // ms between WiFi scans

// ---- RSSI thresholds ----
#define RSSI_CLOSE    -50
#define RSSI_MEDIUM   -70
#define RSSI_FAR      -85

// ---- Device tracking ----
#define MAX_DEVICES   200

struct DetectedDevice {
  char mac[18];
  const char* vendor;
  const char* deviceType;
  int8_t rssi;
  bool ble;
  bool wifi;
  uint32_t firstSeen;    // millis
  uint32_t lastSeen;
};

// ============ GLOBALS ============
TFT_eSPI tft = TFT_eSPI();
BLEScan* pBLEScan = nullptr;

DetectedDevice devices[MAX_DEVICES];
int deviceCount = 0;
int surveillanceCount = 0;
int totalSurveillanceEver = 0;    // total unique surveillance devices seen

unsigned long lastBleScan = 0;
unsigned long lastWifiScan = 0;
unsigned long lastScreenUpdate = 0;
unsigned long startTime = 0;
bool flashActive = false;
unsigned long flashStart = 0;

// Log display: rolling list of last N detections
#define LOG_LINES     7
#define LOG_Y_START   95
#define LOG_LINE_H    20
String logEntries[LOG_LINES];
int logCount = 0;

// ============ OUI MATCHING ============

int matchOUI(const char* mac) {
  char prefix[9];
  strncpy(prefix, mac, 8);
  prefix[8] = '\0';
  // uppercase
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

// Returns true if this is a NEW surveillance device
bool addDevice(const char* mac, int8_t rssi, bool isBle, bool isWifi) {
  // Check if already seen
  for (int i = 0; i < deviceCount; i++) {
    if (strncasecmp(devices[i].mac, mac, 17) == 0) {
      devices[i].lastSeen = millis();
      if (abs(devices[i].rssi - rssi) > 3)
        devices[i].rssi = rssi;
      if (isBle) devices[i].ble = true;
      if (isWifi) devices[i].wifi = true;
      return false;  // already known
    }
  }
  if (deviceCount >= MAX_DEVICES) return false;

  // New device
  int idx = matchOUI(mac);
  DetectedDevice& d = devices[deviceCount];
  strncpy(d.mac, mac, 17);
  d.mac[17] = '\0';
  d.rssi = rssi;
  d.ble = isBle;
  d.wifi = isWifi;
  d.firstSeen = millis();
  d.lastSeen = millis();

  if (idx >= 0) {
    d.vendor = knownSurveillanceOUIs[idx].vendor;
    d.deviceType = knownSurveillanceOUIs[idx].deviceType;
    deviceCount++;
    surveillanceCount++;
    totalSurveillanceEver++;
    return true;  // new surveillance device!
  } else {
    d.vendor = NULL;
    d.deviceType = NULL;
    deviceCount++;
    return false;
  }
}

// ============ DISPLAY ============

void drawHeader() {
  tft.fillRoundRect(0, 0, 319, 30, 10, C6S_NAVY_LIGHT);
  tft.setTextColor(C6S_WHITE, C6S_NAVY_LIGHT);
  tft.drawCentreString("FLOCK FLASH", 160, 4, 4);
}

void drawStats() {
  int y = 36;
  // Big counter
  tft.fillRect(0, y, 319, 55, C6S_NAVY);
  tft.setTextColor(C6S_ACCENT, C6S_NAVY);
  tft.drawCentreString("SURVEILLANCE DETECTED", 160, y, 2);

  // Large number
  tft.setTextColor(C6S_WHITE, C6S_NAVY);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", totalSurveillanceEver);
  tft.drawCentreString(buf, 110, y + 16, 7);

  // Stats on right side
  tft.setTextColor(C6S_SLATE_LIGHT, C6S_NAVY);
  snprintf(buf, sizeof(buf), "%d", deviceCount);
  tft.drawString("All: ", 220, y + 16, 4);
  tft.drawString(buf, 268, y + 16, 4);

  // Uptime
  unsigned long elapsed = (millis() - startTime) / 1000;
  int mins = elapsed / 60;
  int secs = elapsed % 60;
  char timeBuf[12];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mins, secs);
  tft.drawString(timeBuf, 232, y + 42, 2);
}

void drawLogHeader() {
  tft.fillRoundRect(0, 83, 319, 18, 5, C6S_NAVY_LIGHT);
  tft.setTextColor(C6S_WHITE, C6S_NAVY_LIGHT);
  tft.drawString(" VENDOR          TYPE           RSSI", 4, 85, 2);
}

void addLogEntry(const char* vendor, const char* deviceType, int8_t rssi) {
  char entry[52];
  // Truncate vendor and type to fit
  char vBuf[17], tBuf[15];
  strncpy(vBuf, vendor, 16); vBuf[16] = '\0';
  strncpy(tBuf, deviceType, 14); tBuf[14] = '\0';
  snprintf(entry, sizeof(entry), "%-16s %-14s %ddB", vBuf, tBuf, rssi);

  // Scroll log up
  if (logCount >= LOG_LINES) {
    for (int i = 0; i < LOG_LINES - 1; i++)
      logEntries[i] = logEntries[i + 1];
    logEntries[LOG_LINES - 1] = String(entry);
  } else {
    logEntries[logCount] = String(entry);
    logCount++;
  }
  drawLog();
}

void drawLog() {
  for (int i = 0; i < LOG_LINES; i++) {
    int y = LOG_Y_START + (i * LOG_LINE_H) + 8;
    tft.fillRect(0, y, 319, LOG_LINE_H, C6S_NAVY);
    if (i < logCount) {
      // Color by recency: newest = accent, older = slate
      uint16_t color = (i == logCount - 1) ? C6S_ACCENT : C6S_SLATE_LIGHT;
      tft.setTextColor(color, C6S_NAVY);
      tft.drawString(logEntries[i], 4, y + 2, 2);
    }
  }
}

void drawStatusBar() {
  int y = 225;
  tft.fillRect(0, y, 319, 15, C6S_NAVY_LIGHT);
  tft.setTextColor(C6S_SLATE_LIGHT, C6S_NAVY_LIGHT);

  // Scanning indicator
  char status[40];
  snprintf(status, sizeof(status), " BLE+WiFi scanning   |   %d total MACs", deviceCount);
  tft.drawString(status, 2, y + 1, 2);
}

void flashScreen() {
  // Brief red border flash to alert on new surveillance device
  flashActive = true;
  flashStart = millis();
  for (int i = 0; i < 4; i++) {
    tft.drawRect(i, i, 319 - 2*i, 239 - 2*i, C6S_RED);
  }
}

void clearFlash() {
  if (flashActive && (millis() - flashStart > 500)) {
    flashActive = false;
    for (int i = 0; i < 4; i++) {
      tft.drawRect(i, i, 319 - 2*i, 239 - 2*i, C6S_NAVY);
    }
    // Redraw borders that overlap
    tft.drawRoundRect(0, 0, 319, 239, 10, C6S_NAVY_MID);
  }
}

void drawFullScreen() {
  tft.fillScreen(C6S_NAVY);
  tft.drawRoundRect(0, 0, 319, 239, 10, C6S_NAVY_MID);
  drawHeader();
  drawStats();
  drawLogHeader();
  drawLog();
  drawStatusBar();
}

// ============ BLE SCAN CALLBACK ============

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    const char* mac = advertisedDevice.getAddress().toString().c_str();
    int8_t rssi = advertisedDevice.getRSSI();

    // Format MAC with colons uppercase
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%s", mac);
    for (int i = 0; i < 17 && macStr[i]; i++) {
      if (macStr[i] >= 'a' && macStr[i] <= 'z')
        macStr[i] -= 32;
      if (macStr[i] == ':' || (i % 3 == 2))
        ; // keep colons
    }

    bool isNew = addDevice(macStr, rssi, true, false);
    if (isNew) {
      int idx = deviceCount - 1;
      flashScreen();
      addLogEntry(devices[idx].vendor, devices[idx].deviceType, rssi);
      drawStats();

      Serial.printf("[BLE] NEW SURVEILLANCE: %s %s %s RSSI:%d\n",
        macStr, devices[idx].vendor, devices[idx].deviceType, rssi);
    }
  }
};

// ============ WIFI SCANNING ============

void doWifiScan() {
  int n = WiFi.scanNetworks(false, true);
  for (int i = 0; i < n; i++) {
    uint8_t* bssid = WiFi.BSSID(i);
    if (!bssid) continue;
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
      bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    int8_t rssi = WiFi.RSSI(i);

    bool isNew = addDevice(macStr, rssi, false, true);
    if (isNew) {
      int idx = deviceCount - 1;
      flashScreen();
      addLogEntry(devices[idx].vendor, devices[idx].deviceType, rssi);
      drawStats();

      Serial.printf("[WiFi] NEW SURVEILLANCE: %s %s %s RSSI:%d\n",
        macStr, devices[idx].vendor, devices[idx].deviceType, rssi);
    }
  }
  WiFi.scanDelete();
}

// ============ SETUP ============

void setup() {
  Serial.begin(115200);
  Serial.println("\n[FlockFlash] Starting...");
  startTime = millis();

  // Display init
  tft.init();
  tft.invertDisplay(false);
  tft.setRotation(SCREEN_ORIENTATION);

  // Startup splash
  tft.fillScreen(C6S_NAVY);
  tft.setTextColor(C6S_ACCENT, C6S_NAVY);
  tft.drawCentreString("FLOCK FLASH", 160, 60, 4);
  tft.setTextColor(C6S_SLATE_LIGHT, C6S_NAVY);
  tft.drawCentreString("Surveillance Scanner", 160, 100, 2);
  tft.drawCentreString("Initializing BLE + WiFi...", 160, 130, 2);
  tft.setTextColor(C6S_NAVY_MID, C6S_NAVY);
  tft.drawCentreString("Passive receive-only", 160, 180, 2);
  tft.drawCentreString("Educational / research use", 160, 200, 2);

  // BLE init
  BLEDevice::init(""); // no name, passive
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), false);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  // WiFi init (station mode, no connection — just scanning)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  delay(2000);

  // Main screen
  drawFullScreen();
  Serial.println("[FlockFlash] Ready. Scanning...");
}

// ============ LOOP ============

void loop() {
  unsigned long now = millis();

  // BLE scan
  if (now - lastBleScan >= BLE_SCAN_INTERVAL) {
    lastBleScan = now;
    pBLEScan->start(BLE_SCAN_TIME, false);
    pBLEScan->clearResults();
  }

  // WiFi scan
  if (now - lastWifiScan >= WIFI_SCAN_INTERVAL) {
    lastWifiScan = now;
    doWifiScan();
  }

  // Update stats display every second
  if (now - lastScreenUpdate >= 1000) {
    lastScreenUpdate = now;
    drawStats();
    drawStatusBar();
  }

  // Clear flash effect
  clearFlash();

  delay(10);
}
