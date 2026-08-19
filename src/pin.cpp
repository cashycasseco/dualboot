#include "pin.h"
#include "board.h"
#include "display.h"
#include "input.h"
#include <Preferences.h>

extern void serialConsolePoll();


// A digit PIN needs a keyboard this device does not have, so the PIN is a pattern of
// wheel turns (R/L) submitted with a press. Stored in NVS namespace "dbl", key "pin".
static constexpr size_t kMinTurns = 3;
static constexpr size_t kMaxTurns = 16;
static constexpr int kMaxAttempts = 5;

static String loadStoredPin() {
    Preferences p;
    p.begin("dbl", true);
    String pin = p.getString("pin", "");
    p.end();
    return pin;
}
static void saveStoredPin(const String &pin) {
    Preferences p;
    p.begin("dbl", false);
    p.putString("pin", pin);
    p.end();
}
static void clearStoredPin() {
    Preferences p;
    p.begin("dbl", false);
    p.remove("pin");
    p.end();
}

// One entered turn as a rounded chip: right = green arrow ▶, left = amber arrow ◀.
static void drawTurnChip(int cx, int cy, int size, char dir) {
    const uint16_t color = (dir == 'R') ? COL_OK : COL_WARN;
    const int half = size / 2;
    gfx->fillRoundRect(cx - half, cy - half, size, size, 3, color);
    const int a = size / 4;
    if (dir == 'R') gfx->fillTriangle(cx - a, cy - a, cx - a, cy + a, cx + a, cy, COL_BG);
    else gfx->fillTriangle(cx + a, cy - a, cx + a, cy + a, cx - a, cy, COL_BG);
}

// Draws the entry screen and returns the pattern on press (>= kMinTurns), or "" if the
// user backed all the way out and allowAbort is set. `footNote` (e.g. "3 tries left") sits
// just above the bottom hint so it never crowds the title.
static String captureWheelSequence(const String &title, const String &subtext, bool allowAbort,
                                   const String &footNote = "") {
    String seq = "";
    inputDrain();
    bool redraw = true;

    for (;;) {
        if (redraw) {
            uiBackground();
            uiTitleBar(title);
            if (subtext.length()) uiTextCenter(subtext, 32, 1, COL_MUTED);

            const int panelY = 48, panelH = 48;
            gfx->drawRoundRect(10, panelY, scrW() - 20, panelH, 10, uiDim(gAccent, 45));

            if (seq.isEmpty()) {
                uiTextCenter("turn the wheel   L / R", panelY + panelH / 2 - 6, 1, COL_MUTED);
            } else {
                int n = seq.length(), chip = 24, gap = 6;
                while (n * (chip + gap) - gap > scrW() - 40 && chip > 10) {
                    chip -= 2;
                    if (gap > 3) gap -= 1;
                }
                const int totalW = n * (chip + gap) - gap;
                int x = (scrW() - totalW) / 2 + chip / 2;
                const int y = panelY + panelH / 2;
                for (int i = 0; i < n; ++i) {
                    drawTurnChip(x, y, chip, seq[i]);
                    x += chip + gap;
                }
            }

            // Progress dots up to the minimum length.
            const int dotY = panelY + panelH + 12;
            int dx = scrW() / 2 - ((int)kMinTurns - 1) * 14 / 2;
            for (size_t i = 0; i < kMinTurns; ++i) {
                if (i < seq.length()) gfx->fillCircle(dx, dotY, 3, gAccent);
                else gfx->drawCircle(dx, dotY, 3, uiDim(gAccent, 45));
                dx += 14;
            }

            if (footNote.length()) uiTextCenter(footNote, scrH() - 28, 1, COL_WARN);
            if (seq.length() < kMinTurns)
                uiTextCenter(String("at least ") + kMinTurns + " turns", scrH() - 12, 1, COL_MUTED);
            else uiTextCenter("press = OK     back = undo", scrH() - 12, 1, gAccent);
            uiFlush();
            redraw = false;
        }

        switch (inputPoll()) {
            case EV_RIGHT:
                if (seq.length() < kMaxTurns) seq += "R";
                redraw = true;
                break;
            case EV_LEFT:
                if (seq.length() < kMaxTurns) seq += "L";
                redraw = true;
                break;
            case EV_BACK:
                if (seq.length() > 0) {
                    seq.remove(seq.length() - 1);
                    redraw = true;
                } else if (allowAbort) {
                    return "";
                }
                break;
            case EV_PRESS:
                if (seq.length() >= kMinTurns) return seq;
                break;
            default: break;
        }
        serialConsolePoll();
        delay(15);
    }
}

