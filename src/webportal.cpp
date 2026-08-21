#include "webportal.h"
#include "board.h"
#include "display.h"
#include "input.h"
#include "sdcard.h"
#include "settings.h"     // THEMES_DIR
#include "webpage_data.h" // kWebPageGz - see tools/make_webpage.py
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>

// ---------------------------------------------------------------------------------------------
// The launcher hosts its own access point plus a tiny web server, so firmware images and theme
// packs can be dropped onto the SD card from a phone or laptop. Uploads are streamed straight to
// the card (never buffered whole in RAM), so a 4 MB .bin is fine.
// ---------------------------------------------------------------------------------------------

// Two ways onto the device, in order of preference:
//
//   STA - it joins a network you already use. The phone never has to leave your Wi-Fi, which
//         is the only way a remote feels instant: after a reboot the launcher is back on the
//         same address within a couple of seconds and the app just finds it again.
//   AP  - its own hotspot, for when there is no network to join (or none saved yet). Always
//         available as a fallback, and the only way to enter the credentials in the first
//         place.
//
// Either way it answers to `tembed.local` over mDNS.

static const char *AP_PASS = "dualboot";  // WPA2 needs >= 8 characters
static const char *MDNS_NAME = "tembed";  // -> http://tembed.local
static WebServer server(80);
static bool s_running = false; // AP + server are up (possibly while the normal UI is on screen)

static String s_ssid;
static String s_status = "Waiting for uploads";
static String s_lastFile;
static volatile uint32_t s_uploaded = 0, s_total = 0;
static bool s_dirty = true;   // screen needs a repaint
static bool s_busy = false;   // an upload is in flight
static File s_out;            // destination while streaming
static String s_outPath;
static bool s_outOk = false;

static String s_staSsid, s_staPass; // saved home network
static bool s_autoStart = false;    // bring Wi-Fi up on every boot
static bool s_apMode = true;        // true = own hotspot, false = joined s_staSsid
static bool s_mdns = false;
static String s_ip;
static volatile bool s_netRestart = false; // set by POST /net, acted on outside the handler

static void restartNet(); // defined with the rest of the network setup, used by the poll hook

static void netLoad() {
    Preferences p;
    p.begin("dblnet", true);
    s_staSsid = p.getString("ssid", "");
    s_staPass = p.getString("pass", "");
    s_autoStart = p.getBool("auto", false);
    p.end();
}

static void netSave() {
    Preferences p;
    p.begin("dblnet", false);
    p.putString("ssid", s_staSsid);
    p.putString("pass", s_staPass);
    p.putBool("auto", s_autoStart);
    p.end();
}

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


// ---- screen mirror + remote --------------------------------------------------------------------
// The framebuffer is sent as raw RGB565 in screen order, streamed row by row so nothing large is
// ever buffered. `s` thins it (s=2 -> quarter the bytes), which is what makes it feel live over
// the device's own access point.
static void handleFrame() {
    const int s = constrain(server.hasArg("s") ? server.arg("s").toInt() : 2, 1, 4);
    const int w = scrW() / s, h = scrH() / s;
    static uint16_t row[320];
    if (uiScreenRow(0, s, row, (int)(sizeof(row) / sizeof(row[0]))) == 0) {
        server.send(503, "text/plain", "no framebuffer");
        return;
    }
    server.sendHeader("X-Width", String(w));
    server.sendHeader("X-Height", String(h));
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength((size_t)w * h * 2);
    server.send(200, "application/octet-stream", "");
    WiFiClient c = server.client();
    for (int y = 0; y < h; ++y) {
        const int n = uiScreenRow(y * s, s, row, w);
        if (n <= 0) break;
        c.write((const uint8_t *)row, (size_t)n * 2);
    }
}

// Remote control: the same four events the wheel and buttons produce.
static void handleKey() {
    const String k = server.arg("k");
    if (k == "left") inputInject(EV_LEFT);
    else if (k == "right") inputInject(EV_RIGHT);
    else if (k == "ok") inputInject(EV_PRESS);
    else if (k == "back") inputInject(EV_BACK);
    else {
        server.send(400, "text/plain", "unknown key");
        return;
    }
    server.send(200, "text/plain", "ok");
}

