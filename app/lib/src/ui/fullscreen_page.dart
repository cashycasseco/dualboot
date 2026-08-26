/// The mirror, filling the phone.
///
/// The device is 320x170 — wider than it is tall — so on a portrait phone the mirror is a
/// letterbox strip about a fifth of the screen. Turning the phone sideways gives roughly
/// four times the picture, which is the difference between reading the screen and squinting
/// at it. The keys stay reachable as a thin overlay rather than a second surface to hunt for.
library;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../link/device_link.dart';
import '../protocol/screen_painter.dart';
import '../session.dart';
import 'design.dart';

class FullscreenMirror extends StatefulWidget {
  const FullscreenMirror({super.key, required this.session});

  final RemoteSession session;

  @override
  State<FullscreenMirror> createState() => _FullscreenMirrorState();
}

class _FullscreenMirrorState extends State<FullscreenMirror> {
  bool _controls = true;

  @override
  void initState() {
    super.initState();
    SystemChrome.setPreferredOrientations([
      DeviceOrientation.landscapeLeft,
      DeviceOrientation.landscapeRight,
    ]);
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersive);
  }

  @override
  void dispose() {
    // Hand the phone back the way we found it.
    SystemChrome.setPreferredOrientations(DeviceOrientation.values);
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: ListenableBuilder(
        listenable: widget.session,
        builder: (context, _) {
          final s = widget.session;
          final frame = s.frame;
          final vector = frame?.vector;
          final bitmap = frame?.bitmap;

          return Stack(
            fit: StackFit.expand,
            children: [
              GestureDetector(
                onTap: () => setState(() => _controls = !_controls),
                child: Center(
                  child: AspectRatio(
                    aspectRatio: vector != null ? vector.width / vector.height : 320 / 170,
                    child: vector != null
                        ? CustomPaint(
                            painter: ScreenPainter(
                              screen: vector,
                              images: s.link?.images ?? const {},
                            ),
                          )
                        : bitmap != null
                        ? RawImage(
                            image: bitmap,
                            fit: BoxFit.contain,
                            filterQuality: FilterQuality.none,
                          )
                        : Center(
                            child: Text(
                              s.isLive ? 'NO PICTURE' : 'WAITING FOR THE DEVICE',
                              style: micro(size: 10, color: const Color(0xFF4A4A4A)),
                            ),
                          ),
                  ),
                ),
              ),

              // Tap the picture to get the controls out of the way; tap again to bring
              // them back. Nothing is ever permanently hidden.
              AnimatedOpacity(
                opacity: _controls ? 1 : 0,
                duration: const Duration(milliseconds: 160),
                child: IgnorePointer(
                  ignoring: !_controls,
                  child: SafeArea(
                    child: Padding(
                      padding: const EdgeInsets.all(12),
                      child: Column(
                        children: [
                          Row(
                            children: [
                              _Round(
                                icon: Icons.close,
                                onTap: () => Navigator.of(context).pop(),
                              ),
                              const Spacer(),
                              Container(
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 12,
                                  vertical: 7,
                                ),
                                decoration: BoxDecoration(
                                  color: Colors.black.withValues(alpha: 0.55),
                                  borderRadius: BorderRadius.circular(100),
                                ),
                                child: Text(
                                  s.isLive
                                      ? '${s.fps.toStringAsFixed(1)} FPS'
                                      : 'RECONNECTING',
                                  style: micro(size: 9, color: D.snow),
                                ),
                              ),
                            ],
                          ),
                          const Spacer(),
                          Row(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              _Key(s, RemoteKey.prev, icon: Icons.chevron_left),
                              const SizedBox(width: 10),
                              _Key(s, RemoteKey.sel, label: 'OK'),
                              const SizedBox(width: 10),
                              _Key(s, RemoteKey.next, icon: Icons.chevron_right),
                              const SizedBox(width: 26),
                              _Key(s, RemoteKey.esc, label: 'BACK'),
                            ],
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

class _Round extends StatelessWidget {
  const _Round({required this.icon, required this.onTap});

  final IconData icon;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Material(
    color: Colors.black.withValues(alpha: 0.55),
    shape: const CircleBorder(),
    child: InkWell(
      customBorder: const CircleBorder(),
      onTap: onTap,
      child: SizedBox(width: 40, height: 40, child: Icon(icon, size: 19, color: D.snow)),
    ),
  );
}

class _Key extends StatelessWidget {
  const _Key(this.session, this.key_, {this.label, this.icon});

  final RemoteSession session;
  final RemoteKey key_;
  final String? label;
  final IconData? icon;

  @override
  Widget build(BuildContext context) {
    final on = session.isLive && session.keys.contains(key_);
    return Material(
      color: Colors.black.withValues(alpha: on ? 0.6 : 0.35),
      borderRadius: BorderRadius.circular(100),
      child: InkWell(
        borderRadius: BorderRadius.circular(100),
        onTap: on ? () => session.press(key_) : null,
        child: Container(
          height: 46,
          constraints: const BoxConstraints(minWidth: 62),
          alignment: Alignment.center,
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: icon != null
              ? Icon(icon, size: 24, color: on ? D.snow : D.snowSoft)
              : Text(
                  label ?? '',
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                    color: on ? D.snow : D.snowSoft,
                  ),
                ),
        ),
      ),
    );
  }
}
