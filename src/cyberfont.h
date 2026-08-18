#pragma once
#include "cyberfont_data.h"
#include <Arduino_GFX_Library.h>

// Proportional 1-bit bitmap-font renderer for the Cyberjunkies atlases (CY_L / CY_S).
// `scale` is an integer pixel multiplier (1, 2, 3 ...) so the blocky face stays crisp.

// `track` adds inter-letter spacing (in font units, scaled) so the tight Cyberjunkies face
// never merges — 1 keeps at least a 1px gap at scale 1. Same value must be passed to width
// and draw calls for centring to stay correct.
int  cyTextW(const CyFont &f, const char *s, int scale, int track = 1);
void cyBounds(const CyFont &f, const char *s, int scale, int &top, int &bot); // ink extent vs yTop

// Draw with the text's top-left box at (x, yTop).
void cyDrawTop(Arduino_GFX *g, const CyFont &f, int x, int yTop, const char *s,
               uint16_t color, int scale, int track = 1);

// Draw centred inside the rect (x,y,w,h) both axes (uses real ink bounds).
void cyDrawCentered(Arduino_GFX *g, const CyFont &f, int x, int y, int w, int h,
                    const char *s, uint16_t color, int scale, int track = 1);
