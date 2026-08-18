#include "sdcard.h"
#include "board.h"
#include <SD.h>
#include <SPI.h>
#include <algorithm>

static bool mounted = false;
static bool dirty = false; // set by sdInvalidate(); the next sdInit() does the SD.end()+re-mount

bool sdInit() {
    if (dirty) { // deferred re-mount, done here (main task) — never on the USB/MSC path
        SD.end();
        mounted = false;
        dirty = false;
    }
    if (mounted) return true;
    // TFT_MOSI == SDCARD_MOSI on this board, so the SD shares the display's already-
    // initialised SPI bus. The proven mount for this variant is plain SD.begin(cs) on the
    // default bus (passing an explicit SPIClass/frequency was observed to fail here).
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    for (int attempt = 0; attempt < 4 && !mounted; ++attempt) {
        if (SD.begin(SDCARD_CS)) mounted = true;
        else {
            SD.end();
            delay(30);
        }
    }
    return mounted;
}

bool sdReady() { return mounted; }

void sdRemount() {
    if (mounted) {
        SD.end();
        mounted = false;
    }
    sdInit();
}

// Mark the mount stale WITHOUT touching SPI now: calling SD.end() here (right after USB drive
// mode) can race the USB task still finishing a transfer. The next sdInit() (on the main task,
// e.g. the Install screen) performs the actual SD.end()+re-mount and picks up PC-written files.
void sdInvalidate() { dirty = true; }

std::vector<String> sdListBins() {
    std::vector<String> out;
    if (!sdReady() && !sdInit()) return out;
    File root = SD.open("/");
    if (!root) return out;
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        if (f.isDirectory()) continue;
        String name = f.name(); // some cores return a full path
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        String lower = name;
        lower.toLowerCase();
        if (lower.endsWith(".bin")) out.push_back(name);
    }
    root.close();
    std::sort(out.begin(), out.end());
    return out;
}
