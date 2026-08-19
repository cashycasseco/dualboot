#include "webportal.h"
#include "board.h"
#include "display.h"
#include "input.h"
#include "sdcard.h"
#include "settings.h"     // THEMES_DIR
#include "webpage_data.h" // kWebPageGz - see tools/make_webpage.py
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>

// ---------------------------------------------------------------------------------------------
// The launcher hosts its own access point plus a tiny web server, so firmware images and theme
// packs can be dropped onto the SD card from a phone or laptop. Uploads are streamed straight to
// the card (never buffered whole in RAM), so a 4 MB .bin is fine.
// ---------------------------------------------------------------------------------------------

static const char *AP_PASS = "dualboot"; // WPA2 needs >= 8 characters
static WebServer server(80);

static String s_ssid;
static String s_status = "Waiting for uploads";
static String s_lastFile;
static volatile uint32_t s_uploaded = 0, s_total = 0;
static bool s_dirty = true;   // screen needs a repaint
static bool s_busy = false;   // an upload is in flight
static File s_out;            // destination while streaming
static String s_outPath;
static bool s_outOk = false;

// ---- helpers ---------------------------------------------------------------------------------
static String sanitize(const String &nameIn) {
    String n = nameIn;
    int sl = n.lastIndexOf('/');
    if (sl >= 0) n = n.substring(sl + 1);
    sl = n.lastIndexOf('\\');
    if (sl >= 0) n = n.substring(sl + 1);
    String out;
    for (size_t i = 0; i < n.length(); ++i) {
        const char c = n[i];
        out += (isalnum((int)c) || c == '.' || c == '-' || c == '_' || c == ' ') ? c : '_';
    }
    return out;
}

// Create every parent folder of `path` (path is a full /a/b/c file path).
static void ensureDirs(const String &path) {
    int from = 1;
    for (;;) {
        const int sl = path.indexOf('/', from);
        if (sl < 0) break;
        const String dir = path.substring(0, sl);
        if (dir.length() && !SD.exists(dir)) SD.mkdir(dir);
        from = sl + 1;
    }
}

// ---- store-only ZIP extraction ---------------------------------------------------------------
// The theme creator writes uncompressed ("stored") zips, which are simple enough to unpack here:
// walk the local file headers and copy each entry's bytes to the card.
static bool unzipTo(const String &zipPath, const String &destRoot, String &err, int &count) {
    File z = SD.open(zipPath);
    if (!z) { err = "cannot open upload"; return false; }
    count = 0;
    uint8_t hdr[30];
    for (;;) {
        if (z.read(hdr, 30) != 30) break;
        const uint32_t sig = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) |
                             ((uint32_t)hdr[3] << 24);
        if (sig != 0x04034b50) break; // central directory reached (or not a zip)
        const uint16_t flags = hdr[6] | (hdr[7] << 8);
        const uint16_t method = hdr[8] | (hdr[9] << 8);
        const uint32_t csize = (uint32_t)hdr[18] | ((uint32_t)hdr[19] << 8) |
                               ((uint32_t)hdr[20] << 16) | ((uint32_t)hdr[21] << 24);
        const uint16_t nlen = hdr[26] | (hdr[27] << 8);
        const uint16_t elen = hdr[28] | (hdr[29] << 8);
        if (method != 0) { err = "zip is compressed - re-export it"; z.close(); return false; }
        if (flags & 0x08) { err = "zip uses streaming sizes"; z.close(); return false; }

        String name;
        for (uint16_t i = 0; i < nlen; ++i) name += (char)z.read();
        for (uint16_t i = 0; i < elen; ++i) z.read();

        name.replace('\\', '/');
        if (name.indexOf("..") >= 0 || name.startsWith("/")) { // never escape the theme folder
            err = "unsafe path in zip";
            z.close();
            return false;
        }
        if (name.endsWith("/")) { // directory entry
            const String d = destRoot + "/" + name.substring(0, name.length() - 1);
            ensureDirs(d + "/x");
            continue;
        }
        const String outPath = destRoot + "/" + name;
        ensureDirs(outPath);
        if (SD.exists(outPath)) SD.remove(outPath);
        File o = SD.open(outPath, FILE_WRITE);
        if (!o) { err = "cannot write " + outPath; z.close(); return false; }
        static uint8_t buf[2048];
        uint32_t left = csize;
        while (left) {
            const size_t want = left > sizeof(buf) ? sizeof(buf) : left;
            const int got = z.read(buf, want);
            if (got <= 0) break;
            o.write(buf, got);
            left -= got;
        }
        o.close();
        count++;
    }
    z.close();
    if (count == 0 && err.length() == 0) err = "no files inside the zip";
    return count > 0;
}

