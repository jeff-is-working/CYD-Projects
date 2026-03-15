/**************************************************************************
  Title:    Flock Flash — Surveillance Device Scanner for ESP32 CYD
  Author:   Jeff Records / Circle6Systems
  Date:     14 Mar 2026
  Hardware: ESP32 CYD (Cheap Yellow Display) with ILI9341 320x240
  Software: Arduino IDE / arduino-cli
            TFT_eSPI, BLE, WiFi, SD
  License:  GPL-3.0
  Based on: OUI-spy-TOO by colonelpanichacks / Circle Six Consulting

  Description:
    Standalone passive BLE + WiFi scanner that detects Flock Safety
    ALPR cameras and other surveillance devices by MAC prefix (OUI).
    Displays a running count and log on the CYD screen.
    Logs all detected MACs to SD card as CSV.
    No Pi required — fully self-contained.

    Educational / security research tool. Passive receive-only.
**************************************************************************/

#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
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

// ---- SD Card (VSPI bus, separate from TFT's HSPI) ----
#define SD_CS               5
#define SD_MOSI            23
#define SD_MISO            19
#define SD_SCLK            18

// ---- Scan Timing ----
#define BLE_SCAN_TIME       1       // seconds per BLE scan (short to keep display responsive)
#define BLE_SCAN_INTERVAL   5000    // ms between BLE scans
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

// (no BLE queue needed — using post-scan iteration instead of callbacks)

// ============ GLOBALS ============
TFT_eSPI tft = TFT_eSPI();
BLEScan* pBLEScan = nullptr;

DetectedDevice devices[MAX_DEVICES];
int deviceCount = 0;
int surveillanceCount = 0;
int totalSurveillanceEver = 0;

unsigned long lastBleScan = 0;
unsigned long lastWifiScan = 0;
unsigned long lastScreenUpdate = 0;
unsigned long startTime = 0;
bool flashActive = false;
unsigned long flashStart = 0;

// SD card state
SPIClass sdSPI(VSPI);
bool sdReady = false;
const char* logFilename = "/flock_log.csv";
int sdLogCount = 0;

// Log display: rolling list of last N detections
#define LOG_LINES     6
#define LOG_Y_START   118
#define LOG_LINE_H    20
String logEntries[LOG_LINES];
int logCount = 0;

// ============ SD CARD ============

bool initSD() {
  sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("[SD] No card detected");
    return false;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("[SD] No card detected");
    return false;
  }

  const char* typeStr = "UNKNOWN";
  if (cardType == CARD_MMC) typeStr = "MMC";
  else if (cardType == CARD_SD) typeStr = "SD";
  else if (cardType == CARD_SDHC) typeStr = "SDHC";

  Serial.printf("[SD] Card: %s, Size: %lluMB\n", typeStr, SD.cardSize() / (1024 * 1024));

  // Write CSV header only if file doesn't exist yet
  bool isNew = !SD.exists(logFilename);
  File f = SD.open(logFilename, FILE_APPEND);
  if (!f) {
    Serial.println("[SD] Failed to open log file");
    return false;
  }
  if (isNew) {
    f.println("elapsed_sec,vendor,device_type,mac,rssi,ble,wifi,surveillance");
  }
  f.println("# --- session start ---");
  f.close();

  Serial.printf("[SD] Logging to %s\n", logFilename);
  return true;
}

void logToSD(const DetectedDevice& d) {
  if (!sdReady) return;

  File f = SD.open(logFilename, FILE_APPEND);
  if (!f) {
    Serial.println("[SD] Write failed");
    return;
  }

  unsigned long elapsed = (millis() - startTime) / 1000;
  bool isSurveillance = (d.vendor != NULL);
  f.printf("%lu,%s,%s,%s,%d,%d,%d,%d\n",
    elapsed,
    d.vendor ? d.vendor : "",
    d.deviceType ? d.deviceType : "",
    d.mac,
    d.rssi,
    d.ble ? 1 : 0,
    d.wifi ? 1 : 0,
    isSurveillance ? 1 : 0);
  f.close();
  sdLogCount++;
}

