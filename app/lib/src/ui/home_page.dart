/// The whole app on one page.
///
/// One screen, never a push: something you leave open on the desk should not lose its place
/// every time the device reboots. The link state only changes what is *inside* the cards.
library;

import 'dart:async';

import 'package:flutter/material.dart';

import '../link/device_link.dart';
import '../link/discovery.dart';
import '../protocol/screen_painter.dart';
import '../session.dart';
import '../store.dart';
import 'design.dart';
import 'files_card.dart';

class HomePage extends StatefulWidget {
  const HomePage({super.key, required this.session});

  final RemoteSession session;

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final _host = TextEditingController();
  final _user = TextEditingController(text: 'admin');
  final _pass = TextEditingController(text: 'bruce');

  final _found = <Found>[];
  StreamSubscription<Found>? _scan;
  Store? _store;
  String _stage = '';
  bool _scanning = false;
  bool _showCreds = false;

  RemoteSession get s => widget.session;

  @override
  void initState() {
    super.initState();
    _restore();
  }

  Future<void> _restore() async {
    final store = await Store.open();
    if (!mounted) return;
    setState(() {
      _store = store;
      _host.text = store.host;
      _user.text = store.user;
      _pass.text = store.password;
    });
    s
      ..host = store.host
      ..user = store.user
      ..password = store.password
      ..onAutoChanged = store.saveAuto;
    // Auto mode is sticky: if it was on when the app was last closed, start hunting now.
    if (store.auto) s.setAuto(true);
  }

  Future<void> _remember() async =>
      _store?.saveDevice(host: s.host, user: _user.text, password: _pass.text);

  void _startScan() {
    _scan?.cancel();
    setState(() {
      _found.clear();
      _scanning = true;
      _stage = 'starting';
    });
    _scan = discover(
      lastHost: _host.text.trim().isEmpty ? null : _host.text.trim(),
      onStage: (stage) {
        if (mounted) setState(() => _stage = stage);
      },
    ).listen(
      (f) {
        if (!mounted) return;
        setState(() {
          if (!_found.any((e) => e.host == f.host)) _found.add(f);
        });
      },
      onDone: () {
        if (mounted) setState(() => _scanning = false);
      },
      onError: (_) {
        if (mounted) setState(() => _scanning = false);
      },
    );
  }

  Future<void> _connect(Found f) async {
    _scan?.cancel();
    if (mounted) setState(() => _scanning = false);
    await s.connect(f, user: _user.text.trim(), password: _pass.text);
    _host.text = s.host;
    await _remember();
  }

  Future<void> _connectManual([String? _]) async {
    final host = _host.text.trim();
    if (host.isEmpty) return;
    setState(() => _stage = 'checking $host');
    final f = await identify(host, timeout: const Duration(seconds: 3));
    if (!mounted) return;
    if (f == null) {
      setState(() => _stage = 'no answer from $host');
      return;
    }
    setState(() => _stage = '');
    await _connect(f);
  }

  @override
  void dispose() {
    _scan?.cancel();
    _host.dispose();
    _user.dispose();
    _pass.dispose();
    super.dispose();
  }

  // ---- copy that depends on the link ----------------------------------------------------

  String get _title => switch (s.state) {
    LinkState.live => s.firmware == 'Bruce' ? 'Bruce' : 'Dual-Boot Launcher',
    LinkState.connecting => 'Connecting',
    LinkState.searching => 'Looking around',
    LinkState.reconnecting => 'Hold on',
    LinkState.failed => 'Nothing there',
    LinkState.idle => 'T-Embed Remote',
  };

  String get _subtitle => switch (s.state) {
    LinkState.live => s.host,
    LinkState.reconnecting => 'waiting for ${s.host.isEmpty ? 'the device' : s.host}',
    LinkState.searching => 'scanning the network',
    LinkState.connecting => s.host,
    LinkState.failed => 'nothing answered',
    LinkState.idle => s.auto ? 'standing by' : 'not connected',
  };

