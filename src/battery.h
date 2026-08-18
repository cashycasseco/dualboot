#pragma once
#include <Arduino.h>

// BQ27220 fuel-gauge readout for the status bar. batteryInit() probes the gauge over the
// GROVE I2C bus; batteryRead() fills percent (0-100), millivolts and a charging flag.
// Returns false if the gauge did not answer (status bar then hides the number).
void batteryInit();
bool batteryRead(int &percent, int &millivolts, bool &charging);
String batteryDebug(); // raw register dump for the `bat` serial command

