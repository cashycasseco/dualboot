#pragma once
#include <stdint.h>

// Drives the on-board WS2812B strip (8 LEDs on the T-Embed CC1101). No-ops on
// boards without HAS_RGB_LED. rgbStripFill lights every LED the same colour.
void rgbStripFill(uint8_t r, uint8_t g, uint8_t b);
void rgbStripOff();