  (Color, String) get _status => switch (s.state) {
    LinkState.live => (D.live, 'live'),
    LinkState.reconnecting || LinkState.searching || LinkState.connecting => (
      const Color(0xFFC79A3F),
      'waiting',
    ),
    LinkState.failed => (D.warn, 'no link'),
    LinkState.idle => (D.snowSoft, 'idle'),
  };

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: ListenableBuilder(
          listenable: s,
          builder: (context, _) {
            final live = s.isLive;
            final (statusColor, statusText) = _status;

            return ListView(
              padding: const EdgeInsets.fromLTRB(16, 8, 16, 32),
              children: [
                // ---- header ----------------------------------------------------------
                Padding(
                  padding: const EdgeInsets.fromLTRB(6, 4, 4, 16),
                  child: Row(
                    children: [
                      Text('T—EMBED', style: micro(size: 11, color: D.snow)),
                      const SizedBox(width: 10),
                      Dot(color: statusColor),
                      const SizedBox(width: 7),
                      Text(statusText.toUpperCase(), style: micro(size: 9.5)),
                      const Spacer(),
                      RoundButton(
                        icon: _scanning ? Icons.hourglass_empty : Icons.refresh,
                        onTap: _scanning ? null : _startScan,
                      ),
                    ],
                  ),
                ),

                // ---- the screen ------------------------------------------------------
                DarkCard(
                  padding: const EdgeInsets.all(12),
                  child: ClipRRect(
                    borderRadius: BorderRadius.circular(D.rInner),
                    child: _Tube(session: s),
                  ),
                ),
                const SizedBox(height: 12),

                // ---- the control card -------------------------------------------------
                CreamCard(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(_title, style: display()),
                      const SizedBox(height: 4),
                      Text(_subtitle, style: serifSub()),
                      if (live) ...[
                        const SizedBox(height: 16),
                        _Readout(session: s),
                      ],
                      const SizedBox(height: 20),
                      _Pad(session: s),
                      const SizedBox(height: 20),
                      SoftSwitch(
                        value: s.auto,
                        onChanged: s.setAuto,
                        title: 'Auto connect',
                        note: 'Keeps looking whenever nothing is connected, and picks the '
                            'device back up after a reboot.',
                      ),
                      const SizedBox(height: 18),
                      Pill(
                        label: live ? 'Disconnect' : (_scanning ? 'Scanning…' : 'Scan for devices'),
                        enabled: live || !_scanning,
                        onTap: live ? s.disconnect : _startScan,
                      ),
                      if (live && s.link is LauncherLink) ...[
                        const SizedBox(height: 16),
                        Text('STREAM QUALITY', style: micro(size: 9, color: D.inkSoft)),
                        const SizedBox(height: 8),
                        _Quality(session: s),
                      ],
                    ],
                  ),
                ),

                // ---- the card's files -------------------------------------------------
                if (live && s.link!.supportsFiles)
                  FilesCard(key: ValueKey(s.link), link: s.link!),

                // ---- found devices ----------------------------------------------------
                if (_found.isNotEmpty || _scanning) ...[
                  Label('Found', trailing: _scanning ? _stage : '${_found.length}'),
                  CreamCard(
                    padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
                    child: Column(
                      children: [
                        if (_found.isEmpty)
                          Padding(
                            padding: const EdgeInsets.fromLTRB(14, 14, 14, 16),
                            child: Row(
                              children: [
                                const SizedBox(
                                  width: 15,
                                  height: 15,
                                  child: CircularProgressIndicator(strokeWidth: 1.6, color: D.ink),
                                ),
                                const SizedBox(width: 12),
                                Expanded(child: Text('Looking for a T-Embed…', style: body())),
                              ],
                            ),
                          ),
                        for (final f in _found)
                          _DeviceRow(found: f, onTap: () => _connect(f)),
                      ],
                    ),
                  ),
                ],

                // ---- address ----------------------------------------------------------
                const Label('Address'),
                CreamCard(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      SoftField(
                        controller: _host,
                        hint: '192.168.1.42  ·  bruce.local',
                        keyboard: TextInputType.url,
                        onSubmitted: _connectManual,
                      ),
                      const SizedBox(height: 10),
                      Pill(label: 'Connect', height: 48, onTap: _connectManual),
                      if (_stage.isNotEmpty && !_scanning) ...[
                        const SizedBox(height: 10),
                        Text(_stage, style: body(size: 12)),
                      ],
                    ],
                  ),
                ),