// ---- upload handling -------------------------------------------------------------------------
static void handleUploadData() {
    HTTPUpload &up = server.upload();
    const bool isTheme = server.uri() == "/theme";

    if (up.status == UPLOAD_FILE_START) {
        s_busy = true;
        s_outOk = false;
        s_uploaded = 0;
        s_total = 0;
        s_lastFile = sanitize(up.filename);
        s_status = isTheme ? "Receiving theme..." : "Receiving firmware...";
        s_dirty = true;
        if (!sdInit()) {
            s_status = "No SD card";
            return;
        }
        if (!isTheme) { // don't let anything but firmware images land in the card root
            String low = s_lastFile;
            low.toLowerCase();
            if (!low.endsWith(".bin")) {
                s_status = "Not a .bin file";
                s_dirty = true;
                return;
            }
        }
        s_outPath = isTheme ? String("/_upload.zip") : ("/" + s_lastFile);
        if (SD.exists(s_outPath)) SD.remove(s_outPath);
        s_out = SD.open(s_outPath, FILE_WRITE);
        s_outOk = (bool)s_out;
        if (!s_outOk) s_status = "Cannot write to SD";
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (s_outOk && s_out) {
            s_out.write(up.buf, up.currentSize);
            s_uploaded += up.currentSize;
            s_dirty = true;
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (s_out) s_out.close();
        s_uploaded = up.totalSize;
        s_busy = false;
        s_dirty = true;
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (s_out) s_out.close();
        if (s_outPath.length()) SD.remove(s_outPath);
        s_busy = false;
        s_status = "Upload aborted";
        s_dirty = true;
    }
}

static void finishBin() {
    if (!s_outOk) {
        server.send(500, "text/plain", s_status);
        return;
    }
    s_status = "Saved " + s_lastFile;
    s_dirty = true;
    server.send(200, "text/plain", "Saved " + s_lastFile + " to the SD card.\n"
                                   "Install it from the launcher: Install -> pick the file.");
}

static void finishTheme() {
    if (!s_outOk) {
        server.send(500, "text/plain", s_status);
        return;
    }
    if (!SD.exists(THEMES_DIR)) SD.mkdir(THEMES_DIR);
    String err;
    int n = 0;
    const bool ok = unzipTo(s_outPath, THEMES_DIR, err, n);
    SD.remove(s_outPath);
    if (!ok) {
        s_status = "Theme failed";
        s_dirty = true;
        server.send(400, "text/plain", "Could not unpack the theme: " + err);
        return;
    }
    s_status = "Theme installed";
    s_dirty = true;
    server.send(200, "text/plain", "Theme unpacked (" + String(n) + " files).\n"
                                   "Pick it on the device: Settings -> Design -> Theme.");
}


// ---- screen ------------------------------------------------------------------------------------
static void drawPortal() {
    uiBackground();
    uiTitleBar("WIFI UPLOAD");
    gfx->setTextSize(1);
    gfx->setTextColor(COL_FG, COL_BG);
    gfx->setCursor(10, 30);
    gfx->print("1. Join this Wi-Fi:");
    gfx->setTextColor(gAccent, COL_BG);
    gfx->setCursor(20, 44);
    gfx->print(s_ssid.c_str());
    gfx->setCursor(20, 56);
    gfx->print("pass: ");
    gfx->print(AP_PASS);
    gfx->setTextColor(COL_FG, COL_BG);
    gfx->setCursor(10, 72);
    gfx->print("2. Open in a browser:");
    gfx->setTextColor(gAccent, COL_BG);
    gfx->setCursor(20, 86);
    gfx->print("http://");
    gfx->print(WiFi.softAPIP().toString().c_str());

    // status / progress
    gfx->setTextColor(COL_MUTED, COL_BG);
    gfx->setCursor(10, 108);
    gfx->print(s_status.c_str());
    if (s_busy && s_uploaded) {
        char b[32];
        snprintf(b, sizeof(b), "%u KB", (unsigned)(s_uploaded / 1024));
        gfx->setCursor(10, 120);
        gfx->setTextColor(gAccent, COL_BG);
        gfx->print(b);
    }
    const int clients = WiFi.softAPgetStationNum();
    gfx->setTextColor(clients ? gAccent : COL_MUTED, COL_BG);
    gfx->setCursor(scrW() - 74, 108);
    gfx->print(clients ? "connected" : "no client");

    uiTextCenter("press BACK to stop the hotspot", scrH() - 12, 1, uiDim(gAccent, 85));
    uiFlush();
}

// ---- entry -------------------------------------------------------------------------------------
void webPortalEnter() {
    if (!sdInit()) {
        uiError("Insert an SD card first");
        delay(1800);
        return;
    }
    uint8_t mac[6];
    WiFi.softAPmacAddress(mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "T-Embed-%02X%02X", mac[4], mac[5]);
    s_ssid = ssid;
    s_status = "Waiting for uploads";
    s_uploaded = 0;
    s_busy = false;
    s_dirty = true;

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid, AP_PASS)) {
        uiError("Could not start Wi-Fi");
        delay(1800);
        WiFi.mode(WIFI_OFF);
        return;
    }

    server.on("/", HTTP_GET, []() { // stored gzipped; every browser unpacks it for us
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "text/html", (const char *)kWebPageGz, sizeof(kWebPageGz));
    });
    server.on("/bin", HTTP_POST, finishBin, handleUploadData);
    server.on("/theme", HTTP_POST, finishTheme, handleUploadData);
    server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
    server.begin();

    inputDrain();
    uint32_t lastDraw = 0;
    for (;;) {
        server.handleClient();
        if (!s_busy && inputPoll() == EV_BACK) break; // never cut an upload short
        const uint32_t now = millis();
        if (s_dirty && now - lastDraw > 250) { // throttled: redrawing starves the server
            s_dirty = false;
            lastDraw = now;
            drawPortal();
        } else if (now - lastDraw > 2000) {
            lastDraw = now;
            drawPortal();
        }
        delay(2);
    }

    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    sdRemount(); // pick up whatever was just written
    inputDrain();
}
