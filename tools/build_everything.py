#!/usr/bin/env python3
"""Assembles the flashable launcher image.

Run AFTER `pio run`. Stitches together:
    0x000000  custom bootloader   (bootloader/bootloader.bin) -- the launcher gate
    0x008000  partition table     (.pio/.../partitions.bin)   -- launcher + 3 app slots
    0x010000  launcher app        (.pio/.../firmware.bin)

Output:  firmware/launcher.bin   -- flash this at 0x0.

The launcher ships on its own; Bruce/Flipper/etc. are installed later from the SD card
into the OTA slots, so no app images are baked into this image.
"""
import os
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENV = "lilygo-t-embed-cc1101"
BUILD = os.path.join(HERE, ".pio", "build", ENV)
FW = os.path.join(HERE, "firmware")

BOOTLOADER = os.path.join(HERE, "bootloader", "bootloader.bin")
TABLE = os.path.join(BUILD, "partitions.bin")
APP = os.path.join(BUILD, "firmware.bin")

OFF_TABLE = 0x8000
OFF_APP = 0x10000
LAUNCHER_SLOT_END = 0x190000  # start of coredump; the launcher app must fit before here


def read(path):
    if not os.path.exists(path):
        sys.exit("Missing: %s\n(Did you run `pio run` first?)" % path)
    return open(path, "rb").read()


def main():
    boot = read(BOOTLOADER)
    table = read(TABLE)
    app = read(APP)
    if boot[0] != 0xE9:
        sys.exit("Bootloader magic missing")
    if table[0] != 0xAA or table[1] != 0x50:
        sys.exit("Partition table magic missing")
    if app[0] != 0xE9:
        sys.exit("Launcher app magic missing")
    if len(boot) > OFF_TABLE:
        sys.exit("Bootloader too big: %d > 0x8000" % len(boot))
    if OFF_APP + len(app) > LAUNCHER_SLOT_END:
        sys.exit("Launcher app too big for its 0x180000 slot")

    img = bytearray(b"\xFF" * (OFF_APP + len(app)))
    img[0 : len(boot)] = boot
    img[OFF_TABLE : OFF_TABLE + len(table)] = table
    img[OFF_APP : OFF_APP + len(app)] = app

    os.makedirs(FW, exist_ok=True)
    out = os.path.join(FW, "launcher.bin")
    open(out, "wb").write(img)

    ok = img[0] == 0xE9 and img[OFF_TABLE] == 0xAA and img[OFF_APP] == 0xE9
    print("launcher app : %d bytes (%.0f%% of its 1.5 MB slot)" % (len(app), 100.0 * len(app) / 0x180000))
    print("launcher.bin : %d bytes" % len(img))
    print("layout check :", "OK" if ok else "FAILED")
    print("wrote        :", out)


if __name__ == "__main__":
    main()