// ---- screen ------------------------------------------------------------------------------------
static void drawPortal() {
    uiBackground();
    uiTitleBar(s_apMode ? "WIFI HOTSPOT" : "WIFI");
    gfx->setTextSize(1);

    if (s_apMode) {
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
    } else {
        gfx->setTextColor(COL_FG, COL_BG);
        gfx->setCursor(10, 30);
        gfx->print("Joined your network:");
        gfx->setTextColor(gAccent, COL_BG);
        gfx->setCursor(20, 44);
        gfx->print(s_ssid.c_str());
        gfx->setTextColor(COL_MUTED, COL_BG);
        gfx->setCursor(20, 58);
        gfx->print("Your phone can stay on it.");
        gfx->setTextColor(COL_FG, COL_BG);
        gfx->setCursor(10, 72);
        gfx->print("Reach it at:");
    }
    gfx->setTextColor(gAccent, COL_BG);
    gfx->setCursor(20, 86);
    gfx->print("http://");
    gfx->print(s_ip.c_str());
    if (s_mdns) {
        gfx->setTextColor(COL_MUTED, COL_BG);
        gfx->print("  /  ");
        gfx->print(MDNS_NAME);
        gfx->print(".local");
    }

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
    if (s_apMode) {
        const int clients = WiFi.softAPgetStationNum();
        gfx->setTextColor(clients ? gAccent : COL_MUTED, COL_BG);
        gfx->setCursor(scrW() - 74, 108);
        gfx->print(clients ? "connected" : "no client");
    } else {
        gfx->setTextColor(WiFi.status() == WL_CONNECTED ? gAccent : COL_ERR, COL_BG);
        gfx->setCursor(scrW() - 74, 108);
        gfx->print(WiFi.status() == WL_CONNECTED ? "online" : "dropped");
    }

    uiTextCenter("back = menu   (wi-fi stays on)", scrH() - 24, 1, uiDim(gAccent, 85));
    uiTextCenter("press = turn wi-fi off", scrH() - 12, 1, COL_MUTED);
    uiFlush();
}

// ---- lifecycle ----------------------------------------------------------------------------------
bool webPortalRunning() { return s_running; }

// Called from the UI's idle hooks. Kept trivial so it can sit in every menu loop.
void webPortalPoll() {
    if (!s_running) return;
    server.handleClient();
    if (s_netRestart) restartNet();
}

void webPortalStop() {
    if (!s_running) return;
    server.stop();
    if (s_mdns) {
        MDNS.end();
        s_mdns = false;
    }
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    s_running = false;
    sdRemount(); // pick up whatever was uploaded
}

// The same four events again, under the name Bruce's WebUI uses. Costs almost nothing and
// means one remote client can drive the launcher and Bruce through the same call.
static void handleCm() {
    const String c = server.arg("cmnd");
    if (!c.startsWith("nav")) {
        server.send(400, "text/plain", "unknown command");
        return;
    }
    if (c.indexOf("prev") > 0 || c.indexOf("up") > 0) inputInject(EV_LEFT);
    else if (c.indexOf("next") > 0 || c.indexOf("down") > 0) inputInject(EV_RIGHT);
    else if (c.indexOf("sel") > 0) inputInject(EV_PRESS);
    else if (c.indexOf("esc") > 0) inputInject(EV_BACK);
    else {
        server.send(400, "text/plain", "unknown nav");
        return;
    }
    server.send(200, "text/plain", "ok");
}

static void handleNetGet() {
    String j = "{\"mode\":\"";
    j += s_apMode ? "ap" : "sta";
    j += "\",\"ssid\":\"" + s_ssid + "\",\"ip\":\"" + s_ip + "\",\"home\":\"" + s_staSsid +
         "\",\"auto\":" + (s_autoStart ? "true" : "false") + "}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", j);
}

// Saving credentials cannot reconnect inline: the reply has to go out over the very radio we
// are about to drop. Flag it and let the portal loop do the switch.
static void handleNetPost() {
    if (server.hasArg("ssid")) s_staSsid = server.arg("ssid");
    if (server.hasArg("pass")) s_staPass = server.arg("pass");
    if (server.hasArg("auto")) s_autoStart = server.arg("auto") == "1";
    netSave();
    const bool reconnect = server.hasArg("apply") && s_staSsid.length();
    server.send(200, "text/plain",
                reconnect ? "Saved. Joining " + s_staSsid + " - reconnect to that network."
                          : "Saved.");
    if (reconnect) s_netRestart = true;
}