                // ---- bruce login ------------------------------------------------------
                const Label('Bruce login', trailing: 'WebUI'),
                CreamCard(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Text(
                        'Bruce shows its user and password on screen when the WebUI starts. '
                        'The defaults are admin / bruce.',
                        style: body(),
                      ),
                      const SizedBox(height: 14),
                      if (!_showCreds)
                        GestureDetector(
                          onTap: () => setState(() => _showCreds = true),
                          child: Row(
                            children: [
                              Expanded(
                                child: Text(
                                  '${_user.text} · ${'•' * _pass.text.length}',
                                  style: TextStyle(
                                    fontSize: 14,
                                    fontWeight: FontWeight.w600,
                                    color: D.ink,
                                  ),
                                ),
                              ),
                              Text('EDIT', style: micro(size: 10, color: D.inkSoft)),
                            ],
                          ),
                        )
                      else ...[
                        SoftField(controller: _user, hint: 'user'),
                        const SizedBox(height: 9),
                        SoftField(controller: _pass, hint: 'password', obscure: true),
                        const SizedBox(height: 12),
                        Pill(
                          label: 'Save',
                          height: 46,
                          onTap: () async {
                            await _remember();
                            if (mounted) setState(() => _showCreds = false);
                          },
                        ),
                      ],
                    ],
                  ),
                ),

                // ---- setup ------------------------------------------------------------
                const Label('Setup', trailing: 'once'),
                CreamCard(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('Launcher', style: display(size: 15)),
                      const SizedBox(height: 5),
                      Text(
                        'Open WiFi from the main menu, then put your normal network into the '
                        'NETWORK panel so it joins that instead of opening a hotspot. Tick '
                        '"start wi-fi at boot" and it is there after every reset.',
                        style: body(),
                      ),
                      const SizedBox(height: 18),
                      Text('Bruce', style: display(size: 15)),
                      const SizedBox(height: 5),
                      Text(
                        'Config › System Config › Startup App › WebUI, and connect to your WiFi '
                        'once via WiFi › Connect to Wifi so it remembers it.',
                        style: body(),
                      ),
                    ],
                  ),
                ),
              ],
            );
          },
        ),
      ),
    );
  }
}

// ---------------------------------------------------------------------------------------
// the screen
// ---------------------------------------------------------------------------------------

class _Tube extends StatelessWidget {
  const _Tube({required this.session});

  final RemoteSession session;

  @override
  Widget build(BuildContext context) {
    final frame = session.frame;
    final vector = frame?.vector;
    final bitmap = frame?.bitmap;

    return AspectRatio(
      aspectRatio: vector != null ? vector.width / vector.height : 320 / 170,
      child: ColoredBox(
        color: Colors.black,
        child: vector != null
            ? CustomPaint(
                painter: ScreenPainter(
                  screen: vector,
                  images: session.link?.images ?? const {},
                ),
              )
            : bitmap != null
            ? RawImage(image: bitmap, fit: BoxFit.fill, filterQuality: FilterQuality.none)
            : _Waiting(session: session),
      ),
    );
  }
}

/// Nothing to mirror: the cat sleeps until something answers.
class _Waiting extends StatelessWidget {
  const _Waiting({required this.session});

  final RemoteSession session;

  @override
  Widget build(BuildContext context) {
    final label = switch (session.state) {
      LinkState.searching => 'looking for a device',
      LinkState.reconnecting => 'waiting for it to come back',
      LinkState.connecting => 'connecting',
      LinkState.failed => 'nothing there',
      _ => session.auto ? 'standing by' : 'auto connect is off',
    };

    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 12, 12, 10),
      child: Column(
        children: [
          Expanded(
            child: Image.asset(
              'assets/cat-sleep.gif',
              fit: BoxFit.contain,
              filterQuality: FilterQuality.none,
              alignment: Alignment.bottomCenter,
            ),
          ),
          const SizedBox(height: 6),
          Text(label.toUpperCase(), style: micro(size: 8.5, color: const Color(0xFF4A4A4A))),
        ],
      ),
    );
  }
}

class _Readout extends StatelessWidget {
  const _Readout({required this.session});

  final RemoteSession session;

  @override
  Widget build(BuildContext context) {
    final f = session.frame;
    final v = f?.vector;
    final b = f?.bitmap;
    final res = v != null
        ? '${v.width}×${v.height}'
        : b != null
        ? '${b.width}×${b.height}'
        : '—';
    final size = f == null
        ? '—'
        : f.bytes < 1024
        ? '${f.bytes} B'
        : '${(f.bytes / 1024).toStringAsFixed(1)} K';

    return Row(
      children: [
        StatChip(value: session.fps.toStringAsFixed(1), caption: 'frames / s'),
        const SizedBox(width: 8),
        StatChip(value: res, caption: 'screen'),
        const SizedBox(width: 8),
        StatChip(value: size, caption: v != null ? '${v.ops.length} ops' : 'per frame'),
      ],
    );
  }
}

class _Quality extends StatelessWidget {
  const _Quality({required this.session});

  final RemoteSession session;

