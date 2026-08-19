#include "display.h"
#include "battery.h"
#include "cat_data.h"
#include "cyberfont.h"
#include "icons_data.h"
#include "input.h"
#include <SD.h>
#include <math.h>

// SPI bus shared with the SD card; the ST7789 is the only device we drive here.
static Arduino_DataBus *bus =
    new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, TFT_MISO, &SPI, true /*shared*/);

// The physical panel is created UNROTATED (170x320); an off-screen Canvas applies the
// landscape rotation and holds a full-frame buffer, so the screen never blanks (no flicker).
static Arduino_GFX *panel = new Arduino_ST7789(
    bus, TFT_RST, 0, TFT_IS_IPS, TFT_NATIVE_W, TFT_NATIVE_H, TFT_COL_OFS, TFT_ROW_OFS, TFT_COL_OFS,
    TFT_ROW_OFS);
static Arduino_Canvas *canvas =
    new Arduino_Canvas(TFT_NATIVE_W, TFT_NATIVE_H, panel, 0, 0, BOARD_ROTATION);
Arduino_GFX *gfx = canvas;
uint16_t gAccent = COL_ACCENT;

void uiFlush() { canvas->flush(); }
int scrW() { return gfx->width(); }
int scrH() { return gfx->height(); }

static const int SB = 17;             // status-bar height
static const uint16_t COL_INK_HI = 0xFFFF; // titles
static const uint16_t COL_DOT = 0x1965;    // faint dot grid on navy

// ---- colour helpers -------------------------------------------------------
static uint8_t lum565(uint16_t c) {
    // expand each channel to 0..255, then Rec.601 luma (weights sum to 256)
    uint16_t r = ((c >> 11) & 0x1F) * 255 / 31;
    uint16_t g = ((c >> 5) & 0x3F) * 255 / 63;
    uint16_t b = (c & 0x1F) * 255 / 31;
    return (uint8_t)((r * 54 + g * 183 + b * 19) >> 8);
}
uint16_t uiContrast(uint16_t bg) { return lum565(bg) > 150 ? 0x0000 : 0xFFFF; }
uint16_t uiDim(uint16_t c, uint8_t pct) {
    uint16_t r = ((c >> 11) & 0x1F) * pct / 100, g = ((c >> 5) & 0x3F) * pct / 100,
             b = (c & 0x1F) * pct / 100;
    return (r << 11) | (g << 5) | b;
}
uint16_t uiMix(uint16_t a, uint16_t b, uint8_t t) {
    int ra = (a >> 11) & 0x1F, ga = (a >> 5) & 0x3F, ba = a & 0x1F;
    int rb = (b >> 11) & 0x1F, gb = (b >> 5) & 0x3F, bb = b & 0x1F;
    int r = ra + (rb - ra) * t / 255, g = ga + (gb - ga) * t / 255, bl = ba + (bb - ba) * t / 255;
    return (r << 11) | (g << 5) | bl;
}

static void powerRailsAndParkCS() {
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);
    const int cs[] = {CC1101_SS_PIN, TFT_CS, SDCARD_CS, 44};
    for (int p : cs) {
        pinMode(p, OUTPUT);
        digitalWrite(p, HIGH);
    }
}

void displayInit() {
    powerRailsAndParkCS();
    gfx->begin();
    pinMode(TFT_BL, OUTPUT);
    analogWrite(TFT_BL, 220);
    gfx->fillScreen(COL_BG);
    uiFlush();
    batteryInit();
}

// ---- background + status bar ----------------------------------------------
void uiBackground() {
    gfx->fillScreen(COL_BG);
    for (int y = SB + 6; y < scrH(); y += 12)
        for (int x = 6; x < scrW(); x += 12)
            gfx->drawPixel(x, y, COL_DOT); // faint hacker-ish grid
}

