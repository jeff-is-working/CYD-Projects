# CYD-Projects

ESP32-based surveillance detection and scanning tools built on the Cheap Yellow Display (CYD) and LILYGO T-Beam platforms.

## Projects

### `dual_ntp_display/`
NTP dual clock (UTC + local PDT) for the ESP32 CYD. Circle6Systems "Warm & Grounded" color palette. Based on Bruce Hall (W8BH) NTP clock, modified for CYD by WA2FZW & VK3PE.

### `flock_flash/`
Standalone passive BLE + WiFi surveillance device scanner for the CYD. Detects Flock Safety ALPR cameras, Hikvision, Dahua, Ring, DJI drones, and 20+ other surveillance vendors via OUI MAC prefix matching. Displays live count and detection log on screen, logs all MACs to SD card as CSV.

### `tbeam_scanner/`
GPS-tagged mobile surveillance scanner for the LILYGO T-Beam v1.2. Same OUI detection as flock_flash with GPS coordinate tagging. OLED status display, SPIFFS logging, JSON output over USB and UART for CYD receiver integration. Designed for vehicle-mounted wardriving.

### `packet_logger/`
Planning doc for a multi-platform passive WiFi/BLE packet capture system. Phased approach: ESP32 -> nRF52840 -> long-range adapters. Includes mesh scanner concept using LoRa/Meshtastic for remote data exfiltration from deployed sensor nodes.

## Hardware

| Device | Role | Key Features |
|--------|------|--------------|
| ESP32 CYD (2432S028R) | Display + SD logger | ILI9341 320x240, microSD, USB-C |
| LILYGO T-Beam v1.2 | Mobile GPS scanner | ESP32 + SX1262 LoRa + NEO-6M GPS + AXP2101 PMU + 18650 |
| Heltec V3 (x2) | Throwaway traffic monitors | ESP32-S3 + SX1262, small form factor |
| RAK (x2) | Low-power relay nodes | nRF52840 + SX1262 |

## Build Notes

All projects use `arduino-cli`. BLE-enabled sketches require the `no_ota` partition scheme for sufficient flash space:

```bash
# Compile
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=no_ota <sketch_dir>

# Upload (CYD)
arduino-cli upload --fqbn esp32:esp32:esp32:PartitionScheme=no_ota,UploadSpeed=460800 --port /dev/cu.usbserial-1140 <sketch_dir>

# Upload (T-Beam — hold IO31 + press RST for bootloader mode)
arduino-cli upload --fqbn esp32:esp32:esp32:PartitionScheme=no_ota,UploadSpeed=460800 --port /dev/cu.usbserial-57050063581 <sketch_dir>
```

Required libraries: `TFT_eSPI`, `ezTime`, `TinyGPSPlus`, `Adafruit SSD1306`, `Adafruit GFX Library`

## Related Projects & Prior Art

### Surveillance Detection
- [colonelpanichacks/flock-you](https://github.com/colonelpanichacks/flock-you) — BLE-only Flock camera + Raven gunshot detector scanner. Uses BLE manufacturer ID `0x09C8` (XUNTONG) for Flock fingerprinting and service UUID matching for ShotSpotter/Raven devices. Exports JSON/CSV/KML.
- [colonelpanichacks/oui-spy](https://github.com/colonelpanichacks/oui-spy) — Original OUI-based surveillance scanner. Our OUI database is derived from this project.
- [colonelpanichacks/oui-spy-unified-blue](https://github.com/colonelpanichacks/oui-spy-unified-blue) — Unified firmware combining detector, flock-you, foxhunter, and sky-spy into one build.
- [colonelpanichacks/Sky-Spy](https://github.com/colonelpanichacks/Sky-Spy) — Drone RemoteID detection and mapping.
- [jeff-is-working/oui-spy-too](https://github.com/jeff-is-working/oui-spy-too) — Our ESP32 + Raspberry Pi two-device architecture with HTTPS dashboard, SQLite, and GPS. The OUI database and serial protocol used here originate from this project.
- [Flock-You Documentation](https://virtuallyscott.github.io/flock-you/) — Detailed docs on BLE detection heuristics.
- [alpr.watch](https://alpr.watch/) — Tracks ALPR/surveillance tech mentions in local government meetings.

### Wardriving & Scanning
- [JosephHewitt/wardriver_rev3](https://github.com/JosephHewitt/wardriver_rev3/) — Dual-ESP32 WiFi/Bluetooth wardriver for Wigle.net with custom PCB, GPS, and SD logging.
- [cyberman54/ESP32-Paxcounter](https://github.com/cyberman54/ESP32-Paxcounter) — WiFi + BLE passenger flow counter on ESP32 LoRa boards (Heltec, T-Beam). Sends counts over LoRaWAN. Closest existing project to the mesh-scanner concept.

### Gap in the Space
No existing project combines **Meshtastic mesh networking + surveillance camera detection + GPS wardriving** into one platform. The pieces exist separately (flock-you for detection, Paxcounter for LoRa relay, wardriver for GPS logging) — this project aims to bridge all three.

### Future Integration Opportunities
- Add BLE manufacturer ID `0x09C8` fingerprinting from flock-you for improved Flock detection
- Add BLE service UUID matching for ShotSpotter/Raven gunshot detectors
- Integrate Paxcounter-style LoRa relay for mesh scanner nodes
- KML export for Google Earth visualization of detection routes

## Legal

Educational and security research tools. All scanning is **passive receive-only** — no transmission, no interference, no deauthentication, no decryption. Check local laws before use.

## License

GPL-3.0
