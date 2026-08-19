#include "settings.h"
#include "board.h"
#include "bootanim.h"
#include "display.h"
#include "input.h"
#include "led.h"
#include "sdcard.h"
#include "serial_console.h"
#include <Preferences.h>
#include <SD.h>
#include <algorithm>
#include <esp_sleep.h>
#include <math.h>
#include <vector>

// Shared palette for the accent + LED pickers. Each swatch carries an RGB565 value for the
// screen and a full-scale 8-bit RGB triple for the WS2812 strip (scaled by brightness).
struct Swatch {
    const char *name;
    uint16_t rgb565;
    uint8_t r, g, b;
};
static const Swatch PAL[] = {
    {"Violet", 0x8A1F, 140, 10, 220},
    {"Blue", 0x2D7F, 0, 60, 255},
    {"Cyan", 0x07FF, 0, 160, 200},
    {"Green", 0x2FEB, 0, 220, 40},
    {"Amber", 0xFD20, 255, 120, 0},
    {"Red", 0xF9A6, 255, 0, 30},
    {"Pink", 0xFC3F, 255, 30, 140},
    {"Grey", 0xC618, 190, 190, 190}, // Mono theme: light grey so dark icons stay readable
};
static const int PAL_N = (int)(sizeof(PAL) / sizeof(PAL[0]));

// The live theme is stored as raw colours (not palette indices) so a custom theme can be ANY
// colour. Default = green.
static uint16_t sAccent = 0x2FEB;
static uint8_t sLedR = 0, sLedG = 220, sLedB = 40;
static bool sLedOn = true;
static int briPct = 40;              // LED brightness percent (0 = off .. 100)
static bool bootAnimOn = true;       // play the boot animation at startup
static String sThemeDir;             // active theme-pack folder ("" = none/built-in)

bool bootAnimEnabled() { return bootAnimOn; }

// The active theme pack's boot-frame folder, or "" when it has none. bootanim.cpp prefers
// this over the generic /boot folder, so a pack can ship its own startup animation.
String themeBootDir() {
    if (sThemeDir.length() && SD.exists(sThemeDir + "/boot")) return sThemeDir + "/boot";
    return String("");
}

static void applyTheme() {
    gAccent = sAccent;
    if (!sLedOn || briPct <= 0) { // 0 % is a real "off" for the strip
        rgbStripOff();
        return;
    }
    rgbStripFill(sLedR * briPct / 100, sLedG * briPct / 100, sLedB * briPct / 100);
}

static void save() {
    Preferences p;
    p.begin("dbl", false);
    p.putUShort("acc", sAccent);
    p.putUChar("lr", sLedR);
    p.putUChar("lg", sLedG);
    p.putUChar("lb", sLedB);
    p.putBool("ledon", sLedOn);
    p.putInt("bripct", briPct);
    p.putBool("banim", bootAnimOn);
    p.putString("thdir", sThemeDir);
    p.end();
}

void settingsLoad() {
    Preferences p;
    p.begin("dbl", true);
    sAccent = p.getUShort("acc", 0x2FEB);
    sLedR = p.getUChar("lr", 0);
    sLedG = p.getUChar("lg", 220);
    sLedB = p.getUChar("lb", 40);
    sLedOn = p.getBool("ledon", true);
    briPct = constrain(p.getInt("bripct", 40), 0, 100);
    bootAnimOn = p.getBool("banim", true);
    sThemeDir = p.getString("thdir", "");
    p.end();
    // A theme pack may also carry its own icon set; load it before the first screen is drawn.
    if (sThemeDir.length() && sdInit() && SD.exists(sThemeDir + "/icons.bin"))
        uiLoadIconPack(sThemeDir + "/icons.bin");
    applyTheme();
}

