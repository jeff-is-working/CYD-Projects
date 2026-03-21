#!/usr/bin/env python3
"""
tbeam_dump.py — Download surveillance logs from T-Beam Scanner via USB serial.

Connects to the T-Beam, sends 'dump' command, parses the SPIFFS CSV log,
and saves:
  1. Raw CSV archive (dumps/YYYY-MM-DD_HHMMSS_raw.csv)
  2. Surveillance-only JSON normalized for OSM/DeFlock upload
     (dumps/YYYY-MM-DD_HHMMSS_surveillance.json)

Usage:
  python3 tbeam_dump.py                          # auto-detect USB port
  python3 tbeam_dump.py --port /dev/cu.usbserial-XXX
  python3 tbeam_dump.py --clear                  # clear SPIFFS after download
  python3 tbeam_dump.py --all                    # include non-surveillance MACs
"""

import argparse
import csv
import io
import json
import os
import sys
import time
from datetime import datetime, timezone

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial is required.  Install with:  pip install pyserial")
    sys.exit(1)

BAUD = 115200
TIMEOUT_SEC = 30
DUMPS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dumps")

# Maps device_type from firmware → OSM surveillance:type tag
SURVEILLANCE_TYPE_MAP = {
    "ALPR Camera":      "ALPR",
    "IP Camera":        "camera",
    "Cloud Camera":     "camera",
    "Doorbell/Camera":  "camera",
    "Security Camera":  "camera",
    "Network Camera":   "camera",
    "Drone":            "camera",
    "Traffic Sensor":   "sensor",
}

# Maps device_type → OSM camera:type tag
CAMERA_TYPE_MAP = {
    "ALPR Camera":      "fixed",
    "IP Camera":        "fixed",
    "Cloud Camera":     "fixed",
    "Doorbell/Camera":  "fixed",
    "Security Camera":  "fixed",
    "Network Camera":   "fixed",
    "Drone":            "panning",
}


def find_tbeam_port():
    """Auto-detect a USB serial port likely connected to a T-Beam."""
    ports = serial.tools.list_ports.comports()
    candidates = []
    for p in ports:
        desc = (p.description or "").lower()
        hwid = (p.hwid or "").lower()
        # typical CP2104/CH9102 USB-serial chips on T-Beam
        if any(k in desc + hwid for k in ["cp210", "ch910", "usbserial", "usb-serial", "usb serial"]):
            candidates.append(p.device)
        # macOS cu.usbserial-*
        if "usbserial" in p.device.lower():
            candidates.append(p.device)
    # deduplicate, prefer /dev/cu.* on macOS
    seen = set()
    result = []
    for c in candidates:
        if c not in seen:
            seen.add(c)
            result.append(c)
    return result


def connect(port):
    """Open serial connection, wait for it to settle."""
    ser = serial.Serial(port, BAUD, timeout=2)
    time.sleep(2)
    ser.reset_input_buffer()
    return ser


def send_dump(ser):
    """Send 'dump' command and capture everything between BEGIN/END markers."""
    ser.write(b"dump\n")
    ser.flush()

    output = b""
    start = time.time()
    while time.time() - start < TIMEOUT_SEC:
        chunk = ser.read(4096)
        if chunk:
            output += chunk
            if b"END LOG DUMP" in output:
                break
        elif output:
            # no more data flowing
            time.sleep(0.5)
            remaining = ser.read(4096)
            if remaining:
                output += remaining
            break

    text = output.decode("utf-8", errors="replace")

    # extract between markers
    begin = text.find("--- BEGIN LOG DUMP ---")
    end = text.find("--- END LOG DUMP ---")
    if begin == -1:
        print("ERROR: Did not receive BEGIN LOG DUMP marker.")
        print("Raw output:", repr(text[:500]))
        return None
    if end == -1:
        print("WARNING: No END marker — log may be truncated.")
        end = len(text)

    body = text[begin + len("--- BEGIN LOG DUMP ---"):end].strip()
    return body


