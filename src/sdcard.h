#pragma once
#include <Arduino.h>
#include <vector>

// Mounts the micro-SD on the display's shared SPI bus (TFT_MOSI == SDCARD_MOSI on this
// board, so the SD reuses that bus with its own chip-select). Idempotent.
bool sdInit();
bool sdReady();
void sdRemount();    // drop and re-mount, so files written by the PC (USB drive mode) appear
void sdInvalidate(); // mark stale WITHOUT blocking; the next sdInit() re-mounts lazily

// File names (basename) of every *.bin in the SD-card root, sorted.
std::vector<String> sdListBins();
