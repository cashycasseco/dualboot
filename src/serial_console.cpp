#include "serial_console.h"
#include "apps.h"
#include "battery.h"
#include "sdcard.h"
#include "settings.h"
#include "usbdrive.h" // USBSerial
#include <SD.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include "esp32-hal-tinyusb.h" // usb_persist_restart (one-shot ROM download)

static String lineBuf;

static const esp_partition_t *slotByName(String name) {
    name.toLowerCase();
    if (name == "bruce" || name == "0")
        return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    if (name == "flipper" || name == "1")
        return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
    return nullptr;
}

// Blocking receive of `size` raw bytes into the OTA slot, chunk-and-ACK so a slow USB
// link never has more than one chunk in flight (the same scheme the host tool expects).
static void doFlash(const String &slot, uint32_t size) {
    const esp_partition_t *p = slotByName(slot);
    if (!p) {
        USBSerial.println("ERR unknown slot");
        return;
    }
    if (size == 0 || size > p->size) {
        USBSerial.println("ERR bad size");
        return;
    }
    esp_ota_handle_t h;
    if (esp_ota_begin(p, size, &h) != ESP_OK) {
        USBSerial.println("ERR ota_begin");
        return;
    }
    USBSerial.printf("READY %u\n", (unsigned)size);

    const size_t kChunk = 2048;
    static uint8_t buf[kChunk];
    uint32_t written = 0;
    unsigned long lastData = millis();
    USBSerial.setTimeout(1000);
    while (written < size) {
        const size_t want = min(kChunk, (size_t)(size - written));
        const size_t got = USBSerial.readBytes(buf, want);
        if (got == 0) {
            if (millis() - lastData > 5000) {
                USBSerial.println("ERR timeout");
                esp_ota_abort(h);
                return;
            }
            continue;
        }
        lastData = millis();
        if (esp_ota_write(h, buf, got) != ESP_OK) {
            USBSerial.println("ERR write");
            esp_ota_abort(h);
            return;
        }
        written += got;
        USBSerial.printf("ACK %u/%u\n", (unsigned)written, (unsigned)size);
        yield();
    }
    if (esp_ota_end(h) != ESP_OK) {
        USBSerial.println("ERR ota_end");
        return;
    }
    if (esp_ota_set_boot_partition(p) != ESP_OK) {
        USBSerial.println("ERR set_boot");
        return;
    }
    USBSerial.println("OK flashed, rebooting");
    USBSerial.flush();
    delay(200);
    esp_restart();
}