def send_clear(ser):
    """Send 'clear' command to erase SPIFFS log."""
    ser.write(b"clear\n")
    ser.flush()
    time.sleep(1)
    resp = ser.read(1024).decode("utf-8", errors="replace")
    if "cleared" in resp.lower():
        print("SPIFFS log cleared on device.")
    else:
        print("Clear response:", resp.strip())


def parse_csv_log(raw_csv):
    """Parse the T-Beam CSV dump into a list of dicts."""
    records = []
    session = 0
    reader = csv.reader(io.StringIO(raw_csv))

    for row in reader:
        line = ",".join(row).strip()
        if not line:
            continue
        if line.startswith("# --- session start"):
            session += 1
            continue
        if line.startswith("#") or line.startswith("elapsed_sec"):
            continue

        # elapsed_sec,lat,lon,vendor,device_type,mac,rssi,ble,wifi,surveillance
        if len(row) < 10:
            continue
        try:
            rec = {
                "elapsed_sec": int(row[0]),
                "lat":         float(row[1]),
                "lon":         float(row[2]),
                "vendor":      row[3].strip(),
                "device_type": row[4].strip(),
                "mac":         row[5].strip(),
                "rssi":        int(row[6]),
                "ble":         row[7].strip() == "1",
                "wifi":        row[8].strip() == "1",
                "surveillance": row[9].strip() == "1",
                "session":     session,
            }
            records.append(rec)
        except (ValueError, IndexError):
            continue

    return records


def to_osm_node(rec):
    """Convert a surveillance record to an OSM-compatible node dict for DeFlock."""
    stype = SURVEILLANCE_TYPE_MAP.get(rec["device_type"], "camera")
    ctype = CAMERA_TYPE_MAP.get(rec["device_type"], "fixed")

    node = {
        "lat": rec["lat"],
        "lon": rec["lon"],
        "tags": {
            "man_made":          "surveillance",
            "surveillance:type": stype,
            "surveillance":      "outdoor",
            "surveillance:zone": "traffic",
            "camera:type":       ctype,
        },
    }

    if rec["vendor"]:
        node["tags"]["manufacturer"] = rec["vendor"]
    if rec["device_type"]:
        node["tags"]["description"] = rec["device_type"]

    # detection metadata (not OSM tags, but useful for review/dedup)
    node["_meta"] = {
        "mac":            rec["mac"],
        "rssi":           rec["rssi"],
        "detection":      "BLE" if rec["ble"] else "WiFi",
        "source":         "tbeam_scanner",
        "session":        rec["session"],
    }

    return node


def to_atlas_row(rec):
    """Convert a surveillance record to EFF Atlas of Surveillance CSV fields."""
    return {
        "lat":         rec["lat"],
        "lon":         rec["lon"],
        "technology":  "Automated License Plate Readers" if "ALPR" in rec["device_type"] else "Video Surveillance",
        "vendor":      rec["vendor"],
        "device_type": rec["device_type"],
        "mac":         rec["mac"],
        "detection":   "BLE" if rec["ble"] else "WiFi",
        "rssi":        rec["rssi"],
    }


def deduplicate(records):
    """Deduplicate by MAC, keeping the strongest RSSI and best GPS fix."""
    by_mac = {}
    for r in records:
        mac = r["mac"]
        if mac not in by_mac:
            by_mac[mac] = r
        else:
            prev = by_mac[mac]
            # prefer record with valid GPS
            has_gps = r["lat"] != 0.0 or r["lon"] != 0.0
            prev_gps = prev["lat"] != 0.0 or prev["lon"] != 0.0
            if (has_gps and not prev_gps) or (has_gps == prev_gps and r["rssi"] > prev["rssi"]):
                by_mac[mac] = r
    return list(by_mac.values())


