#pragma once
#include "board.h"
#include <Arduino_GFX_Library.h>
#include <vector>

// The ST7789 driver instance (created in display.cpp). Landscape after displayInit().
extern Arduino_GFX *gfx;

// Runtime UI accent colour (RGB565). Defaults to COL_ACCENT; overridden from the saved
// theme by settingsLoad(). Everything themeable is derived from this at draw time.
extern uint16_t gAccent;

void displayInit();          // power rails + CS parking + panel + backlight + battery
void uiFlush();              // push the off-screen canvas to the panel
int  scrW();                 // 320 in landscape
int  scrH();                 // 170 in landscape

// Carousel-pill icons (Material Design Icons, tinted at draw time — theme-safe).
enum Icon {
    IC_NONE = 0, IC_APP, IC_EMPTY, IC_INSTALL, IC_USB, IC_SETTINGS, IC_DESIGN,
    IC_DELETE, IC_BACK, IC_PIN, IC_ABOUT, IC_OFF, IC_LEDBRIGHT, IC_LEDCOLOR,
    IC_ANIM, IC_COLOR, IC_THEME, IC_FLASH, IC_FILE, IC_CHECK,
};

struct Tile {
    String label;
    int icon;
    int badge = 0;    // slot number for IC_APP tiles (else 0)
    uint16_t tint = 0; // non-zero => pill is filled with this colour (theme / LED swatches)
};

// --- colour helpers (keep any accent readable) ---
uint16_t uiContrast(uint16_t bg);      // near-black or white, whichever pops on bg
uint16_t uiDim(uint16_t c, uint8_t pct); // scale brightness (0-100%)
uint16_t uiMix(uint16_t a, uint16_t b, uint8_t t); // blend a->b by t/255

// --- primitives (the shared visual language) ---
void uiBackground();                                  // navy + faint dot grid
void uiStatusBar();                                   // battery widget
void uiIconMask(int icon, int cx, int cy, int size, uint16_t color); // scaled 1-bit icon

// --- text (Cyberjunkies bitmap font) ---
void uiTitleBar(const String &title);                 // big centred title + accent rule
void uiTextCenter(const String &s, int y, uint8_t size, uint16_t fg, uint16_t bg = COL_BG);
void uiMessage(const String &s, uint16_t fg = COL_FG);
void uiError(const String &s);
void uiDrawIcon(int icon, int badge, int cx, int cy, int s, uint16_t col, uint16_t bg = COL_TILE);

// The carousel: ONE big icon pill (right) that slides to the next icon as you scroll, plus
// the big focused title (left). heading names the screen. pickerMode=false shows the focused
// item's label as the big title (menus); pickerMode=true shows heading as the big title with
// the focused item's text beneath it (file / option lists). cornerCat (a CatId or -1) draws a
// small looping cat in the bottom-left. Returns the selected index, or -1 on BACK.
int uiCarousel(const std::vector<Tile> &items, const String &heading, bool pickerMode,
               void (*onIdle)() = nullptr, int cornerCat = -1);

// Back-compat wrappers routed through the carousel so every screen shares the look.
int uiGrid(const String &title, const std::vector<Tile> &tiles, void (*onIdle)() = nullptr);
int uiMenu(const String &title, const std::vector<String> &items, void (*onIdle)() = nullptr);

// Wheel text entry (name a firmware). Returns the typed string ("" = cancelled).
String uiTextEntry(const String &title, const String &initial);

// --- Oreo cat + vertical progress (install screen) ---
enum CatId { CAT_ID_SLEEP, CAT_ID_ATTACK };
void uiCatDraw(int catId, int frame, int cx, int cy); // draw one frame centred
int  uiCatFrames(int catId);
int  uiCatDelayMs(int catId);
void uiVBar(int x, int y, int w, int h, int pct, uint16_t fill, uint16_t track); // vertical bar
void uiInstallScreen(const String &name, int pct, int catFrame); // cat + vertical progress
void uiUsbScreen(int catFrame); // USB drive mode: cat + "copy files, press back"