// A big scrollable spectrum for the accent + LED pickers — 30 hues plus a grey ramp, so you
// just spin through the colours instead of picking from a tiny palette.
struct ColorOpt {
    String name;
    uint8_t r, g, b;
    uint16_t c565;
};
static uint16_t rgb565of(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static void hsv2rgb(int h, uint8_t &r, uint8_t &g, uint8_t &b) {
    const float c = 1.0f, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float rr = 0, gg = 0, bb = 0;
    if (h < 60)       { rr = c; gg = x; }
    else if (h < 120) { rr = x; gg = c; }
    else if (h < 180) { gg = c; bb = x; }
    else if (h < 240) { gg = x; bb = c; }
    else if (h < 300) { rr = x; bb = c; }
    else              { rr = c; bb = x; }
    r = (uint8_t)(rr * 255);
    g = (uint8_t)(gg * 255);
    b = (uint8_t)(bb * 255);
}
static std::vector<ColorOpt> colorWheel() {
    std::vector<ColorOpt> v;
    for (int h = 0; h < 360; h += 12) { // 30 hues
        uint8_t r, g, b;
        hsv2rgb(h, r, g, b);
        char nm[8];
        snprintf(nm, sizeof(nm), "%02X%02X%02X", r, g, b);
        v.push_back({String(nm), r, g, b, rgb565of(r, g, b)});
    }
    const uint8_t greys[] = {255, 200, 150, 100, 60};
    for (uint8_t gg : greys) {
        char nm[8];
        snprintf(nm, sizeof(nm), "%02X%02X%02X", gg, gg, gg);
        v.push_back({String(nm), gg, gg, gg, rgb565of(gg, gg, gg)});
    }
    return v;
}

// Accent picker — spin through the whole spectrum; each pill IS its colour.
static void accentMenu() {
    std::vector<ColorOpt> cols = colorWheel();
    std::vector<Tile> tiles;
    for (const ColorOpt &c : cols)
        tiles.push_back({c.name, sAccent == c.c565 ? IC_CHECK : IC_NONE, 0, c.c565});
    tiles.push_back({"Back", IC_BACK, 0, 0});
    int sel = uiCarousel(tiles, "COLOR", true);
    if (sel < 0 || sel >= (int)cols.size()) return;
    sAccent = cols[sel].c565;
    applyTheme();
    save();
}

// LED colour picker — same spectrum, plus an Off entry.
static void ledColorMenu() {
    std::vector<ColorOpt> cols = colorWheel();
    std::vector<Tile> tiles;
    for (const ColorOpt &c : cols) {
        bool active = sLedOn && sLedR == c.r && sLedG == c.g && sLedB == c.b;
        tiles.push_back({c.name, active ? IC_CHECK : IC_NONE, 0, c.c565});
    }
    tiles.push_back({"Off", sLedOn ? IC_OFF : IC_CHECK, 0, 0});
    tiles.push_back({"Back", IC_BACK, 0, 0});
    int sel = uiCarousel(tiles, "LED COLOR", true);
    if (sel < 0 || sel > (int)cols.size()) return; // Back / cancel
    if (sel == (int)cols.size()) {
        sLedOn = false;
    } else {
        sLedR = cols[sel].r;
        sLedG = cols[sel].g;
        sLedB = cols[sel].b;
        sLedOn = true;
    }
    applyTheme();
    save();
}

// Live brightness slider: turning nudges +/-5 % (applied to the strip immediately), the
// wheel click saves, BACK cancels back to the value you started with.
static void drawBrightness(int pct) {
    uiBackground();
    uiTitleBar("BRIGHTNESS");
    char b[8];
    if (pct <= 0) snprintf(b, sizeof(b), "OFF"); // 0 % turns the strip off completely
    else snprintf(b, sizeof(b), "%d%%", pct);
    uiTextCenter(b, 52, 3, gAccent);
    const int x = 30, y = 108, w = scrW() - 60, h = 16;
    gfx->fillRoundRect(x, y, w, h, h / 2, uiDim(gAccent, 22));
    if (pct > 0) gfx->fillRoundRect(x, y, w * pct / 100, h, h / 2, gAccent);
    uiTextCenter("turn +/-5%   press save   back cancel", scrH() - 12, 1, uiDim(gAccent, 85));
    uiFlush();
}

static void ledBrightnessMenu() {
    const int start = briPct;
    const bool startOn = sLedOn;
    sLedOn = true; // the slider owns the strip while it is open; 0 % is how you turn it off
    inputDrain();
    bool redraw = true;
    for (;;) {
        if (redraw) { drawBrightness(briPct); redraw = false; }
        switch (inputPoll()) {
            case EV_RIGHT: briPct = min(100, briPct + 5); applyTheme(); redraw = true; break;
            case EV_LEFT:  briPct = max(0, briPct - 5);   applyTheme(); redraw = true; break;
            case EV_PRESS: save(); return;
            case EV_BACK:
                briPct = start; // cancel restores what we started with
                sLedOn = startOn;
                applyTheme();
                return;
            default: break;
        }
        delay(12);
    }
}

// A full theme = an accent (screen) colour, an LED colour, and an optional brightness. Both
// the built-in presets and user themes loaded from the SD card use this shape.
struct ThemeDef {
    String name;
    uint16_t accent;
    uint8_t r, g, b; // LED colour
    int bri;         // -1 = keep current brightness
    String dir;      // theme-pack folder ("" = built-in preset or a plain .txt theme)
};

// Built-in presets, coordinated accent+LED pairs drawn from PAL.
static const struct {
    const char *name;
    int accent, led;
} BUILTIN[] = {
    {"Emerald", 3, 3}, {"Violet", 0, 0}, {"Ocean", 1, 1}, {"Aqua", 2, 2},
    {"Sunset", 4, 4},  {"Crimson", 5, 5}, {"Candy", 6, 6}, {"Mono", 7, 7},
};
static const int BUILTIN_N = (int)(sizeof(BUILTIN) / sizeof(BUILTIN[0]));

static uint16_t rgb888to565(uint32_t c) {
    uint8_t r = c >> 16, g = c >> 8, b = c;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// First whitespace/comment-delimited token of a value (so "2FE85A  # note" -> "2FE85A").
static String firstToken(String v) {
    v.trim();
    for (int i = 0; i < (int)v.length(); ++i)
        if (v[i] == ' ' || v[i] == '\t' || v[i] == '#') return v.substring(0, i);
    return v;
}
static uint32_t parseHex(String v) {
    v = firstToken(v);
    if (v.startsWith("#")) v = v.substring(1);
    return strtoul(v.c_str(), nullptr, 16);
}

// Parse one theme file (key = value, '#' comments). Needs at least an accent colour.
static bool parseThemeFile(const String &path, ThemeDef &t) {
    File f = SD.open(path);
    if (!f) return false;
    t.name = "";
    t.bri = -1;
    bool haveAccent = false, haveLed = false, first = true;
    uint32_t accent = 0, led = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (first) { // editors (Notepad) like to prefix a UTF-8 BOM
            first = false;
            if (line.length() >= 3 && (uint8_t)line[0] == 0xEF && (uint8_t)line[1] == 0xBB &&
                (uint8_t)line[2] == 0xBF)
                line = line.substring(3);
        }
        line.trim();
        if (line.length() == 0 || line[0] == '#') continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq);
        key.trim();
        key.toLowerCase();
        String val = line.substring(eq + 1);
        if (key == "name") {
            int h = val.indexOf('#'); // strip an inline comment, keep spaces in the name
            if (h >= 0) val = val.substring(0, h);
            val.trim();
            t.name = val;
        } else if (key == "accent") { accent = parseHex(val); haveAccent = true; }
        else if (key == "led") { led = parseHex(val); haveLed = true; }
        else if (key == "brightness") t.bri = constrain(firstToken(val).toInt(), 0, 100);
    }
    f.close();
    if (!haveAccent) return false;
    t.accent = rgb888to565(accent);
    uint32_t l = haveLed ? led : accent;
    t.r = l >> 16;
    t.g = l >> 8;
    t.b = l;
    return true;
}

