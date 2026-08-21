#pragma once

// One physical wheel + two buttons. RIGHT/LEFT are wheel turns; PRESS is the wheel
// click (SEL_BTN / GPIO0); BACK is the side button (BK_BTN / GPIO6).
enum InputEvent { EV_NONE, EV_LEFT, EV_RIGHT, EV_PRESS, EV_BACK };

void inputInit();
InputEvent inputPoll(); // returns at most one queued event; non-blocking
void inputDrain();      // forget pending turns/presses (call before a new screen)

// Queue a synthetic event, as if the wheel or a button had been used. The web remote uses this;
// inputPoll() hands queued events out before it looks at the hardware.
void inputInject(InputEvent e);