def main():
    parser = argparse.ArgumentParser(description="Download logs from T-Beam Scanner")
    parser.add_argument("--port", help="Serial port (auto-detect if omitted)")
    parser.add_argument("--clear", action="store_true", help="Clear SPIFFS log after download")
    parser.add_argument("--all", action="store_true", help="Include non-surveillance MACs in output")
    parser.add_argument("--no-dedup", action="store_true", help="Skip deduplication")
    args = parser.parse_args()

    # find port
    port = args.port
    if not port:
        ports = find_tbeam_port()
        if not ports:
            print("ERROR: No USB serial port found. Is the T-Beam plugged in?")
            sys.exit(1)
        port = ports[0]
        if len(ports) > 1:
            print(f"Multiple ports found: {ports}")
        print(f"Using port: {port}")

    # connect
    print(f"Connecting at {BAUD} baud...")
    ser = connect(port)
    print("Connected. Sending dump command...")

    # dump
    raw = send_dump(ser)
    if raw is None:
        ser.close()
        sys.exit(1)

    # parse
    records = parse_csv_log(raw)
    total = len(records)
    surv = [r for r in records if r["surveillance"]]
    print(f"Downloaded {total} records ({len(surv)} surveillance hits) across sessions.")

    if not records:
        print("No data to save.")
        if args.clear:
            send_clear(ser)
        ser.close()
        return

    # filter
    output_records = records if args.all else surv
    if not args.no_dedup:
        before = len(output_records)
        output_records = deduplicate(output_records)
        dupes = before - len(output_records)
        if dupes:
            print(f"Deduplicated: {before} → {len(output_records)} ({dupes} duplicates removed)")

    # save files
    os.makedirs(DUMPS_DIR, exist_ok=True)
    ts = datetime.now().strftime("%Y-%m-%d_%H%M%S")

    # 1. Raw CSV (full dump, unmodified)
    raw_path = os.path.join(DUMPS_DIR, f"{ts}_raw.csv")
    with open(raw_path, "w") as f:
        f.write(raw)
    print(f"Raw CSV:          {raw_path}")

    # 2. Surveillance JSON (normalized for OSM/DeFlock + Atlas)
    has_gps = [r for r in output_records if r["lat"] != 0.0 or r["lon"] != 0.0]
    no_gps = [r for r in output_records if r["lat"] == 0.0 and r["lon"] == 0.0]

    export = {
        "meta": {
            "source":     "tbeam_scanner",
            "dump_time":  datetime.now(timezone.utc).isoformat(),
            "total_records": total,
            "surveillance_records": len(surv),
            "exported":   len(output_records),
            "with_gps":   len(has_gps),
            "without_gps": len(no_gps),
        },
        "osm_nodes": [to_osm_node(r) for r in has_gps],
        "atlas_rows": [to_atlas_row(r) for r in has_gps],
        "no_gps": [
            {
                "mac": r["mac"],
                "vendor": r["vendor"],
                "device_type": r["device_type"],
                "rssi": r["rssi"],
                "detection": "BLE" if r["ble"] else "WiFi",
            }
            for r in no_gps
        ],
    }

    json_path = os.path.join(DUMPS_DIR, f"{ts}_surveillance.json")
    with open(json_path, "w") as f:
        json.dump(export, f, indent=2)
    print(f"Surveillance JSON: {json_path}")

    # summary
    if has_gps:
        print(f"\nGPS-tagged surveillance devices ready for upload:")
        for r in has_gps:
            print(f"  {r['vendor']:20s} {r['device_type']:20s} ({r['lat']:.4f}, {r['lon']:.4f})  {r['mac']}  RSSI:{r['rssi']}")
    if no_gps:
        print(f"\n{len(no_gps)} device(s) without GPS fix (not uploadable to map platforms):")
        for r in no_gps:
            print(f"  {r['vendor']:20s} {r['device_type']:20s} {r['mac']}  RSSI:{r['rssi']}")

    # clear if requested
    if args.clear:
        print()
        send_clear(ser)

    ser.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