// Create /themes with a self-documenting example the first time, so the feature is discoverable.
static void ensureThemesExample() {
    if (!sdInit() || SD.exists("/themes")) return;
    SD.mkdir("/themes");
    File f = SD.open("/themes/example.txt", FILE_WRITE);
    if (!f) return;
    f.print("# Custom theme for the T-Embed launcher.\n"
            "# Build complete packs (colours + icons + boot animation) with the web editor:\n"
            "#   https://loznoc.github.io/dualboot/theme.html\n"
            "# ...or just drop simple .txt files like this one in /themes and pick them under\n"
            "# Settings > Design > Theme. Colours are hex RRGGBB.\n\n"
            "name = Example\n"
            "accent = 2FE85A      # screen / UI colour\n"
            "led    = FF3CA0      # LED strip colour (optional, defaults to accent)\n"
            "brightness = 60      # optional, 0-100 (0 = LEDs off)\n");
    f.close();
}

// The basename of a path, since some SD cores return "/themes/x.txt" from name().
static String baseName(String n) {
    int sl = n.lastIndexOf('/');
    return sl >= 0 ? n.substring(sl + 1) : n;
}

// Windows Information Protection encrypts files copied to removable drives, renaming them to
// "<name>.PFILE" and dropping an "$EFS" marker beside them. The contents are genuinely
// encrypted, so nothing on the device can read them — we detect it to explain the failure.
static bool sSawEncrypted = false;
static bool looksEncrypted(const String &name) {
    String low = name;
    low.toLowerCase();
    return low.endsWith(".pfile") || low == "$efs";
}
static bool isThemeText(const String &name) {
    String low = name;
    low.toLowerCase();
    return low.endsWith(".txt") || low.endsWith(".thm");
}