void uiStatusBar() {
    static uint32_t lastMs = 0;
    static bool have = false;
    static int pct = 0, mv = 0;
    static bool chg = false;
    uint32_t now = millis();
    if (!lastMs || now - lastMs > 1200) { // cache the I2C read (short, so charge state stays live)
        have = batteryRead(pct, mv, chg);
        lastMs = now ? now : 1;
    }
    // battery body
    const int bx = 6, by = 4, bw = 24, bh = 11;
    gfx->drawRoundRect(bx, by, bw, bh, 2, gAccent);
    gfx->fillRect(bx + bw, by + 3, 2, bh - 6, gAccent); // nub
    if (have) {
        int fw = (bw - 4) * pct / 100;
        if (fw > 0) gfx->fillRoundRect(bx + 2, by + 2, fw, bh - 4, 1, gAccent);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", pct);
        cyDrawTop(gfx, CY_S, bx + bw + 8, by - 1, buf, COL_INK_HI, 1);
        if (chg) // clear charging bolt to the right of the percentage
            uiIconMask(IC_FLASH, bx + bw + 8 + cyTextW(CY_S, buf, 1) + 11, by + bh / 2, 16, gAccent);
    }
    // thin accent divider under the bar
    gfx->drawFastHLine(0, SB, scrW(), uiDim(gAccent, 40));
}

// ---- scaled 1-bit icon mask ------------------------------------------------
static inline bool iconBit(const uint8_t *row, int sx) {
    return row[sx >> 3] & (0x80 >> (sx & 7));
}

// Optional icon set loaded from a theme pack; null = use the compiled-in ICON_BITS.
static const size_t kIconBytes = (size_t)ICON_STRIDE * ICON_H;
static uint8_t *sIconPack = nullptr;

static inline const uint8_t *iconRows(int icon) {
    return sIconPack ? (sIconPack + (size_t)icon * kIconBytes) : ICON_BITS[icon];
}

void uiClearIconPack() {
    if (sIconPack) {
        free(sIconPack);
        sIconPack = nullptr;
    }
}

// icons.bin: "TEIC", version(1), count, width, height, then count * stride * height mask bytes.
bool uiLoadIconPack(const String &path) {
    File f = SD.open(path);
    if (!f) return false;
    uint8_t hdr[8];
    bool ok = f.read(hdr, sizeof(hdr)) == (int)sizeof(hdr) && hdr[0] == 'T' && hdr[1] == 'E' &&
              hdr[2] == 'I' && hdr[3] == 'C' && hdr[4] == 1 && hdr[6] == ICON_W && hdr[7] == ICON_H;
    const int count = ok ? hdr[5] : 0;
    if (!ok || count < 1 || count > ICON_COUNT) {
        f.close();
        return false;
    }
    uint8_t *buf = (uint8_t *)ps_malloc(kIconBytes * ICON_COUNT);
    if (!buf) buf = (uint8_t *)malloc(kIconBytes * ICON_COUNT);
    if (!buf) {
        f.close();
        return false;
    }
    // Start from the built-ins so a short pack only overrides the icons it actually ships.
    for (int i = 0; i < ICON_COUNT; ++i) memcpy(buf + (size_t)i * kIconBytes, ICON_BITS[i], kIconBytes);
    const bool read = f.read(buf, kIconBytes * count) == (int)(kIconBytes * count);
    f.close();
    if (!read) {
        free(buf);
        return false;
    }
    uiClearIconPack();
    sIconPack = buf;
    return true;
}

void uiIconMask(int icon, int cx, int cy, int size, uint16_t color) {
    if (icon <= 0 || icon >= ICON_COUNT || size <= 0) return;
    const uint8_t *bits = iconRows(icon);
    const int x0 = cx - size / 2, y0 = cy - size / 2;
    for (int dy = 0; dy < size; ++dy) {
        const uint8_t *row = bits + (dy * ICON_H / size) * ICON_STRIDE;
        int dx = 0;
        while (dx < size) {
            while (dx < size && !iconBit(row, dx * ICON_W / size)) ++dx;
            if (dx >= size) break;
            int start = dx;
            while (dx < size && iconBit(row, dx * ICON_W / size)) ++dx;
            gfx->fillRect(x0 + start, y0 + dy, dx - start, 1, color);
        }
    }
}