// ============ BLE MANUFACTURER ID TABLE ============

struct BLEManufEntry {
  uint16_t companyId;
  const char* vendor;
  const char* deviceType;
};

BLEManufEntry knownBLEManufacturers[] = {
  {0x09C8, "Flock Safety", "ALPR Camera"},     // XUNTONG — Flock hardware
  {0x004C, "Apple", ""},                        // Apple (not surveillance, for reference)
  {0x0000, NULL, NULL}                          // Terminator
};

// Check BLE manufacturer data for known surveillance company IDs
// Returns index into knownBLEManufacturers or -1
int matchBLEManufacturer(uint16_t companyId) {
  for (int i = 0; knownBLEManufacturers[i].vendor != NULL; i++) {
    if (knownBLEManufacturers[i].companyId == companyId &&
        knownBLEManufacturers[i].deviceType[0] != '\0') {  // skip non-surveillance
      return i;
    }
  }
  return -1;
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

// Returns true if this is a NEW surveillance device
bool addDevice(const char* mac, int8_t rssi, bool isBle, bool isWifi) {
  for (int i = 0; i < deviceCount; i++) {
    if (strncasecmp(devices[i].mac, mac, 17) == 0) {
      devices[i].lastSeen = millis();
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
  d.firstSeen = millis();
  d.lastSeen = millis();

  if (idx >= 0) {
    d.vendor = knownSurveillanceOUIs[idx].vendor;
    d.deviceType = knownSurveillanceOUIs[idx].deviceType;
    deviceCount++;
    surveillanceCount++;
    totalSurveillanceEver++;
    logToSD(d);
    return true;
  } else {
    d.vendor = NULL;
    d.deviceType = NULL;
    deviceCount++;
    logToSD(d);
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
  int y = 32;
  tft.fillRect(0, y, 319, 64, C6S_NAVY);

  // Label with subtle background bar
  tft.fillRect(4, y, 311, 16, C6S_NAVY_LIGHT);
  tft.setTextColor(C6S_ACCENT, C6S_NAVY_LIGHT);
  tft.drawCentreString("SURVEILLANCE DETECTED", 160, y + 1, 2);

  // Large number
  tft.setTextColor(C6S_WHITE, C6S_NAVY);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", totalSurveillanceEver);
  tft.drawCentreString(buf, 100, y + 20, 7);

  // Stats on right side
  tft.setTextColor(C6S_SLATE_LIGHT, C6S_NAVY);
  snprintf(buf, sizeof(buf), "%d", deviceCount);
  tft.drawString("All: ", 220, y + 22, 4);
  tft.drawString(buf, 268, y + 22, 4);

  // Uptime
  unsigned long elapsed = (millis() - startTime) / 1000;
  int mins = elapsed / 60;
  int secs = elapsed % 60;
  char timeBuf[12];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mins, secs);
  tft.drawString(timeBuf, 232, y + 48, 2);
}

void drawLogHeader() {
  tft.fillRoundRect(0, 98, 319, 18, 5, C6S_NAVY_LIGHT);
  tft.setTextColor(C6S_WHITE, C6S_NAVY_LIGHT);
  tft.drawString(" VENDOR          TYPE           RSSI", 4, 100, 2);
}

void addLogEntry(const char* vendor, const char* deviceType, int8_t rssi) {
  char entry[52];
  char vBuf[17], tBuf[15];
  strncpy(vBuf, vendor, 16); vBuf[16] = '\0';
  strncpy(tBuf, deviceType, 14); tBuf[14] = '\0';
  snprintf(entry, sizeof(entry), "%-16s %-14s %ddB", vBuf, tBuf, rssi);

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

  char status[50];
  if (sdReady) {
    snprintf(status, sizeof(status), " BLE+WiFi | %d MACs | SD:%d logged", deviceCount, sdLogCount);
  } else {
    snprintf(status, sizeof(status), " BLE+WiFi | %d MACs | NO SD CARD", deviceCount);
  }
  tft.drawString(status, 2, y + 1, 2);
}

void flashScreen() {
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

// ============ BLE SCANNING ============
// No callbacks — do a blocking scan then iterate results on main core.

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
    int8_t rssi = dev.getRSSI();

    bool isNew = addDevice(mac, rssi, true, false);

    // If OUI didn't match, check BLE manufacturer data
    if (isNew) {
      int idx = deviceCount - 1;
      if (!devices[idx].vendor && dev.haveManufacturerData()) {
        String manufData = dev.getManufacturerData();
        if (manufData.length() >= 2) {
          uint16_t companyId = (uint8_t)manufData[0] | ((uint8_t)manufData[1] << 8);
          int mIdx = matchBLEManufacturer(companyId);
          if (mIdx >= 0) {
            devices[idx].vendor = knownBLEManufacturers[mIdx].vendor;
            devices[idx].deviceType = knownBLEManufacturers[mIdx].deviceType;
            surveillanceCount++;
            totalSurveillanceEver++;
            logToSD(devices[idx]);  // re-log with vendor info
            Serial.printf("[BLE] MANUF ID 0x%04X: %s %s RSSI:%d\n",
              companyId, devices[idx].vendor, devices[idx].deviceType, rssi);
          }
        }
      }
      if (devices[idx].vendor) {
        flashScreen();
        addLogEntry(devices[idx].vendor, devices[idx].deviceType, rssi);
        drawStats();
      }
    }
  }
  pBLEScan->clearResults();
}

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
      Serial.printf("[WiFi] SURVEILLANCE: %s %s %s RSSI:%d\n",
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
  tft.drawCentreString("FLOCK FLASH", 160, 50, 4);
  tft.setTextColor(C6S_SLATE_LIGHT, C6S_NAVY);
  tft.drawCentreString("Surveillance Scanner", 160, 85, 2);

  // SD card init
  tft.drawCentreString("Checking SD card...", 160, 115, 2);
  sdReady = initSD();
  if (sdReady) {
    tft.setTextColor(C6S_ACCENT, C6S_NAVY);
    tft.drawCentreString("SD OK - logging to flock_log.csv", 160, 135, 2);
  } else {
    tft.setTextColor(C6S_ORANGE, C6S_NAVY);
    tft.drawCentreString("NO SD CARD - logging disabled", 160, 135, 2);
  }

  // BLE + WiFi init
  tft.setTextColor(C6S_SLATE_LIGHT, C6S_NAVY);
  tft.drawCentreString("Initializing BLE + WiFi...", 160, 165, 2);

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  tft.setTextColor(C6S_NAVY_MID, C6S_NAVY);
  tft.drawCentreString("Passive receive-only", 160, 200, 2);
  tft.drawCentreString("Educational / research use", 160, 218, 2);

  delay(2500);

  drawFullScreen();
  Serial.println("[FlockFlash] Ready. Scanning...");
}

// ============ LOOP ============

void loop() {
  unsigned long now = millis();

  // BLE scan (blocking, then iterate results — all on main core)
  if (now - lastBleScan >= BLE_SCAN_INTERVAL) {
    lastBleScan = now;
    doBLEScan();
  }

  // WiFi scan (skip if BLE scan is active to avoid coexistence issues)
  if (now - lastWifiScan >= WIFI_SCAN_INTERVAL && !pBLEScan->isScanning()) {
    lastWifiScan = now;
    doWifiScan();
  }

  // Update stats display every second
  if (now - lastScreenUpdate >= 1000) {
    lastScreenUpdate = now;
    drawStats();
    drawStatusBar();
  }

  clearFlash();

  delay(10);
}
