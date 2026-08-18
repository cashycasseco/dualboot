#include "cyberfont.h"

static inline const CyGlyph *glyph(const CyFont &f, char c) {
    uint8_t u = (uint8_t)c;
    if (u < f.first || u > f.last) return nullptr;
    return &f.glyphs[u - f.first];
}

int cyTextW(const CyFont &f, const char *s, int scale, int track) {
    int w = 0;
    bool first = true;
    for (; *s; ++s) {
        const CyGlyph *g = glyph(f, *s);
        if (!g) continue;
        if (!first) w += track * scale;
        w += g->adv * scale;
        first = false;
    }
    return w;
}

void cyBounds(const CyFont &f, const char *s, int scale, int &top, int &bot) {
    int t = 1000, b = -1000;
    for (; *s; ++s) {
        const CyGlyph *g = glyph(f, *s);
        if (!g || g->h == 0) continue;
        t = min(t, (int)g->yoff);
        b = max(b, (int)g->yoff + (int)g->h);
    }
    if (b < t) { t = 0; b = f.lineH; }
    top = t * scale;
    bot = b * scale;
}

void cyDrawTop(Arduino_GFX *g, const CyFont &f, int x, int yTop, const char *s,
               uint16_t color, int scale, int track) {
    int penX = x;
    bool first = true;
    for (; *s; ++s) {
        const CyGlyph *gl = glyph(f, *s);
        if (!gl) continue;
        if (!first) penX += track * scale;
        first = false;
        if (gl->w && gl->h) {
            const int stride = (gl->w + 7) / 8;
            const uint8_t *bits = f.bits + gl->off;
            const int gx = penX + gl->xoff * scale;
            const int gy = yTop + gl->yoff * scale;
            for (int row = 0; row < gl->h; ++row) {
                const uint8_t *rp = bits + row * stride;
                int col = 0;
                while (col < gl->w) {
                    // find a run of set bits for one fillRect
                    while (col < gl->w && !(rp[col >> 3] & (0x80 >> (col & 7)))) ++col;
                    if (col >= gl->w) break;
                    int start = col;
                    while (col < gl->w && (rp[col >> 3] & (0x80 >> (col & 7)))) ++col;
                    g->fillRect(gx + start * scale, gy + row * scale,
                                (col - start) * scale, scale, color);
                }
            }
        }
        penX += gl->adv * scale;
    }
}

void cyDrawCentered(Arduino_GFX *g, const CyFont &f, int x, int y, int w, int h,
                    const char *s, uint16_t color, int scale, int track) {
    int tw = cyTextW(f, s, scale, track);
    int top, bot;
    cyBounds(f, s, scale, top, bot);
    int tx = x + (w - tw) / 2;
    int yTop = y + (h - (bot - top)) / 2 - top; // so the ink block is vertically centred
    cyDrawTop(g, f, tx, yTop, s, color, scale, track);
}