// ---- text helpers ----------------------------------------------------------
static String upper(const String &s) {
    String u = s;
    u.toUpperCase();
    return u;
}
// Compact centred title for the secondary screens (about / flash / usb / pin / text entry).
// The big hero titles live on the carousel; this stays short so dense screens have room.
void uiTitleBar(const String &title) {
    String up = upper(title);
    int scale = 1;
    int tw = cyTextW(CY_L, up.c_str(), scale);
    int x = (scrW() - tw) / 2;
    int top, bot;
    cyBounds(CY_L, up.c_str(), scale, top, bot);
    cyDrawTop(gfx, CY_L, x, 4, up.c_str(), COL_INK_HI, scale);
    gfx->fillRect(x, 4 + bot + 3, tw, 2, gAccent);
}

void uiTextCenter(const String &s, int y, uint8_t size, uint16_t fg, uint16_t bg) {
    (void)bg;
    int scale = size < 1 ? 1 : size;
    int tw = cyTextW(CY_S, s.c_str(), scale);
    cyDrawTop(gfx, CY_S, (scrW() - tw) / 2, y, s.c_str(), fg, scale);
}

void uiMessage(const String &s, uint16_t fg) {
    uiBackground();
    const int cardH = 52, y = (scrH() - cardH) / 2;
    gfx->fillRoundRect(18, y, scrW() - 36, cardH, 12, uiDim(gAccent, 18));
    gfx->drawRoundRect(18, y, scrW() - 36, cardH, 12, gAccent);
    uint16_t col = (fg == COL_FG) ? COL_INK_HI : fg;
    String up = upper(s);
    int tw = cyTextW(CY_S, up.c_str(), 2);
    cyDrawTop(gfx, CY_S, (scrW() - tw) / 2, y + cardH / 2 - CY_S.lineH, up.c_str(), col, 2);
    uiFlush();
}

void uiError(const String &s) {
    uiBackground();
    const int cardH = 52, y = (scrH() - cardH) / 2;
    gfx->fillRoundRect(18, y, scrW() - 36, cardH, 12, 0x3000);
    gfx->drawRoundRect(18, y, scrW() - 36, cardH, 12, COL_ERR);
    String up = upper(s);
    int tw = cyTextW(CY_S, up.c_str(), 2);
    cyDrawTop(gfx, CY_S, (scrW() - tw) / 2, y + cardH / 2 - CY_S.lineH, up.c_str(), COL_ERR, 2);
    uiFlush();
}

void uiDrawIcon(int icon, int badge, int cx, int cy, int s, uint16_t col, uint16_t bg) {
    (void)badge;
    (void)bg;
    uiIconMask(icon, cx, cy, s > 0 ? s : 40, col);
}

// ---- the carousel ----------------------------------------------------------
// ---- single big icon that slides between selections (cleaner than a stack of pills) ----
static const int PILL = 88;                 // the one big pill on the right
static int pillCX() { return scrW() - 52; } // pill centre X
static int pillCY() { return SB + (scrH() - SB) / 2; }
static void catCorner(int catId);           // defined in the cat section below
static float easeOut(float t) { return 1.0f - powf(1.0f - t, 3.0f); }

// Icon mask, but only rows whose screen-y falls within [clipTop,clipBot] are drawn — lets the
// icon slide inside the pill during a transition without spilling onto the background.
static void iconMaskClip(int icon, int cx, int cy, int size, uint16_t color, int clipTop,
                         int clipBot) {
    if (icon <= 0 || icon >= ICON_COUNT || size <= 0) return;
    const uint8_t *bits = iconRows(icon);
    const int x0 = cx - size / 2, y0 = cy - size / 2;
    for (int dy = 0; dy < size; ++dy) {
        const int py = y0 + dy;
        if (py < clipTop || py > clipBot) continue;
        const uint8_t *row = bits + (dy * ICON_H / size) * ICON_STRIDE;
        int dx = 0;
        while (dx < size) {
            while (dx < size && !iconBit(row, dx * ICON_W / size)) ++dx;
            if (dx >= size) break;
            int start = dx;
            while (dx < size && iconBit(row, dx * ICON_W / size)) ++dx;
            gfx->fillRect(x0 + start, py, dx - start, 1, color);
        }
    }
}

