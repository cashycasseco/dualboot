#pragma once
#include <Arduino.h>
// Arduino.h pulls in the board variant (boards/pinouts/pins_arduino.h), which already
// defines: PIN_POWER_ON(15), ROTATION(3), ENCODER_INA(4)/INB(5)/KEY(0), SEL_BTN(0),
// BK_BTN(6), BTN_ACT(LOW), RGB_LED(14), LED_COUNT(8), HAS_RGB_LED, FP/FM/FG, SDCARD_*.
// This file only adds what the variant does not: the TFT wiring and the UI palette.

// ---- Display (Arduino_GFX / ST7789, SPI bus shared with the SD card) ----
#define TFT_SCLK    11
#define TFT_MOSI    9
#define TFT_MISO    10
#define TFT_CS      41
#define TFT_DC      16
#define TFT_RST     -1
#define TFT_BL      21          // backlight enable (active high)
#define TFT_NATIVE_W 170        // native panel size (portrait)
#define TFT_NATIVE_H 320
#define TFT_COL_OFS 35          // 170-wide ST7789 needs a 35 px column offset
#define TFT_ROW_OFS 0
#define TFT_IS_IPS  true
#define BOARD_ROTATION ROTATION // 3 = landscape (320x170); flip to 1 if upside down

// ---- UI palette (RGB565) — retro "grid launcher" look ----
#define COL_BG      0x10A7      // deep navy background
#define COL_FG      0xFFFF      // white (titles, labels on navy)
#define COL_TILE    0xFFFF      // white icon tiles / pills
#define COL_INK     0x10A7      // navy ink for icons/text on white tiles
#define COL_ACCENT  0x2FEB      // default accent = green (runtime override: gAccent)
#define COL_CARD    0x1C2C      // slightly lighter navy (empty tiles / panels)
#define COL_MUTED   0x8410      // muted grey-blue
#define COL_FRAME   0x39C7      // navy border
#define COL_OK      0x2FEB      // green
#define COL_WARN    0xFD20      // amber
#define COL_ERR     0xF9A6      // red

// ---- Boot / brand ----
#define LAUNCHER_NAME "DUALBOOT"
