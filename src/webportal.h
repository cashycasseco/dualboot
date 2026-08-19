#pragma once

// Wi-Fi upload portal. The launcher becomes an access point and serves a small page where you
// can upload firmware .bin files and theme packs (the .zip straight from the theme creator)
// from a phone or laptop — no card reader, no cable. Blocks until BACK is pressed, then shuts
// the radio down again.
void webPortalEnter();
