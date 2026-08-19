#pragma once
#include <Arduino.h>

// Loads the saved theme (UI accent + LED colour) from NVS and applies it. Call once at
// boot, before the first screen is drawn and after the LED strip is ready.
void settingsLoad();

// Menu to pick the UI accent colour and the LED colour; both are saved immediately.
void appearanceMenu();

// Powers the device down (releases the power latch / deep sleep, wakes on the side button).
void powerOffDevice();

// Boot preference (read by setup() to decide whether to play the animation).
bool bootAnimEnabled();

// The active theme pack's boot-frame folder, or "" if it has none (then /boot is used).
String themeBootDir();

// Human-readable dump of what the /themes scanner finds (serial console "themes" command).
String themesScanReport();

// True when the last scan saw Windows-encrypted ".PFILE"/"$EFS" entries in /themes — the files
// are present but unreadable, which otherwise looks like "no themes found".
bool themesSawEncryptedFiles();