// Draw one item's icon (or nothing for a pure colour swatch) centred at y, clipped to the pill.
static void drawItemIcon(const Tile &t, bool pickerMode, int cy, uint16_t ink, int clipTop,
                         int clipBot) {
    const int cx = pillCX();
    const int is = (int)(PILL * 0.72f);
    if (!pickerMode && t.icon == IC_APP) {
        iconMaskClip(IC_APP, cx, cy - 8, is, ink, clipTop, clipBot);
        if (t.badge) {
            char b[4];
            snprintf(b, sizeof(b), "%d", t.badge);
            cyDrawTop(gfx, CY_S, cx - cyTextW(CY_S, b, 2) / 2, cy + PILL / 2 - CY_S.lineH * 2 - 2, b, ink, 2);
        }
    } else if (t.icon == IC_NONE) {
        if (!t.tint) iconMaskClip(IC_FILE, cx, cy, is, ink, clipTop, clipBot); // generic list chip
        // pure colour swatch: the pill colour is the content, no icon
    } else {
        iconMaskClip(t.icon, cx, cy, is, ink, clipTop, clipBot);
    }
}

// Draw a title left-aligned with a FIXED baseline, so the bottom of the caps never shifts as
// different labels scroll past. Keeps the biggest scale that fits: tightens letter spacing
// before it gives up a scale step, so long headings (e.g. "LED COLOR") stay large.
static void cyLeftBaseline(const CyFont &f, const String &s, int x, int baselineY, int w,
                           int maxScale, uint16_t col) {
    int scale = maxScale, track = 1;
    if (cyTextW(f, s.c_str(), scale, 1) > w) {
        if (cyTextW(f, s.c_str(), scale, 0) <= w) {
            track = 0; // stay big, just tighten
        } else {
            while (scale > 1 && cyTextW(f, s.c_str(), scale, 1) > w) scale--;
        }
    }
    cyDrawTop(gfx, f, x, baselineY - f.ascent * scale, s.c_str(), col, scale, track);
}

static uint16_t pillFill(const Tile &t, bool pickerMode) {
    bool empty = (!pickerMode && t.icon == IC_EMPTY);
    uint16_t base = t.tint ? t.tint : gAccent; // theme / LED swatches show their own colour
    return uiDim(base, empty ? 40 : 100);
}

// Everything except the sliding icon: background, status, title, the pill, the corner cat.
static void drawCarouselBase(int focusIdx, bool pickerMode, const String &heading,
                             const std::vector<Tile> &items, int cornerCat) {
    uiBackground();
    uiStatusBar();
    const int leftX = 12;
    const int leftW = pillCX() - PILL / 2 - 10 - leftX;
    if (pickerMode) {
        cyLeftBaseline(CY_L, upper(heading), leftX, SB + 42, leftW, 2, gAccent);
        cyLeftBaseline(CY_S, upper(items[focusIdx].label), leftX, SB + 88, leftW, 2, COL_INK_HI);
    } else {
        cyLeftBaseline(CY_L, upper(items[focusIdx].label), leftX, SB + (scrH() - SB) / 2 + 14,
                       leftW, 2, COL_INK_HI);
    }
    uint16_t fill = pillFill(items[focusIdx], pickerMode);
    gfx->fillRoundRect(pillCX() - PILL / 2, pillCY() - PILL / 2, PILL, PILL, PILL / 4, fill);
    if (cornerCat >= 0) catCorner(cornerCat);
}