static void handleLine(String cmd) {
    cmd.trim();
    if (cmd.isEmpty()) return;

    if (cmd.startsWith("flash ")) {
        // Split into tokens: "flash <slot> <size>"  or  "flash firmware <slot> <size>".
        int a = cmd.indexOf(' ');
        int b = cmd.indexOf(' ', a + 1);
        String t1 = (b > 0) ? cmd.substring(a + 1, b) : "";
        String rest = (b > 0) ? cmd.substring(b + 1) : "";
        rest.trim();
        String slot = t1;
        String sizeStr = rest;
        if (t1 == "firmware") { // tolerate the older "flash firmware <name> <size>"
            int c = rest.indexOf(' ');
            if (c > 0) {
                slot = rest.substring(0, c);
                sizeStr = rest.substring(c + 1);
            }
        }
        sizeStr.trim();
        uint32_t size = (uint32_t)strtoul(sizeStr.c_str(), nullptr, 10);
        if (slot.isEmpty() || size == 0) {
            USBSerial.println("ERR usage: flash <bruce|flipper> <size>");
            return;
        }
        doFlash(slot, size);
    } else if (cmd.startsWith("name ")) {
        // name <1|2|3> <text>  — set a slot's display name
        int s1 = cmd.indexOf(' '), s2 = cmd.indexOf(' ', s1 + 1);
        if (s2 > 0) {
            int slot = cmd.substring(s1 + 1, s2).toInt();
            String nm = cmd.substring(s2 + 1);
            nm.trim();
            auto slots = appAllSlots();
            if (slot >= 1 && slot <= (int)slots.size()) {
                appSetName(slots[slot - 1].part->label, nm);
                USBSerial.printf("named slot %d = %s\n", slot, nm.c_str());
            } else USBSerial.println("ERR slot out of range");
        } else USBSerial.println("ERR usage: name <1|2|3> <text>");
    } else if (cmd == "download") {
        USBSerial.println("entering ROM download mode...");
        USBSerial.flush();
        delay(200);
        usb_persist_restart(RESTART_BOOTLOADER); // one-shot: recovers on the next flash+reset
    } else if (cmd == "bat") {
        USBSerial.println(batteryDebug());
    } else if (cmd == "themes") {
        // The CDC TX timeout is 0 (so a stalled host can never block the UI), which means a long
        // burst would be silently dropped. Feed it in small chunks and let the FIFO drain.
        const String r = themesScanReport();
        for (int i = 0; i < (int)r.length(); i += 64) {
            USBSerial.print(r.substring(i, min((int)r.length(), i + 64)));
            USBSerial.flush();
            delay(15);
        }
    } else if (cmd == "usbon") {
        usbDrivePresent(true);
        USBSerial.println("usb: media present");
    } else if (cmd == "usboff") {
        usbDrivePresent(false);
        USBSerial.println("usb: media absent");
    } else if (cmd == "help") {
        USBSerial.println("commands: flash <slot> <size> | name <1-3> <text> | info | sd | themes | bat | usbon | usboff | download | help");
    } else if (cmd == "info") {
        auto apps = appsList();
        if (apps.empty()) USBSerial.println("no apps installed");
        for (auto &app : apps) USBSerial.printf("installed: %s\n", app.name.c_str());
    } else if (cmd == "peek") {
        // Dump the first byte at a few offsets of the first .bin/.PFILE in root, to see
        // whether a .PFILE wrapper just prepends a header (0xE9 shows up shifted by 4096)
        // or encrypts the payload (0xE9 never appears).
        if (!sdInit()) {
            USBSerial.println("peek: no SD");
            return;
        }
        File root = SD.open("/");
        String target;
        for (File f = root.openNextFile(); f; f = root.openNextFile()) {
            if (f.isDirectory()) continue;
            String n = f.name();
            String low = n;
            low.toLowerCase();
            if (low.indexOf(".bin") >= 0 || low.endsWith(".pfile")) {
                target = n;
                break;
            }
        }
        root.close();
        if (target.isEmpty()) {
            USBSerial.println("peek: no candidate file");
            return;
        }
        String path = target.startsWith("/") ? target : ("/" + target);
        File f = SD.open(path);
        if (!f) {
            USBSerial.printf("peek: cannot open %s\n", path.c_str());
            return;
        }
        USBSerial.printf("peek: %s size=%u\n", target.c_str(), (unsigned)f.size());
        const uint32_t offs[] = {0, 0x1000, 0x10000, 0x11000, 0x1000 + 0x10000};
        for (uint32_t o : offs) {
            uint8_t b = 0;
            if (f.seek(o) && f.read(&b, 1) == 1) USBSerial.printf("  @0x%06X = 0x%02X\n", o, b);
            else USBSerial.printf("  @0x%06X = (past end)\n", o);
        }
        f.close();
    } else if (cmd == "sd") {
        if (!sdInit()) {
            USBSerial.println("sd: mount FAILED");
            return;
        }
        USBSerial.println("sd: mounted");
        File root = SD.open("/");
        if (!root) {
            USBSerial.println("sd: cannot open /");
            return;
        }
        int count = 0;
        for (File f = root.openNextFile(); f; f = root.openNextFile()) {
            USBSerial.printf("  %s  %-24s %u\n", f.isDirectory() ? "DIR " : "FILE", f.name(),
                          (unsigned)f.size());
            ++count;
        }
        root.close();
        USBSerial.printf("sd: %d entries in root\n", count);
        auto bins = sdListBins();
        USBSerial.printf("sd: %d .bin match\n", (int)bins.size());
        for (auto &b : bins) USBSerial.printf("  bin: %s\n", b.c_str());
    } else {
        USBSerial.println("ERR unknown (type help)");
    }
}

void serialConsolePoll() {
    while (USBSerial.available()) {
        const char c = (char)USBSerial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            String cmd = lineBuf;
            lineBuf = "";
            handleLine(cmd);
        } else if (lineBuf.length() < 80) {
            lineBuf += c;
        }
    }
}
