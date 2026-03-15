/*
 * OUI Database — Known Surveillance Device MAC Prefixes
 *
 * Each entry maps a 3-octet OUI prefix to a vendor and device type.
 * The array is NULL-terminated.
 *
 * To add a new entry, insert before the {NULL, NULL, NULL} terminator.
 * Format: {"XX:XX:XX", "Vendor Name", "Device Type"}
 */

#ifndef OUI_DATABASE_H
#define OUI_DATABASE_H

struct OUIEntry {
  const char* oui;       // First 3 octets: "XX:XX:XX"
  const char* vendor;
  const char* deviceType;
};

OUIEntry knownSurveillanceOUIs[] = {
  // Flock Safety - ALPR Cameras
  {"7C:DB:98", "Flock Safety", "ALPR Camera"},
  {"D4:A0:2A", "Flock Safety", "ALPR Camera"},
  {"E8:6D:CB", "Flock Safety", "ALPR Camera"},
  {"B0:A7:32", "Flock Safety", "ALPR Camera"},

  // Hikvision - IP Cameras
  {"C0:56:E3", "Hikvision", "IP Camera"},
  {"44:19:B6", "Hikvision", "IP Camera"},
  {"A4:14:37", "Hikvision", "IP Camera"},
  {"54:C4:15", "Hikvision", "IP Camera"},
  {"BC:AD:28", "Hikvision", "IP Camera"},
  {"C4:2F:90", "Hikvision", "IP Camera"},
  {"18:68:CB", "Hikvision", "IP Camera"},
  {"80:D0:1A", "Hikvision", "IP Camera"},

  // Dahua Technology - IP Cameras
  {"3C:EF:8C", "Dahua", "IP Camera"},
  {"40:E4:8C", "Dahua", "IP Camera"},
  {"A0:BD:1D", "Dahua", "IP Camera"},
  {"E0:50:8B", "Dahua", "IP Camera"},
  {"B0:C5:CA", "Dahua", "IP Camera"},
  {"90:02:A9", "Dahua", "IP Camera"},

  // Verkada - Cloud Cameras
  {"D4:20:B0", "Verkada", "Cloud Camera"},
  {"F4:52:14", "Verkada", "Cloud Camera"},

  // Ring / Amazon - Doorbells & Cameras
  {"34:D2:70", "Ring/Amazon", "Doorbell/Camera"},
  {"5C:41:5A", "Ring/Amazon", "Doorbell/Camera"},
  {"4C:AB:4F", "Ring/Amazon", "Doorbell/Camera"},
  {"6C:56:97", "Ring/Amazon", "Doorbell/Camera"},
  {"FC:A1:83", "Ring/Amazon", "Doorbell/Camera"},
  {"90:F1:AA", "Ring/Amazon", "Doorbell/Camera"},

  // Nest / Google - Security Cameras
  {"64:16:66", "Nest/Google", "Security Camera"},
  {"18:B4:30", "Nest/Google", "Security Camera"},
  {"A4:77:33", "Nest/Google", "Security Camera"},

  // Arlo Technologies - Wireless Cameras
  {"9C:B7:0D", "Arlo", "Wireless Camera"},
  {"E4:4F:29", "Arlo", "Wireless Camera"},
  {"A0:43:DB", "Arlo", "Wireless Camera"},

  // Wyze Labs - Budget Cameras
  {"2C:AA:8E", "Wyze", "Budget Camera"},
  {"7C:78:B2", "Wyze", "Budget Camera"},
  {"D0:3F:27", "Wyze", "Budget Camera"},

  // Eufy / Anker - Security Cameras
  {"98:8C:33", "Eufy/Anker", "Security Camera"},
  {"C8:C2:FA", "Eufy/Anker", "Security Camera"},

  // Axis Communications - Professional Cameras
  {"00:40:8C", "Axis", "Professional Camera"},
  {"AC:CC:8E", "Axis", "Professional Camera"},
  {"B8:A4:4F", "Axis", "Professional Camera"},

  // Amcrest - IP Cameras
  {"9C:8E:CD", "Amcrest", "IP Camera"},

  // Redflex - Traffic/Red-Light Cameras
  {"00:1E:C0", "Redflex", "Traffic Camera"},

  // Sensys Networks - Traffic Sensors
  {"00:06:8E", "Sensys Networks", "Traffic Sensor"},

  // Motorola Solutions (Vigilant/ALPR)
  {"00:17:4B", "Motorola Solutions", "ALPR System"},
  {"00:1A:77", "Motorola Solutions", "ALPR System"},

  // DJI - Drones (RemoteID)
  {"60:60:1F", "DJI", "Drone"},
  {"34:D2:62", "DJI", "Drone"},
  {"48:1C:B9", "DJI", "Drone"},

  // Genetec - Security Platform
  {"00:18:85", "Genetec", "Security Platform"},

  // Bosch Security - Cameras
  {"00:07:5F", "Bosch", "Security Camera"},
  {"00:04:56", "Bosch", "Security Camera"},

  // Hanwha (Samsung) Techwin - Cameras
  {"00:09:18", "Hanwha/Samsung", "Security Camera"},
  {"00:16:6C", "Hanwha/Samsung", "Security Camera"},

  // Pelco (Schneider Electric) - Cameras
  {"00:0A:1A", "Pelco", "Security Camera"},

  // Vivotek - IP Cameras
  {"00:02:D1", "Vivotek", "IP Camera"},

  // Ubiquiti - Network Cameras
  {"FC:EC:DA", "Ubiquiti", "Network Camera"},
  {"24:5A:4C", "Ubiquiti", "Network Camera"},
  {"B4:FB:E4", "Ubiquiti", "Network Camera"},
  {"78:8A:20", "Ubiquiti", "Network Camera"},

  // Terminator
  {NULL, NULL, NULL}
};

#endif // OUI_DATABASE_H