static void drawCarouselStatic(int focusIdx, bool pickerMode, const String &heading,
                               const std::vector<Tile> &items, int cornerCat) {
    drawCarouselBase(focusIdx, pickerMode, heading, items, cornerCat);
    const Tile &t = items[focusIdx];
    bool empty = (!pickerMode && t.icon == IC_EMPTY);
    uint16_t ink = empty ? uiDim(gAccent, 70) : uiContrast(pillFill(t, pickerMode));
    drawItemIcon(t, pickerMode, pillCY(), ink, pillCY() - PILL / 2, pillCY() + PILL / 2);
    uiFlush();
}

// Draw one item's icon centred at a given size (no badge; used only during the swap animation).
static void drawIconScaled(const Tile &t, bool pickerMode, int size, uint16_t ink) {
    if (size < 3) return;
    int ic = t.icon;
    if (ic == IC_NONE) {
        if (!t.tint) uiIconMask(IC_FILE, pillCX(), pillCY(), size, ink);
        return; // pure colour swatch: nothing to scale
    }
    uiIconMask(ic, pillCX(), pillCY(), size, ink);
}

// Clean swap: the old icon shrinks away in place, then the new one grows in place. Nothing
// slides in from outside the pill, so no odd clipping at the edges.
static void animateSwap(int fromIdx, int toIdx, bool pickerMode, const String &heading,
                        const std::vector<Tile> &items, int cornerCat) {
    const int STEPS = 6;
    const int fullIs = (int)(PILL * 0.72f);
    const uint16_t ink = uiContrast(pillFill(items[toIdx], pickerMode));
    for (int s = 1; s <= STEPS; ++s) {
        float p = (float)s / STEPS;
        drawCarouselBase(toIdx, pickerMode, heading, items, cornerCat);
        if (p < 0.5f) drawIconScaled(items[fromIdx], pickerMode, (int)(fullIs * (1.0f - p * 2.0f)), ink);
        else drawIconScaled(items[toIdx], pickerMode, (int)(fullIs * ((p - 0.5f) * 2.0f)), ink);
        uiFlush();
    }
}

int uiCarousel(const std::vector<Tile> &items, const String &heading, bool pickerMode,
               void (*onIdle)(), int cornerCat) {
    inputDrain();
    const int n = items.size();
    if (n == 0) return -1;
    int sel = 0;
    drawCarouselStatic(0, pickerMode, heading, items, cornerCat);
    uint32_t lastCat = millis();
    for (;;) {
        if (onIdle) onIdle();
        InputEvent e = inputPoll();
        if (e == EV_RIGHT) { // wraps last -> first with no cut
            int to = (sel + 1) % n;
            animateSwap(sel, to, pickerMode, heading, items, cornerCat);
            sel = to;
            drawCarouselStatic(sel, pickerMode, heading, items, cornerCat); // settle (badge, etc.)
            lastCat = millis();
        } else if (e == EV_LEFT) {
            int to = (sel - 1 + n) % n;
            animateSwap(sel, to, pickerMode, heading, items, cornerCat);
            sel = to;
            drawCarouselStatic(sel, pickerMode, heading, items, cornerCat);
            lastCat = millis();
        } else if (e == EV_PRESS) {
            return sel;
        } else if (e == EV_BACK) {
            return -1;
        } else {
            // Idle repaint: fast for the corner cat, slow otherwise — this also keeps the status
            // bar (battery / charging bolt) live so it updates when the charger is plugged/pulled.
            uint32_t interval = (cornerCat >= 0) ? (uint32_t)uiCatDelayMs(cornerCat) : 1500;
            if (millis() - lastCat > interval) {
                drawCarouselStatic(sel, pickerMode, heading, items, cornerCat);
                lastCat = millis();
            }
        }
        delay(6);
    }
}

int uiGrid(const String &title, const std::vector<Tile> &tiles, void (*onIdle)()) {
    return uiCarousel(tiles, title, false, onIdle);
}

int uiMenu(const String &title, const std::vector<String> &items, void (*onIdle)()) {
    std::vector<Tile> t;
    t.reserve(items.size());
    for (const String &s : items) t.push_back({s, IC_NONE, 0});
    return uiCarousel(t, title, true, onIdle);
}

