# T-Embed Remote

A Flutter app that mirrors the LilyGo T-Embed CC1101's screen and drives its wheel and side
button from your phone. One app covers the whole device: the dual-boot launcher **and stock
Bruce** — no patched Bruce build, no extra firmware on the Bruce side.

```
  phone  ──── WiFi ────►  T-Embed
                          ├── launcher   GET /fb?s=N     raw RGB565 from its PSRAM canvas
                          │              GET /key?k=…    injects a wheel/button event
                          └── Bruce      GET /getscreen  the drawing commands it issued
                                         POST /cm        cmnd=nav sel|esc|prev|next|up|down
```

## Why the two firmwares look different on the wire

The launcher renders into a full framebuffer in PSRAM, so it can hand out real pixels —
320x170 RGB565, 108 KB a frame, thinned by the `s` parameter when you want it lighter.

Bruce has no framebuffer at all; it draws straight to the panel. What it *does* keep is a
ring buffer of the drawing commands it issued — `fillRect(…)`, `drawString(…)` — which is
what its own WebUI navigator replays. A whole Bruce screen is a few hundred bytes instead of
108 KB. The app decodes that stream (`lib/src/protocol/screen_ops.dart`) and replays it on a
canvas (`screen_painter.dart`), laying text out on the same 6x8 cell grid the device uses, so
menu labels line up with the real screen instead of drifting.

`DeviceLink` hides the difference; nothing above it knows which firmware is on the other end.

## Why WiFi and not Bluetooth

BLE was the first idea and it does not survive contact with the numbers. A full launcher
frame is 108 KB; realistic BLE throughput on an ESP32-S3 is tens of kB/s, so even a quarter
-resolution mirror lands around one frame per second. WiFi on the same chip moves megabytes.

Bruce's command stream *would* fit through BLE comfortably — but then the app would only work
with one of the two firmwares, and the launcher would still need WiFi. One transport that
covers both beats two half-transports.

## Getting connected

**Launcher** — open **WiFi** in the main menu. First time, connect to the `T-Embed-XXXX`
hotspot (password `dualboot`), open `http://192.168.4.1`, and fill in the **NETWORK** panel
with your normal WiFi. From then on the launcher joins that network instead of opening a
hotspot, so your phone never has to switch networks. Tick **start wi-fi at boot** and it comes
up on its own after every reset.

**Bruce** — **Settings > Startup app > WebUI**. Bruce then joins a known network on every
boot and starts its WebUI automatically. It prints its user and password on screen; the
defaults are `admin` / `bruce`.

Both firmwares register mDNS, so `tembed.local` and `bruce.local` work as well as the IP.

## Finding the device again after a reboot

Booting from the launcher into Bruce is a full restart *and* a change of firmware. The app
handles that as a normal event rather than an error:

1. three missed frames in a row is treated as a reboot, not a hiccup,
2. it keeps knocking on the same address — a reboot nearly always lands on the same DHCP
   lease — every 800 ms for the first ten seconds, then backs off,
3. whatever answers gets **re-identified**, so a launcher link is rebuilt as a Bruce link
   without you touching anything.

In practice the screen is back a second or two after Bruce finishes booting.

This is also why the launcher sets `WiFi.setSleep(false)`: modem sleep parks the radio
between beacons and adds up to ~100 ms to every single request, which is the difference
between a mirror that feels live and one that feels laggy.

## Building it

```bash
flutter pub get
flutter build apk --release --split-per-abi --android-skip-build-dependency-validation
```

The last flag is not optional on this machine. Something in the local Flutter install leaves
`flutter.compileSdkVersion` unset, which makes Gradle give up with "Cannot query the value of
this provider because it has no value available" — so the Android versions are pinned by hand
(AGP 8.7.3 / Gradle 8.12 / Kotlin 2.1.0 / compileSdk 35) and Flutter's own version check has
to be told to stand down. The same defect is why **no plugin with Android code can be built
here at all**: a bare `flutter create` plus `file_picker` fails identically, on 3.44.1 and on
3.47.1 alike. Until that is sorted the project stays plugin-free, which is also why there is
no file picker for uploads yet.

The APK lands in `build/app/outputs/flutter-apk/app-release.apk`. It is signed with the debug
key — fine for sideloading, not for a store.

There are deliberately **no plugins with native code** in this project (`http` and
`multicast_dns` are pure Dart), which keeps the Android build off the Gradle/AGP treadmill.
The last device is remembered in a small JSON file rather than via `shared_preferences` for
the same reason — see `lib/src/store.dart`.

Android needs `usesCleartextTraffic` (the device serves plain HTTP) and the multicast
permission (mDNS); both are already in the manifest. iOS needs the local-network entries in
`Info.plist`, also already there.

## Tests

```bash
flutter test
```

`test/widget_test.dart` decodes hand-built Bruce packets, including a deliberately truncated
tail — the ring buffer really does hand out half a packet sometimes, and the decoder has to
keep the ops before it rather than throw. `test/render_test.dart` renders a synthetic screen
through the real painter and writes `build/render.png` so the renderer can be checked without
a device on the bench.

## Layout

```
lib/
  main.dart
  src/
    session.dart              poll loop, stats, reconnect-after-reboot
    store.dart                remembered device
    link/
      device_link.dart        DeviceLink + BruceLink + LauncherLink
      discovery.dart          cached host -> mDNS -> AP -> /24 sweep
    protocol/
      screen_ops.dart         Bruce's 0xAA packet format
      screen_painter.dart     replays the ops onto a canvas
    ui/
      console.dart            palette and the hard-edged widgets
      connect_page.dart
      remote_page.dart
```
