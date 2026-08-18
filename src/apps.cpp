#include "apps.h"
#include <Preferences.h>
#include <SD.h>
#include <algorithm>
#include <esp_app_format.h> // ESP_IMAGE_HEADER_MAGIC
#include <esp_ota_ops.h>

// Display names live in their own NVS namespace, keyed by the slot's partition label.
static const char *kNameNs = "dblapps";

String appStoredName(const char *label) {
    Preferences p;
    p.begin(kNameNs, true);
    String n = p.getString(label, "");
    p.end();
    return n;
}
void appSetName(const char *label, const String &name) {
    Preferences p;
    p.begin(kNameNs, false);
    p.putString(label, name);
    p.end();
}
void appClearName(const char *label) {
    Preferences p;
    p.begin(kNameNs, false);
    p.remove(label);
    p.end();
}

String appSlotDisplay(const AppSlot &s) {
    if (!s.installed) return "Empty";
    String nm = appStoredName(s.part->label);
    return nm.isEmpty() ? "Empty" : nm;
}

static bool slotHasImage(const esp_partition_t *p) {
    uint8_t magic = 0;
    return esp_partition_read(p, 0, &magic, 1) == ESP_OK && magic == ESP_IMAGE_HEADER_MAGIC;
}

std::vector<AppSlot> appAllSlots() {
    std::vector<AppSlot> slots;
    esp_partition_iterator_t it =
        esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (it) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
            p->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_15) {
            AppSlot s;
            s.part = p;
            s.index = p->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0;
            s.installed = slotHasImage(p);
            String stored = appStoredName(p->label);
            s.name = stored.isEmpty() ? String(p->label) : stored;
            slots.push_back(s);
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    // Order by OTA index so the menu is stable.
    std::sort(slots.begin(), slots.end(), [](const AppSlot &a, const AppSlot &b) {
        return a.index < b.index;
    });
    return slots;
}

std::vector<AppSlot> appsList() {
    std::vector<AppSlot> out;
    for (const AppSlot &s : appAllSlots())
        if (s.installed) out.push_back(s);
    return out;
}

bool appBoot(const esp_partition_t *part) {
    if (!part) return false;
    if (esp_ota_set_boot_partition(part) != ESP_OK) return false;
    delay(120);
    esp_restart(); // software reset -> custom bootloader boots the selected OTA slot
    return true;   // not reached
}

bool appDelete(const esp_partition_t *part) {
    if (!part) return false;
    if (esp_partition_erase_range(part, 0, part->size) != ESP_OK) return false;
    appClearName(part->label);
    return true;
}

bool appInstallFromSd(const char *path, const esp_partition_t *slot, const String &name, InstallProgress cb) {
    if (!slot) return false;
    File f = SD.open(path);
    if (!f) return false;

    const size_t total = f.size();
    if (total <= 0x10000) { // must contain a real app image past its own bootloader/table
        f.close();
        return false;
    }
    const size_t appSize = total - 0x10000;
    if (appSize > slot->size || !f.seek(0x10000)) {
        f.close();
        return false;
    }

    esp_ota_handle_t h;
    if (esp_ota_begin(slot, appSize, &h) != ESP_OK) { // erases the slot, then streams in
        f.close();
        return false;
    }

    static uint8_t buf[4096];
    size_t done = 0;
    bool ok = true;
    while (done < appSize) {
        const size_t want = min(sizeof(buf), appSize - done);
        const int n = f.read(buf, want);
        if (n <= 0) {
            ok = false;
            break;
        }
        if (esp_ota_write(h, buf, n) != ESP_OK) {
            ok = false;
            break;
        }
        done += n;
        if (cb) cb(done, appSize);
        yield();
    }
    f.close();

    if (!ok) {
        esp_ota_abort(h);
        return false;
    }
    if (esp_ota_end(h) != ESP_OK) return false; // validates the freshly written image
    appSetName(slot->label, name);
    return true;
}
