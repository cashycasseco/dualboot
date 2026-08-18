#include "battery.h"
#include <Arduino.h>
#include <Wire.h>

// T-Embed CC1101 carries a BQ27220 fuel gauge on the GROVE I2C bus (SDA 8 / SCL 18).
// Standard-command registers (little-endian 16-bit reads):
static const uint8_t BQ_ADDR = 0x55;
static const uint8_t REG_VOLT = 0x08;    // mV
static const uint8_t REG_CURRENT = 0x0C; // mA, SIGNED: <0 discharging, ~0/+ on external power
static const uint8_t REG_SOC = 0x2C;     // state of charge, %

static bool s_present = false;

static bool readWord(uint8_t reg, uint16_t &out) {
    Wire.beginTransmission(BQ_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)BQ_ADDR, 2) != 2) return false;
    uint8_t lo = Wire.read(), hi = Wire.read();
    out = (uint16_t)lo | ((uint16_t)hi << 8);
    return true;
}

void batteryInit() {
    Wire.begin(GROVE_SDA, GROVE_SCL);
    Wire.setClock(100000);
    uint16_t v;
    s_present = readWord(REG_VOLT, v) && v > 2000 && v < 5000; // sane Li-ion range
}

bool batteryRead(int &percent, int &millivolts, bool &charging) {
    if (!s_present) return false;
    uint16_t soc, mv, cur = 0;
    if (!readWord(REG_SOC, soc) || !readWord(REG_VOLT, mv)) return false;
    percent = constrain((int)soc, 0, 100);
    millivolts = mv;
    // "Charging" = on external power. On battery the running device DRAWS from the pack, so the
    // gauge's signed current is clearly negative (~-80 mA). On USB it is ~0 (full) or positive
    // (charging). So anything not clearly negative means USB is connected. (This is more robust
    // than the DSG status flag, which sticks at "discharging" after a full-battery unplug.)
    charging = readWord(REG_CURRENT, cur) && ((int16_t)cur > -15);
    return true;
}

String batteryDebug() {
    uint16_t soc = 0, mv = 0, cur = 0, st = 0;
    readWord(REG_SOC, soc);
    readWord(REG_VOLT, mv);
    readWord(REG_CURRENT, cur);
    readWord(0x0A, st);
    char b[96];
    snprintf(b, sizeof(b), "bat present=%d soc=%d mv=%d cur=%d(signed) status=0x%04X",
             s_present, soc, mv, (int)(int16_t)cur, st);
    return String(b);
}