// ---- wheel text entry ------------------------------------------------------
// Wheel entries are the letters/digits/space, then DEL and SAVE. The cursor starts on SAVE, so
// a pre-filled name can be accepted with one press. The physical BACK button cancels the whole
// entry (returns "") — the caller uses that to step back rather than proceed.
String uiTextEntry(const String &title, const String &initial) {
    static const char CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
    const int NC = (int)sizeof(CHARS) - 1;
    const int DEL = NC;         // delete-last entry
    const int DONE = NC + 1;    // save entry
    const int NENTRIES = NC + 2;
    const int MAXLEN = 16;
    String text = initial;
    int cur = DONE; // default to SAVE
    inputDrain();
    bool redraw = true;
    for (;;) {
        if (redraw) {
            uiBackground();
            uiTitleBar(title);
            String shown = text.length() ? text : String("_");
            uiTextCenter(shown, 44, 2, COL_INK_HI);
            String label = (cur == DONE)         ? String("SAVE")
                           : (cur == DEL)        ? String("DEL")
                           : (CHARS[cur] == ' ') ? String("SPACE")
                                                 : String(CHARS[cur]);
            const int bw = 120, bh = 48, bx = (scrW() - bw) / 2, by = 78;
            gfx->fillRoundRect(bx, by, bw, bh, 12, gAccent);
            uint16_t ink = uiContrast(gAccent);
            int sc = label.length() == 1 ? 3 : 2;
            int tw = cyTextW(CY_S, label.c_str(), sc);
            cyDrawTop(gfx, CY_S, bx + (bw - tw) / 2, by + (bh - CY_S.lineH * sc) / 2, label.c_str(), ink, sc);
            const int my = by + bh / 2;
            gfx->fillTriangle(bx - 16, my, bx - 6, my - 8, bx - 6, my + 8, uiDim(gAccent, 60));
            gfx->fillTriangle(bx + bw + 16, my, bx + bw + 6, my - 8, bx + bw + 6, my + 8, uiDim(gAccent, 60));
            uiTextCenter("turn pick   press select   back cancel", scrH() - 12, 1, uiDim(gAccent, 80));
            uiFlush();
            redraw = false;
        }
        switch (inputPoll()) {
            case EV_RIGHT: cur = (cur + 1) % NENTRIES; redraw = true; break;
            case EV_LEFT: cur = (cur - 1 + NENTRIES) % NENTRIES; redraw = true; break;
            case EV_PRESS:
                if (cur == DONE) { text.trim(); return text; }
                if (cur == DEL) { if (text.length()) text.remove(text.length() - 1); }
                else if ((int)text.length() < MAXLEN) text += CHARS[cur];
                redraw = true;
                break;
            case EV_BACK:
                return String(""); // cancel the whole entry -> caller steps back
            default: break;
        }
        delay(8);
    }
}

// ---- Oreo cat + vertical progress -----------------------------------------
// ESP32-S3 ROM Deflate decoder (miniz tinfl); flag 1 = parse the zlib header.
extern "C" size_t
tinfl_decompress_mem_to_mem(void *pOut, size_t out_len, const void *pSrc, size_t src_len, int flags);

static const CatAnim &catOf(int id) {
    return (id == CAT_ID_ATTACK) ? CAT_ATTACK : CAT_SLEEP;
}
int uiCatFrames(int id) { return catOf(id).frames; }
int uiCatDelayMs(int id) { return catOf(id).delayMs; }

