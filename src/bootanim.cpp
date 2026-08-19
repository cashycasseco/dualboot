#include "bootanim.h"
#include "bootanim_data.h"
#include "display.h"
#include "input.h"
#include "sdcard.h"
#include "settings.h"
#include <SD.h>
#include <algorithm>
#include <vector>

// ESP32-S3 ROM Deflate decoder (miniz tinfl). Flag 1 = parse the zlib header.
extern "C" size_t
tinfl_decompress_mem_to_mem(void *pOut, size_t out_len, const void *pSrc, size_t src_len, int flags);

// Nearest-neighbour scale one RGB565 frame (w x h) to the full screen (out is SW*SH) and push.
static void scaleAndDraw(const uint16_t *src, int w, int h, uint16_t *out, int SW, int SH) {
    for (int y = 0; y < SH; ++y) {
        const uint16_t *srow = src + (size_t)(y * h / SH) * w;
        uint16_t *orow = out + (size_t)y * SW;
        for (int x = 0; x < SW; ++x) orow[x] = srow[x * w / SW];
    }
    gfx->draw16bitRGBBitmap(0, 0, out, SW, SH);
    uiFlush();
}

// Custom animation from the SD card: a folder of *.raw frames, each = uint16 w, uint16 h
// (little-endian), then w*h RGB565 pixels. Frames play in file-name order. The active theme
// pack's own boot/ folder wins; otherwise the generic /boot folder is used.
static bool playSdBootAnimation(uint16_t *out, int SW, int SH) {
    if (!sdReady()) return false;
    String base = themeBootDir();
    if (base.length() == 0) base = "/boot";
    File dir = SD.open(base);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return false;
    }
    std::vector<String> frames;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) continue;
        String n = f.name();
        int sl = n.lastIndexOf('/');
        if (sl >= 0) n = n.substring(sl + 1);
        String low = n;
        low.toLowerCase();
        if (low.endsWith(".raw")) frames.push_back(n);
    }
    dir.close();
    if (frames.empty()) return false;
    std::sort(frames.begin(), frames.end());

    uint16_t *src = (uint16_t *)ps_malloc((size_t)SW * SH * sizeof(uint16_t));
    if (!src) return false;

    gfx->fillScreen(COL_BG);
    bool drew = false;
    for (const String &nm : frames) {
        if (inputPoll() != EV_NONE) break; // skippable
        File f = SD.open(base + "/" + nm);
        if (!f) continue;
        uint8_t hdr[4];
        if (f.read(hdr, 4) != 4) {
            f.close();
            continue;
        }
        const int w = hdr[0] | (hdr[1] << 8);
        const int h = hdr[2] | (hdr[3] << 8);
        if (w <= 0 || h <= 0 || w > SW || h > SH) {
            f.close();
            continue;
        }
        const size_t bytes = (size_t)w * h * 2;
        if (f.read((uint8_t *)src, bytes) != (int)bytes) {
            f.close();
            continue;
        }
        f.close();
        scaleAndDraw(src, w, h, out, SW, SH);
        drew = true;
        delay(50);
    }
    free(src);
    inputDrain();
    return drew;
}

// The animation compiled into the firmware, scaled fullscreen.
static void playEmbeddedBootAnimation(uint16_t *out, int SW, int SH) {
    uint16_t *frame = (uint16_t *)ps_malloc(kBootAnimRawFrameSize);
    if (!frame) frame = (uint16_t *)malloc(kBootAnimRawFrameSize);
    if (!frame) return;
    gfx->fillScreen(COL_BG);
    for (uint16_t f = 0; f < kBootAnimFrames; ++f) {
        if (inputPoll() != EV_NONE) break;
        const uint8_t *s = kBootAnimData + kBootAnimOffsets[f];
        const size_t sl = kBootAnimOffsets[f + 1] - kBootAnimOffsets[f];
        if (tinfl_decompress_mem_to_mem(frame, kBootAnimRawFrameSize, s, sl, 1) != kBootAnimRawFrameSize)
            continue;
        scaleAndDraw(frame, kBootAnimWidth, kBootAnimHeight, out, SW, SH);
        delay(50);
    }
    free(frame);
    inputDrain();
}

void playBootAnimation() {
    const int SW = scrW(), SH = scrH();
    uint16_t *out = (uint16_t *)ps_malloc((size_t)SW * SH * sizeof(uint16_t));
    if (!out) { // no PSRAM: draw the embedded frames centred, unscaled
        uint16_t *frame = (uint16_t *)malloc(kBootAnimRawFrameSize);
        if (!frame) return;
        const int x0 = (SW - kBootAnimWidth) / 2, y0 = (SH - kBootAnimHeight) / 2;
        gfx->fillScreen(COL_BG);
        for (uint16_t f = 0; f < kBootAnimFrames && x0 >= 0 && y0 >= 0; ++f) {
            if (inputPoll() != EV_NONE) break;
            const uint8_t *s = kBootAnimData + kBootAnimOffsets[f];
            const size_t sl = kBootAnimOffsets[f + 1] - kBootAnimOffsets[f];
            if (tinfl_decompress_mem_to_mem(frame, kBootAnimRawFrameSize, s, sl, 1) == kBootAnimRawFrameSize)
                gfx->draw16bitRGBBitmap(x0, y0, frame, kBootAnimWidth, kBootAnimHeight);
            uiFlush();
            delay(50);
        }
        free(frame);
        inputDrain();
        return;
    }

    // A custom /boot animation on the SD wins; otherwise the built-in one plays.
    if (!playSdBootAnimation(out, SW, SH)) playEmbeddedBootAnimation(out, SW, SH);
    free(out);
}