static void installRoutes() {
    static bool done = false;
    if (done) return;
    done = true;
    server.on("/", HTTP_GET, []() { // stored gzipped; every browser unpacks it for us
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "text/html", (const char *)kWebPageGz, sizeof(kWebPageGz));
    });
    server.on("/fb", HTTP_GET, handleFrame);
    server.on("/key", HTTP_GET, handleKey);
    server.on("/cm", HTTP_POST, handleCm);
    server.on("/cm", HTTP_GET, handleCm);
    server.on("/net", HTTP_GET, handleNetGet);
    server.on("/net", HTTP_POST, handleNetPost);
    server.on("/bin", HTTP_POST, finishBin, handleUploadData);
    server.on("/theme", HTTP_POST, finishTheme, handleUploadData);
    server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
}

static void startMdns() {
    if (s_mdns) return;
    if (MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        s_mdns = true;
    }
}

// Join the saved network. Reports progress on screen because nine seconds of a blank panel
// looks like a hang.
static bool startSta() {
    if (!s_staSsid.length()) return false;
    WiFi.mode(WIFI_STA);
    // Modem sleep parks the radio between beacons and adds up to ~100 ms to every request.
    // For a screen mirror that is the difference between "live" and "laggy".
    WiFi.setSleep(false);
    WiFi.begin(s_staSsid.c_str(), s_staPass.c_str());

    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 9000) {
        uiMessage("Joining " + s_staSsid + " ...", COL_MUTED);
        delay(250);
    }
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(true);
        return false;
    }
    s_apMode = false;
    s_ssid = s_staSsid;
    s_ip = WiFi.localIP().toString();
    return true;
}

static bool startApOnly() {
    uint8_t mac[6];
    WiFi.softAPmacAddress(mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "T-Embed-%02X%02X", mac[4], mac[5]);

    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    if (!WiFi.softAP(ssid, AP_PASS)) {
        WiFi.mode(WIFI_OFF);
        return false;
    }
    s_apMode = true;
    s_ssid = ssid;
    s_ip = WiFi.softAPIP().toString();
    return true;
}

static bool startNet() {
    if (s_running) return true;
    netLoad();
    s_status = "Waiting";
    s_uploaded = 0;
    s_busy = false;
    s_dirty = true;

    if (!startSta() && !startApOnly()) return false;
    installRoutes();
    startMdns();
    server.begin();
    s_running = true;
    return true;
}

// Drop the radio and bring it back up with whatever is saved now.
static void restartNet() {
    s_netRestart = false;
    if (s_running) {
        server.stop();
        if (s_mdns) { MDNS.end(); s_mdns = false; }
        WiFi.softAPdisconnect(true);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        s_running = false;
    }
    delay(200);
    startNet();
}

// Called once at boot. Only does anything if the user ticked "start at boot" in the portal —
// then the launcher is reachable the moment it comes up, which is what makes hopping between
// firmwares survivable from the phone.
void webPortalAutoStart() {
    netLoad();
    if (!s_autoStart) return;
    startNet();
}

bool webPortalAutoEnabled() {
    netLoad();
    return s_autoStart;
}

// ---- entry -------------------------------------------------------------------------------------
void webPortalEnter() {
    if (!sdInit()) {
        uiError("Insert an SD card first");
        delay(1800);
        return;
    }
    if (!startNet()) {
        uiError("Could not start Wi-Fi");
        delay(1800);
        return;
    }

    inputDrain();
    uint32_t lastDraw = 0;
    for (;;) {
        server.handleClient();
        if (s_netRestart) {
            restartNet();
            s_dirty = true;
        }
        if (!s_busy) {                        // never react while an upload is in flight
            const InputEvent e = inputPoll();
            // BACK is what everyone presses to leave a screen, so it must NOT tear the hotspot
            // down — it just hands the screen back while the server keeps serving. Stopping is
            // the deliberate action: come back here and press.
            if (e == EV_BACK) {
                inputDrain();
                return;
            }
            if (e == EV_PRESS) {
                webPortalStop();
                break;
            }
        }
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
    uiMessage("Hotspot off", COL_MUTED);
    delay(700);
    inputDrain();
}
