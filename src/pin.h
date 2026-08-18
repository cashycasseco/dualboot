#pragma once

// Wheel PIN: the user turns the encoder into a Left/Right pattern and presses to submit.
bool pinFlow();         // boot gate — true when unlocked, freshly set, or no PIN is stored
void pinSettingsMenu(); // change / remove the PIN from inside the launcher
