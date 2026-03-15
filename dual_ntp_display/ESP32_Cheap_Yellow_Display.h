// =========================================================================
// CYD (Cheap Yellow Display) TFT_eSPI User Setup
// For: Elegoo 2.8" ESP32 CYD clone with USB-C connector
// Driver: ILI9341, 320x240
// Touch: XPT2046 (resistive)
//
// Place this file in:
//   Arduino/libraries/TFT_eSPI/User_Setups/ESP32_Cheap_Yellow_Display.h
// =========================================================================

#define USER_SETUP_ID 206

// ---- Driver ---------------------------------------------------------------
#define ILI9341_DRIVER

// ---- TFT SPI pins (standard CYD pinout) -----------------------------------
#define TFT_MISO  12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15   // Chip select
#define TFT_DC     2   // Data / Command
#define TFT_RST   -1   // Reset tied to EN, no dedicated pin needed

// ---- Backlight ------------------------------------------------------------
#define TFT_BL          21   // Backlight control pin
#define TFT_BACKLIGHT_ON HIGH

// ---- Touch (XPT2046 shares SPI bus) ---------------------------------------
#define TOUCH_CS  33

// ---- Fonts to load --------------------------------------------------------
#define LOAD_GLCD    // Font 1. Original Adafruit 8 pixel font
#define LOAD_FONT2   // Font 2. Small 16 pixel high font
#define LOAD_FONT4   // Font 4. Medium 26 pixel high font
#define LOAD_FONT6   // Font 6. Large 48 pixel high font
#define LOAD_FONT7   // Font 7. 7-segment 48 pixel high font (used by NTP clock)
#define LOAD_FONT8   // Font 8. Large 75 pixel high font
#define LOAD_GFXFF   // FreeFonts - includes many font options

#define SMOOTH_FONT

// ---- SPI speeds -----------------------------------------------------------
#define SPI_FREQUENCY        55000000   // TFT
#define SPI_READ_FREQUENCY   20000000   // TFT read
#define SPI_TOUCH_FREQUENCY   2500000   // Touch controller