// The frames live in flash Deflate-compressed (~32x smaller). Inflate on first use and keep the
// indices in PSRAM; both cats together are ~113 KB, which PSRAM has in abundance.
static uint8_t *sCatCache[2] = {nullptr, nullptr};
static const uint8_t *catIndices(int id) {
    const int slot = (id == CAT_ID_ATTACK) ? 1 : 0;
    if (sCatCache[slot]) return sCatCache[slot];
    const CatAnim &a = catOf(id);
    uint8_t *buf = (uint8_t *)ps_malloc(a.rawlen);
    if (!buf) buf = (uint8_t *)malloc(a.rawlen);
    if (!buf) return nullptr;
    if (tinfl_decompress_mem_to_mem(buf, a.rawlen, a.z, a.zlen, 1) != a.rawlen) {
        free(buf);
        return nullptr;
    }
    sCatCache[slot] = buf;
    return buf;
}

// Palette-indexed blit: index 0 is transparent, so no separate mask is needed. Runs of the same
// index are drawn with one fillRect, which is a lot cheaper than per-pixel writes.
void uiCatDraw(int id, int frame, int cx, int cy) {
    const CatAnim &a = catOf(id);
    const uint8_t *base = catIndices(id);
    if (!base) return;
    frame %= a.frames;
    const uint8_t *idx = base + (uint32_t)frame * a.w * a.h;
    const int x0 = cx - a.w / 2, y0 = cy - a.h / 2;
    for (int y = 0; y < a.h; ++y) {
        const uint8_t *row = idx + (uint32_t)y * a.w;
        int x = 0;
        while (x < a.w) {
            const uint8_t v = row[x];
            int run = 1;
            while (x + run < a.w && row[x + run] == v) ++run;
            if (v) gfx->fillRect(x0 + x, y0 + y, run, 1, a.pal[v]);
            x += run;
        }
    }
}

// Small looping cat tucked into the bottom-left corner of the carousel.
static void catCorner(int catId) {
    const CatAnim &a = catOf(catId);
    int frame = (int)(millis() / (uint32_t)a.delayMs) % a.frames;
    uiCatDraw(catId, frame, 6 + a.w / 2, scrH() - 4 - a.h / 2);
}

void uiVBar(int x, int y, int w, int h, int pct, uint16_t fill, uint16_t track) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    gfx->fillRoundRect(x, y, w, h, w / 2, track);
    int fh = (h - 4) * pct / 100;
    if (fh > 0) gfx->fillRoundRect(x + 2, y + h - 2 - fh, w - 4, fh, (w - 4) / 2, fill);
}

void uiInstallScreen(const String &name, int pct, int catFrame) {
    uiBackground();
    const char *h = "INSTALLING";
    int tw = cyTextW(CY_S, h, 2);
    cyDrawTop(gfx, CY_S, (scrW() - tw) / 2, 5, h, COL_INK_HI, 2);
    String up = upper(name);
    int nw = cyTextW(CY_S, up.c_str(), 1);
    cyDrawTop(gfx, CY_S, (scrW() - nw) / 2, 29, up.c_str(), gAccent, 1);
    const int barW = 14, barX = scrW() - 28, barTop = 46, barH = scrH() - barTop - 12;
    uiVBar(barX, barTop, barW, barH, pct, gAccent, uiDim(gAccent, 22));
    // big attack cat, centred in the area left of the progress bar
    uiCatDraw(CAT_ID_ATTACK, catFrame, (barX - 12) / 2, 44 + (scrH() - 44) / 2);
    uiFlush();
}

// USB drive mode screen — big animated sleeping cat. The SPI bus is mutex-shared with the SD,
// so redrawing here just serialises with the PC's transfers (no corruption).
void uiUsbScreen(int catFrame) {
    uiBackground();
    const char *h = "USB DRIVE";
    int tw = cyTextW(CY_S, h, 2);
    cyDrawTop(gfx, CY_S, (scrW() - tw) / 2, 6, h, COL_INK_HI, 2);
    uiIconMask(IC_USB, scrW() / 2 + tw / 2 + 20, 6 + CY_S.lineH, 20, gAccent);
    uiCatDraw(CAT_ID_SLEEP, catFrame, scrW() / 2, scrH() / 2 + 14);
    uiTextCenter("copy files, then press BACK", scrH() - 13, 1, uiDim(gAccent, 85));
    uiFlush();
}
