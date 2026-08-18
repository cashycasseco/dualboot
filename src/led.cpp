#include "led.h"
#include "board.h"
#include <Arduino.h>

#if defined(HAS_RGB_LED) && defined(RGB_LED) && defined(LED_COUNT)

// The core's rgbLedWrite() only clocks out one LED (24 bits). This board chains
// LED_COUNT of them, so we build all LED_COUNT*24 WS2812 bit symbols and stream
// them in a single RMT transaction. Timing (0.4us/0.8us at a 10 MHz tick) matches
// the framework's own WS2812 driver.
void rgbStripFill(uint8_t r, uint8_t g, uint8_t b) {
    static bool inited = false;
    if (!inited) {
        if (!rmtInit(RGB_LED, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) return;
        inited = true;
    }

    rmt_data_t data[LED_COUNT * 24];
    const uint8_t grb[3] = {g, r, b}; // WS2812B expects Green, Red, Blue

    int i = 0;
    for (int led = 0; led < LED_COUNT; ++led) {
        for (int col = 0; col < 3; ++col) {
            for (int bit = 0; bit < 8; ++bit) {
                const bool one = grb[col] & (1 << (7 - bit));
                data[i].level0 = 1;
                data[i].duration0 = one ? 8 : 4; // T?H
                data[i].level1 = 0;
                data[i].duration1 = one ? 4 : 8; // T?L
                ++i;
            }
        }
    }
    rmtWrite(RGB_LED, data, LED_COUNT * 24, RMT_WAIT_FOR_EVER);
}

void rgbStripOff() { rgbStripFill(0, 0, 0); }

#else
void rgbStripFill(uint8_t, uint8_t, uint8_t) {}
void rgbStripOff() {}
#endif