// Asks twice; returns the pattern only when both entries match.
static String askTwice(const String &title, const String &hint) {
    for (;;) {
        String first = captureWheelSequence(title, hint, true);
        if (first.isEmpty()) return "";
        String again = captureWheelSequence(title + " again", "repeat the pattern", true);
        if (again.isEmpty()) return "";
        if (first == again) return first;
        uiError("Did not match");
        delay(1200);
    }
}

// The side button is what enters the launcher and also means "undo" here, so wait for
// it to be released first — otherwise that same press dismisses the PIN prompt.
static void waitButtonsReleased(uint32_t timeoutMs = 4000) {
    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (digitalRead(BK_BTN) != BTN_ACT && digitalRead(SEL_BTN) != BTN_ACT) break;
        delay(20);
    }
    delay(150);
    inputDrain();
}

bool pinFlow() {
    waitButtonsReleased();

    String pin = loadStoredPin();
    if (pin.isEmpty()) return true; // PIN lock is off — straight into the launcher, no prompt

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        String entered = captureWheelSequence(
            "Enter PIN", "", false, String(kMaxAttempts - attempt) + " tries left");
        if (entered == pin) {
            uiMessage("Unlocked", COL_OK);
            delay(400);
            return true;
        }
        uiError("Wrong PIN");
        delay(1000);
    }
    uiError("Too many attempts");
    delay(1500);
    return false;
}

// "PIN: on/off" is the single switch: turning it on asks for a new pattern, turning it off
// asks for the current one and then clears it (no PIN stored = no prompt at boot).
void pinSettingsMenu() {
    for (;;) {
        String pin = loadStoredPin();
        const bool on = !pin.isEmpty();
        std::vector<Tile> tiles;
        tiles.push_back({on ? "PIN: on" : "PIN: off", on ? IC_CHECK : IC_PIN, 0, 0});
        if (on) tiles.push_back({"Change PIN", IC_PIN, 0, 0});
        tiles.push_back({"Back", IC_BACK, 0, 0});

        int sel = uiCarousel(tiles, "PIN", true);
        if (sel < 0 || sel == (int)tiles.size() - 1) return; // Back / cancel

        if (sel == 0) {          // toggle the lock
            if (!on) {           // off -> on: pick a pattern
                String chosen = askTwice("Set PIN", "turn a pattern, then press");
                if (!chosen.isEmpty()) {
                    saveStoredPin(chosen);
                    uiMessage("PIN on", COL_OK);
                    delay(1000);
                }
            } else { // on -> off: confirm with the current pattern first
                String cur = captureWheelSequence("Current PIN", "to turn off", true);
                if (cur.isEmpty()) continue;
                if (cur == pin) {
                    clearStoredPin();
                    uiMessage("PIN off", COL_OK);
                } else {
                    uiError("Wrong PIN");
                }
                delay(1000);
            }
        } else if (on && sel == 1) { // change
            String cur = captureWheelSequence("Current PIN", "to change", true);
            if (cur.isEmpty()) continue;
            if (cur != pin) {
                uiError("Wrong PIN");
                delay(1000);
                continue;
            }
            String chosen = askTwice("New PIN", "turn a pattern, then press");
            if (!chosen.isEmpty()) {
                saveStoredPin(chosen);
                uiMessage("PIN changed", COL_OK);
                delay(1000);
            }
        }
    }
}
