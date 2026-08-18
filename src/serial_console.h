#pragma once

// Non-blocking USB-serial console. Called from the launcher's idle loops (menu + PIN
// screen). Understands:
//   flash <bruce|flipper> <size>          stream a raw app image into that OTA slot
//   flash firmware <bruce|flipper> <size> (same, tolerated for the host tool)
//   info | help
// A finished flash sets the boot slot and reboots into the app just written.
void serialConsolePoll();