// List a directory's entries, separated into files and subfolders (names only).
static void listDir(const String &path, std::vector<String> &files, std::vector<String> &dirs) {
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        const String n = baseName(f.name());
        if (n.length() == 0 || n[0] == '.') continue; // skip . / .. / dot-files
        if (looksEncrypted(n)) sSawEncrypted = true;
        if (f.isDirectory()) dirs.push_back(n);
        else files.push_back(n);
    }
    dir.close();
    std::sort(files.begin(), files.end());
    std::sort(dirs.begin(), dirs.end());
}

// Load a theme PACK from a folder: theme.txt (or any *.txt in it) plus optional icons.bin and
// boot/ frames. Unzipping on Windows often adds one extra folder level, so if the folder holds
// no theme text we look one level deeper before giving up.
static bool loadPack(const String &base, const String &folderName, ThemeDef &t, int depth = 0) {
    std::vector<String> files, dirs;
    listDir(base, files, dirs);
    String txt;
    for (const String &f : files)                      // prefer theme.txt
        if (baseName(f).equalsIgnoreCase("theme.txt")) { txt = f; break; }
    if (txt.length() == 0)
        for (const String &f : files)
            if (isThemeText(f)) { txt = f; break; }
    if (txt.length()) {
        if (!parseThemeFile(base + "/" + txt, t)) return false;
        if (t.name.length() == 0) t.name = folderName;
        t.dir = base;
        return true;
    }
    if (depth < 1)                                     // e.g. /themes/pack-theme/pack/theme.txt
        for (const String &d : dirs)
            if (loadPack(base + "/" + d, d, t, depth + 1)) return true;
    return false;
}

// Built-ins first, then the SD card's /themes: both plain "<name>.txt" files and full theme
// PACKS, i.e. a "<name>/" folder holding theme.txt plus optional icons.bin and boot/ frames.
static std::vector<ThemeDef> themesAll(String *report = nullptr) {
    std::vector<ThemeDef> v;
    for (int i = 0; i < BUILTIN_N; ++i)
        v.push_back({BUILTIN[i].name, PAL[BUILTIN[i].accent].rgb565, PAL[BUILTIN[i].led].r,
                     PAL[BUILTIN[i].led].g, PAL[BUILTIN[i].led].b, -1, ""});
    sSawEncrypted = false;
    if (!sdInit()) {
        if (report) *report = "SD not mounted";
        return v;
    }
    if (!SD.exists("/themes")) {
        if (report) *report = "no /themes folder on the card";
        return v;
    }
    std::vector<String> files, dirs;
    listDir("/themes", files, dirs);
    int okFiles = 0, badFiles = 0, okPacks = 0, badPacks = 0;
    for (const String &n : files) {
        if (!isThemeText(n)) continue;
        ThemeDef t;
        if (parseThemeFile("/themes/" + n, t)) {
            if (t.name.length() == 0) t.name = n.substring(0, n.lastIndexOf('.'));
            t.dir = "";
            v.push_back(t);
            okFiles++;
        } else badFiles++;
    }
    for (const String &d : dirs) {
        ThemeDef t;
        if (loadPack("/themes/" + d, d, t)) { v.push_back(t); okPacks++; }
        else badPacks++;
    }
    if (report) {
        char b[160];
        snprintf(b, sizeof(b), "%d file(s) ok / %d bad, %d pack(s) ok / %d bad%s", okFiles, badFiles,
                 okPacks, badPacks, sSawEncrypted ? " [Windows-encrypted .PFILE found]" : "");
        *report = String(b);
    }
    return v;
}