  @override
  Widget build(BuildContext context) {
    const options = {1: 'Full', 2: 'Fast', 3: 'Light'};
    return Container(
      padding: const EdgeInsets.all(4),
      decoration: BoxDecoration(
        color: D.creamDeep,
        borderRadius: BorderRadius.circular(100),
      ),
      child: Row(
        children: [
          for (final e in options.entries)
            Expanded(
              child: GestureDetector(
                onTap: () => session.setScale(e.key),
                child: AnimatedContainer(
                  duration: const Duration(milliseconds: 130),
                  height: 36,
                  alignment: Alignment.center,
                  decoration: BoxDecoration(
                    color: session.scale == e.key ? D.ink : Colors.transparent,
                    borderRadius: BorderRadius.circular(100),
                  ),
                  child: Text(
                    e.value,
                    style: TextStyle(
                      fontSize: 12.5,
                      fontWeight: FontWeight.w600,
                      color: session.scale == e.key ? D.cream : D.inkSoft,
                    ),
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }
}

// ---------------------------------------------------------------------------------------
// keys
// ---------------------------------------------------------------------------------------

class _Pad extends StatelessWidget {
  const _Pad({required this.session});

  final RemoteSession session;

  @override
  Widget build(BuildContext context) {
    final live = session.isLive;
    final keys = session.keys;

    Widget key(
      RemoteKey k, {
      String? label,
      bool? arrowLeft,
      bool filled = false,
      double height = 56,
    }) => SoftKey(
      enabled: live && keys.contains(k),
      filled: filled && live,
      height: height,
      onTap: () => session.press(k),
      child: arrowLeft != null
          ? _Arrow(left: arrowLeft)
          : Text(label ?? '', style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
    );

    return Column(
      children: [
        if (keys.contains(RemoteKey.up)) ...[
          key(RemoteKey.up, label: 'Up', height: 46),
          const SizedBox(height: 8),
        ],
        Row(
          children: [
            Expanded(child: key(RemoteKey.prev, arrowLeft: true)),
            const SizedBox(width: 8),
            Expanded(flex: 2, child: key(RemoteKey.sel, label: 'OK', filled: true)),
            const SizedBox(width: 8),
            Expanded(child: key(RemoteKey.next, arrowLeft: false)),
          ],
        ),
        const SizedBox(height: 8),
        if (keys.contains(RemoteKey.down)) ...[
          key(RemoteKey.down, label: 'Down', height: 46),
          const SizedBox(height: 8),
        ],
        key(RemoteKey.esc, label: 'Back', height: 48),
      ],
    );
  }
}

/// A drawn triangle rather than an icon-font glyph, so it stays crisp at any size and takes
/// its colour from the key it sits in.
class _Arrow extends StatelessWidget {
  const _Arrow({required this.left});

  final bool left;

  @override
  Widget build(BuildContext context) => CustomPaint(
    size: const Size(14, 17),
    painter: _ArrowPainter(left: left, color: IconTheme.of(context).color ?? D.ink),
  );
}

class _ArrowPainter extends CustomPainter {
  const _ArrowPainter({required this.left, required this.color});

  final bool left;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final path = Path();
    if (left) {
      path
        ..moveTo(size.width, 0)
        ..lineTo(size.width, size.height)
        ..lineTo(0, size.height / 2);
    } else {
      path
        ..moveTo(0, 0)
        ..lineTo(0, size.height)
        ..lineTo(size.width, size.height / 2);
    }
    canvas.drawPath(
      path..close(),
      Paint()
        ..color = color
        ..strokeJoin = StrokeJoin.round,
    );
  }

  @override
  bool shouldRepaint(_ArrowPainter old) => old.left != left || old.color != color;
}

// ---------------------------------------------------------------------------------------

class _DeviceRow extends StatelessWidget {
  const _DeviceRow({required this.found, required this.onTap});

  final Found found;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final isBruce = found.kind == 'bruce';
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(D.rChip),
        child: Padding(
          padding: const EdgeInsets.fromLTRB(14, 13, 14, 13),
          child: Row(
            children: [
              Dot(color: isBruce ? const Color(0xFF6A7FA8) : D.live, size: 8),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      isBruce ? 'Bruce' : 'Dual-Boot Launcher',
                      style: TextStyle(
                        fontSize: 14.5,
                        fontWeight: FontWeight.w600,
                        letterSpacing: -0.2,
                        color: D.ink,
                      ),
                    ),
                    Text(found.host, style: serifSub(size: 12.5)),
                  ],
                ),
              ),
              Icon(Icons.arrow_forward, size: 17, color: D.inkSoft),
            ],
          ),
        ),
      ),
    );
  }
}
