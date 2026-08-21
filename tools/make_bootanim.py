#!/usr/bin/env python3
"""Builds a custom boot animation the launcher will play from the SD card.

The launcher looks for a folder named  /boot  on the SD card containing frames
frame_000.raw, frame_001.raw, ...  (each = uint16 width, uint16 height little-endian,
then width*height RGB565 pixels). If that folder exists it plays those frames fullscreen;
otherwise it plays the built-in animation.

Usage:
    python make_bootanim.py <source> <dest> [WxH] [maxframes]

    <source>  an animated .gif, OR a folder of images (png/jpg, played in name order)
    <dest>    where to write the frames — point it straight at the card, e.g.  E:\\boot
              (writing the .raw files directly avoids the managed-file *.PFILE wrapping)
    [WxH]     frame size, default 240x128 (~screen aspect; the device scales to fullscreen)
    [maxframes] cap on frame count, default 60 (SD reads are slow — keep it short)

Example:
    python make_bootanim.py my.gif E:\\boot
Then insert the card and power-cycle: your animation plays before the PIN screen.
"""
import glob
import os
import struct
import sys

from PIL import Image, ImageSequence


def frames(src):
    if os.path.isdir(src):
        for fn in sorted(glob.glob(os.path.join(src, '*'))):
            try:
                yield Image.open(fn).convert('RGB')
            except Exception:
                pass
    else:
        im = Image.open(src)
        for fr in ImageSequence.Iterator(im):
            yield fr.convert('RGB')


def cover(im, tw, th):
    w, h = im.size
    s = max(tw / w, th / h)
    im = im.resize((max(1, round(w * s)), max(1, round(h * s))), Image.LANCZOS)
    nw, nh = im.size
    x, y = (nw - tw) // 2, (nh - th) // 2
    return im.crop((x, y, x + tw, y + th))


def rgb565(im):
    out = bytearray()
    for r, g, b in im.getdata():
        out += struct.pack('<H', ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, dest = sys.argv[1], sys.argv[2]
    tw, th = (240, 128)
    if len(sys.argv) > 3:
        tw, th = (int(v) for v in sys.argv[3].lower().split('x'))
    maxf = int(sys.argv[4]) if len(sys.argv) > 4 else 60
    if not os.path.exists(src):
        sys.exit("source not found: %s" % src)

    os.makedirs(dest, exist_ok=True)
    for old in glob.glob(os.path.join(dest, '*.raw')):
        os.remove(old)

    i = 0
    for im in frames(src):
        if i >= maxf:
            break
        data = struct.pack('<HH', tw, th) + rgb565(cover(im, tw, th))
        with open(os.path.join(dest, 'frame_%03d.raw' % i), 'wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        i += 1

    if i == 0:
        sys.exit("no frames produced (unsupported source?)")
    print("Wrote %d frames (%dx%d) to %s" % (i, tw, th, dest))
    print("Make sure they end up in a folder named  /boot  on the SD card, then power-cycle.")


if __name__ == '__main__':
    main()