bool themesSawEncryptedFiles() { return sSawEncrypted; }

// Raw listing of /themes, two levels deep, so a failing pack can be diagnosed.
static String themesTree() {
    String out = "/themes tree:\n";
    if (!sdInit()) return out + "  (SD not mounted)\n";
    if (!SD.exists("/themes")) return out + "  (folder does not exist)\n";
    // First bytes of a theme file, so we can tell "renamed" from "encrypted".
    auto peek = [](const String &path) {
        File f = SD.open(path);
        if (!f) return String("  <cannot open>");
        String s = "  first bytes: ";
        for (int i = 0; i < 24 && f.available(); ++i) {
            char c = (char)f.read();
            s += (c >= 32 && c < 127) ? String(c) : String(".");
        }
        f.close();
        return s;
    };
    std::vector<String> files, dirs;
    listDir("/themes", files, dirs);
    for (const String &f : files) out += "  F /themes/" + f + "\n";
    for (const String &d : dirs) {
        out += "  D /themes/" + d + "/\n";
        std::vector<String> f2, d2;
        listDir("/themes/" + d, f2, d2);
        for (const String &f : f2) {
            out += "      F " + f + "\n";
            String low = f;
            low.toLowerCase();
            if (low.startsWith("theme.txt")) out += peek("/themes/" + d + "/" + f) + "\n";
        }
        for (const String &s : d2) {
            out += "      D " + s + "/\n";
            std::vector<String> f3, d3;
            listDir("/themes/" + d + "/" + s, f3, d3);
            for (const String &f : f3) out += "          F " + f + "\n";
            for (const String &t : d3) out += "          D " + t + "/\n";
        }
    }
    return out;
}

// Used by the serial console's "themes" command to explain what the scanner found.
String themesScanReport() {
    String rep;
    std::vector<ThemeDef> all = themesAll(&rep);
    String out = themesTree();
    out += "themes: " + rep + "\n";
    for (const ThemeDef &t : all) {
        char line[160];
        snprintf(line, sizeof(line), "  %-18s accent=%04X led=%02X%02X%02X bri=%d %s\n",
                 t.name.c_str(), t.accent, t.r, t.g, t.b, t.bri,
                 t.dir.length() ? t.dir.c_str() : "(built-in)");
        out += line;
    }
    return out;
}

static void themesMenu() {
    // Re-mount first: the card may have been written over USB drive mode or swapped since the
    // last scan, and a stale FAT cache would hide freshly added themes.
    sdRemount();
    ensureThemesExample();
    String rep;
    std::vector<ThemeDef> themes = themesAll(&rep);
    if ((int)themes.size() <= BUILTIN_N) { // nothing from the card — say why instead of staying mute
        uiBackground();
        uiTitleBar("THEME");
        if (themesSawEncryptedFiles()) {
            // The files are there but Windows encrypted them on copy (.PFILE + $EFS).
            uiTextCenter("Windows ENCRYPTED your theme", 42, 1, COL_WARN);
            uiTextCenter("The card holds .PFILE files, which", 62, 1, COL_MUTED);
            uiTextCenter("nothing can read. Copy the theme", 74, 1, COL_MUTED);
            uiTextCenter("again with Windows data protection", 86, 1, COL_MUTED);
            uiTextCenter("turned off, then delete the .PFILEs.", 98, 1, COL_MUTED);
        } else {
            uiTextCenter("No themes found on the SD card", 44, 1, COL_FG);
            uiTextCenter(rep, 62, 1, COL_MUTED);
            uiTextCenter("put them in  /themes  on the card", 84, 1, COL_MUTED);
            uiTextCenter("make one at", 100, 1, COL_MUTED);
            uiTextCenter("loznoc.github.io/dualboot/theme.html", 112, 1, gAccent);
        }
        uiTextCenter("press = built-in themes    back = exit", scrH() - 12, 1, gAccent);
        uiFlush();
        inputDrain();
        for (;;) {
            InputEvent e = inputPoll();
            if (e == EV_BACK) return;
            if (e == EV_PRESS) break; // fall through to the built-in list
            delay(30);
        }
    }
    std::vector<Tile> tiles;
    for (const ThemeDef &t : themes) {
        bool active = sAccent == t.accent && sLedOn && sLedR == t.r && sLedG == t.g &&
                      sLedB == t.b && sThemeDir == t.dir;
        tiles.push_back({t.name, active ? IC_CHECK : IC_NONE, 0, t.accent});
    }
    tiles.push_back({"Back", IC_BACK, 0, 0});
    int sel = uiCarousel(tiles, "THEME", true, serialConsolePoll);
    if (sel < 0 || sel >= (int)themes.size()) return;
    const ThemeDef &t = themes[sel];
    sAccent = t.accent;
    sLedR = t.r;
    sLedG = t.g;
    sLedB = t.b;
    sLedOn = true;
    if (t.bri >= 0) briPct = t.bri;
    // A pack can also replace the icon set; a plain theme restores the built-in icons.
    sThemeDir = t.dir;
    if (t.dir.length() && SD.exists(t.dir + "/icons.bin")) uiLoadIconPack(t.dir + "/icons.bin");
    else uiClearIconPack();
    applyTheme();
    save();
}

