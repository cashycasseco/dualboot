#!/usr/bin/env python3
"""Prepares the SD card for the launcher.

  python prepare_sdcard.py <drive letter>        e.g.  python prepare_sdcard.py E

It does two things:
  1. Copies every firmware image from ../firmware (bruce.bin, flipper.bin, ...) into the
     card root, written FRESH byte-for-byte. That matters on managed Windows PCs: dragging
     a downloaded .bin onto removable media wraps it as an encrypted *.PFILE the device
     cannot read, whereas a fresh local write carries no such tag.
  2. If ../sdcard.zip exists (Flipper resources), extracts it onto the card too.

After running: eject the card, insert it into the LilyGo, and use  Install from SD.
"""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(HERE, "firmware")
ZIP = os.path.join(HERE, "sdcard.zip")
if not os.path.isdir(FW):
    FW = os.path.join(HERE, "kit", "firmware")  # when run from the kit layout


def copy_fresh(src, dst):
    """Read the whole file and write it back out as a brand-new file (drops any managed
    tag that would otherwise turn it into a *.PFILE on removable media)."""
    with open(src, "rb") as f:
        data = f.read()
    with open(dst, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    drive = sys.argv[1].rstrip(":\\/")
    root = drive + ":" + os.sep
    if not os.path.isdir(root):
        sys.exit("Drive %s not found. Is the card in the PC?" % root)

    # 1. Firmware images (skip the launcher's own recovery image).
    bins = [f for f in os.listdir(FW) if f.lower().endswith(".bin") and f.lower() != "launcher.bin"] \
        if os.path.isdir(FW) else []
    if not bins:
        print("No firmware .bin files found in %s (skipping)." % FW)
    for name in bins:
        dst = os.path.join(root, name)
        print("Writing %s ..." % name)
        copy_fresh(os.path.join(FW, name), dst)
        head = open(dst, "rb").read(1)
        print("   -> %s  first byte 0x%02X %s" % (name, head[0], "(ok, ESP32 image)" if head[0] == 0xE9 else "(WARNING: not 0xE9)"))
        wrapped = os.path.join(root, name + ".PFILE")
        if os.path.exists(wrapped):
            print("   !! %s.PFILE appeared — this PC still wrapped it. Use the USB installer instead." % name)

    # 2. Flipper resources (optional).
    if os.path.exists(ZIP):
        z = zipfile.ZipFile(ZIP)
        members = [m for m in z.namelist() if not m.endswith("/")]
        print("Extracting %d Flipper resource files ..." % len(members))
        for m in members:
            target = os.path.join(root, m.replace("/", os.sep))
            d = os.path.dirname(target)
            if d and not os.path.isdir(d):
                os.makedirs(d, exist_ok=True)
            with z.open(m) as srcf, open(target, "wb") as out:
                out.write(srcf.read())
        z.close()

    print("\nDone. Eject the card in Windows, then insert it into the LilyGo (before power-on).")


if __name__ == "__main__":
    main()
