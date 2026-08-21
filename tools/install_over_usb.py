"""Installs a firmware ALONGSIDE the Launcher in flash - over USB.

Usage:
    python install_over_usb.py <firmware.bin> [Name] [--port COMx]

Examples:
    python install_over_usb.py bruce.bin Bruce
    python install_over_usb.py flipper.bin Flipper

What happens:
  1. The device is reset into the LAUNCHER (our bootloader always starts the
     Launcher on an external reset).
  2. Only the app part from offset 0x10000 of the .bin is used (the Launcher
     already provides the bootloader and partition table).
  3. Over the Launcher's serial console: "flash firmware <Name> <Size>", then the
     app bytes are sent in chunks. The Launcher creates a new partition and writes
     into it.
  4. Done -> the device boots into the firmware that was just installed.

Install both = run the script twice (first bruce.bin, then flipper.bin). The reset
at the start brings the device back into the Launcher each time, no matter what is
currently running.

Afterwards: cold-boot -> Launcher with PIN; pick between the installed firmwares
from the menu.
"""

import os
import subprocess
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial fehlt. Dieses Skript mit dem PlatformIO-Python starten:\n"
             r"  C:\Users\zilen\.platformio\penv\Scripts\python.exe install_over_usb.py ...")

# Run esptool through the same Python that runs this script (pip install esptool).
ESPTOOL = [sys.executable, '-m', 'esptool']
CHUNK = 2048
APP_OFFSET = 0x10000


def find_port(explicit):
    if explicit:
        return explicit
    for p in list_ports.comports():
        # The T-Embed enumerates as an Espressif USB device (VID 303A).
        if p.vid == 0x303A or 'USB' in (p.description or ''):
            return p.device
    return None


def reset_into_launcher(port):
    # External reset via esptool -> our bootloader starts the Launcher.
    env = dict(os.environ, PYTHONIOENCODING='utf-8')
    subprocess.run(ESPTOOL + ['--chip', 'esp32s3', '--port', port, '--after', 'hard_reset', 'read_mac'],
                   capture_output=True, env=env, timeout=60)
    time.sleep(6)  # let the Launcher come up


class PortGone(Exception):
    pass


def read_lines(ser, box):
    try:
        d = ser.read_all()
    except Exception:
        # The device re-enumerates USB when it reboots after a finished install;
        # the handle dies mid-read. Callers turn this into "done" once all bytes
        # have been sent.
        raise PortGone()
    if d:
        box[0] += d
    out = []
    while b'\n' in box[0]:
        line, box[0] = box[0].split(b'\n', 1)
        s = line.decode(errors='replace').strip()
        if s:
            out.append(s)
    return out


def wait_for(ser, box, prefixes, timeout):
    end = time.time() + timeout
    while time.time() < end:
        for l in read_lines(ser, box):
            for p in prefixes:
                if l.startswith(p):
                    return l
        time.sleep(0.005)
    return None


def main():
    args = [a for a in sys.argv[1:]]
    port = None
    if '--port' in args:
        i = args.index('--port')
        port = args[i + 1]
        del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    binpath = args[0]
    name = args[1] if len(args) > 1 else os.path.splitext(os.path.basename(binpath))[0]

    if not os.path.exists(binpath):
        sys.exit(f"File missing: {binpath}")

    port = find_port(port)
    if not port:
        sys.exit("No device found. Connect the LilyGo over USB (or pass --port COMx).")
    print(f"Port: {port}")

    data = open(binpath, 'rb').read()
    if data[0] != 0xE9:
        sys.exit("Das ist kein gueltiges ESP32-Image (Magic-Byte fehlt).")
    app = data[APP_OFFSET:]
    size = len(app)
    print(f"{name}: App {size} Bytes (aus {binpath})")

    print("Resetting device into the Launcher...")
    reset_into_launcher(port)

    ser = serial.Serial(port, 115200, timeout=0)
    time.sleep(0.5)
    box = [b'']
    ser.reset_input_buffer()

    ser.write(f'flash firmware {name} {size}\r\n'.encode())
    ser.flush()
    ready = wait_for(ser, box, ['READY', 'ERR'], 20)
    if not ready or not ready.startswith('READY'):
        ser.close()
        sys.exit(f"Device not ready: {ready}")

    print("Uebertrage...")
    sent = 0
    t0 = time.time()
    last = -1
    try:
        while sent < size:
            ser.write(app[sent:sent + CHUNK])
            ser.flush()
            sent += CHUNK
            wait_for(ser, box, ['ACK', 'ERR'], 3)
            pct = int(min(sent, size) * 100 / size)
            if pct != last and pct % 10 == 0:
                print(f"  {pct:3d}%  {time.time()-t0:.0f}s")
                last = pct

        result = wait_for(ser, box, ['OK flashed', 'ERR'], 60)
    except PortGone:
        # Port vanished after we sent everything -> the device rebooted into the
        # freshly installed firmware. That is exactly the success case.
        result = 'OK flashed (reboot)'

    try:
        ser.close()
    except Exception:
        pass

    if result and result.startswith('OK') and sent >= size:
        print(f"{name} installed ({time.time()-t0:.0f}s). The device is booting into it now.")
    else:
        print(f"Install not confirmed (sent {sent}/{size}) - try again.")
        sys.exit(1)


if __name__ == '__main__':
    main()
