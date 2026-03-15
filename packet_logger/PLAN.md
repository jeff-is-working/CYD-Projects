# Packet Logger — Passive WiFi/BLE Capture Platform

## Goal
Long-term deployable passive packet loggers near known surveillance devices
to capture and analyze wireless traffic patterns.

## Platform Roadmap

### Phase 1: ESP32 (CYD + bare dev boards)
- WiFi promiscuous mode (raw 802.11 frame capture)
- BLE advertisement sniffing
- PCAP format to SD card (Wireshark-compatible)
- Cheap (~$5-15 per node), limited range
- Good for proof of concept and close-proximity deployment

### Phase 2: nRF modules
- Raw BLE packet capture with better sensitivity
- Lower power consumption for extended deployment
- nRF52840 supports BLE 5.0 long range (coded PHY)

### Phase 3: Long-range BLE/WiFi adapters
- Extended range SIGINT capability
- Directional antenna options
- Compare cost/range/capability vs ESP32 and nRF

## Capture Targets
- WiFi beacon frames (SSIDs, BSSIDs, channel, capabilities)
- WiFi probe requests/responses
- WiFi data frame headers (src/dst MAC, no payload decryption)
- BLE advertisements (raw payload, manufacturer data, service UUIDs)
- BLE scan responses

## Deployment Considerations
- Battery life: deep sleep between capture windows
- SD card rotation: auto-rotate logs by size
- Headless operation: no display needed for drop-box nodes
- Cost per node: compare platforms for fleet deployment
- Weatherproofing for outdoor placement

## Output Format
- PCAP files for Wireshark analysis
- CSV summary logs (MAC, vendor, timestamp, RSSI, GPS if available)
- Compatible with flock_flash OUI database for vendor identification

## Legal
Educational and security research tool. Passive receive-only.
No deauthentication, no injection, no decryption.
