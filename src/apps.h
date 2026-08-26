#pragma once
#include <Arduino.h>
#include <esp_partition.h>
#include <vector>

struct AppSlot {
    const esp_partition_t *part;
    String name;   // display name (stored per slot, falls back to the slot label)
    int index;     // OTA index 0..N
    bool installed; // a valid app image is present
};

// Every OTA slot in the partition table (installed or empty), ordered by index.
std::vector<AppSlot> appAllSlots();
// Only the slots that currently hold a bootable app image.
std::vector<AppSlot> appsList();

// Select the slot and issue a software restart. The custom bootloader honours the OTA
// choice on a software reset, so the device comes up in the chosen app. Does not return.
bool appBoot(const esp_partition_t *part);

// Progress callback for a running install (done/total bytes of the app image).
typedef void (*InstallProgress)(uint32_t done, uint32_t total);

// Install a firmware .bin from the SD card into the given OTA slot and remember `name`.
// Works with both layouts published in the wild: a full flash image (bootloader + partition
// table + app at 0x10000) and a bare app image. The app is located by reading the ESP32 image
// headers, so only the image itself is written — never trailing data. Does NOT change the boot
// slot. On failure `err` (optional) receives a short reason to show the user.
bool appInstallFromSd(const char *path, const esp_partition_t *slot, const String &name,
                      InstallProgress cb, String *err = nullptr);

// The label shown for a slot everywhere (home grid + delete): the stored name, or
// "Empty" when the slot has no name/app. Keeps the menus consistent.
String appSlotDisplay(const AppSlot &s);

// Per-slot display name (stored in NVS, keyed by the partition label).
String appStoredName(const char *label);
void appSetName(const char *label, const String &name);
void appClearName(const char *label);

// Per-slot icon. appSlotIcon() falls back to the default app icon when none was chosen.
int  appSlotIcon(const char *label);
void appSetIcon(const char *label, int icon);
// The icons offered in the picker, in display order.
const int *appIconChoices(int &count);

// Integrity of an installed slot.
//
// A slot is erased before it is written, so an install that dies halfway leaves a partition
// that still starts with a valid-looking image header — it boots, and then does whatever a
// truncated firmware does. The installer records a checksum over exactly the bytes it wrote;
// this re-reads the slot and compares. Costs a few hundred ms for a 4 MB image.
//
// Returns false only when a recorded checksum exists and does not match. Slots installed by
// an older launcher have none, and are reported healthy rather than blocked.
bool appSlotHealthy(const esp_partition_t *part, String *err = nullptr);

// True if this slot has a checksum on file at all.
bool appSlotHasChecksum(const esp_partition_t *part);

// Erase a slot's app image and forget its stored name (uninstall).
bool appDelete(const esp_partition_t *part);