// Just the facts: what each frame file must contain and where to put it.
static void drawAnimTutorial() {
    uiBackground();
    uiTitleBar("BOOT FRAMES");
    gfx->setTextSize(1);
    gfx->setTextColor(COL_FG, COL_BG);
    int y = 34;
    auto line = [&](const char *s) {
        gfx->setCursor(10, y);
        gfx->print(s);
        y += 13;
    };
    line("Folder:  /boot  on the SD card.");
    line("Order:   played by file name");
    line("         (frame_000.raw, 001 ...).");
    line("Each frame = one .raw file:");
    line("  2B width + 2B height (LE), then");
    line("  width*height RGB565 pixels.");
    line("Max 320x170, ~20 fps. Drop a GIF at");
    line("loznoc.github.io/dualboot/theme.html");
    uiTextCenter("press = preview     back = exit", scrH() - 12, 1, gAccent);
    uiFlush();
}

static void animationTutorial() {
    drawAnimTutorial();
    inputDrain();
    for (;;) {
        InputEvent e = inputPoll();
        if (e == EV_BACK) return;
        if (e == EV_PRESS) {
            playBootAnimation(); // preview current (SD /boot or built-in)
            drawAnimTutorial();
            inputDrain();
        }
        delay(30);
    }
}

// Boot options: turn the startup animation on/off, preview it, or read the custom-frame info.
static void bootMenu() {
    for (;;) {
        std::vector<Tile> tiles = {
            {bootAnimOn ? "Animation: on" : "Animation: off", bootAnimOn ? IC_CHECK : IC_ANIM, 0, 0},
            {"Preview", IC_ANIM, 0, 0},
            {"Frame info", IC_ABOUT, 0, 0},
            {"Back", IC_BACK, 0, 0},
        };
        int sel = uiCarousel(tiles, "BOOT", true);
        if (sel < 0 || sel == 3) return;
        if (sel == 0) { bootAnimOn = !bootAnimOn; save(); }
        else if (sel == 1) playBootAnimation();
        else if (sel == 2) animationTutorial();
    }
}

void appearanceMenu() {
    for (;;) {
        std::vector<Tile> tiles = {
            {"Theme", IC_THEME, 0},      {"Color", IC_COLOR, 0}, {"LED Color", IC_LEDCOLOR, 0},
            {"Bright", IC_LEDBRIGHT, 0}, {"Boot", IC_ANIM, 0},   {"Back", IC_BACK, 0},
        };
        int sel = uiGrid("DESIGN", tiles, serialConsolePoll);
        if (sel < 0 || sel == 5) return;
        switch (sel) {
            case 0: themesMenu(); break;
            case 1: accentMenu(); break;
            case 2: ledColorMenu(); break;
            case 3: ledBrightnessMenu(); break;
            case 4: bootMenu(); break;
        }
    }
}

void powerOffDevice() {
    uiMessage("Powering off ...", COL_MUTED);
    rgbStripOff();
    delay(400);
    // Release the power latch (turns off on battery) and deep-sleep as a fallback when
    // USB keeps the rail alive. The side button wakes it -> non-software reset -> launcher.
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BK_BTN, 0);
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, LOW);
    esp_deep_sleep_start();
}
