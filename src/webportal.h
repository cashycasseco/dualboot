#pragma once
#include <Arduino.h>

// Wi-Fi portal. The launcher becomes an access point and serves a small page where you can
// upload firmware .bin files and theme packs, mirror the screen, and drive the UI remotely.
//
// webPortalEnter() shows the info screen and blocks. BACK shuts the radio down; PRESS leaves the
// server RUNNING and returns to the menu, so the phone keeps mirroring while you navigate. For
// that to work the UI's idle hooks must call webPortalPoll() on every pass.
void webPortalEnter();
void webPortalPoll();      // service the web server; cheap no-op when nothing is running
bool webPortalRunning();   // true while the access point is up
void webPortalStop();      // tear down server + radio

// Bring Wi-Fi up at boot if the user enabled it in the portal ("start at boot"). Call once
// from setup(), AFTER the PIN — otherwise the remote could be used to answer the PIN screen.
void webPortalAutoStart();
bool webPortalAutoEnabled();

// Save the network to join, without going through the hotspot and the portal page.
// Used by the serial console and by a /wifi.txt dropped on the card.
void webPortalSetCredentials(const String &ssid, const String &pass, bool autoStart);
String webPortalNetworkName();
