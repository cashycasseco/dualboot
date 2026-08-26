/// Owns the live connection: the poll loop, the stats, and — the part that matters most in
/// practice — getting back on after the device reboots.
///
/// Booting from the launcher into Bruce is a full ESP32 restart *and* a change of firmware,
/// so the session never assumes the device comes back as the same thing. It re-identifies
/// whatever answers and rebuilds the link accordingly. In auto mode the same loop also does
/// the very first connection, which is why there is only one hunt routine here rather than
/// separate "connect" and "reconnect" paths that could fight each other.
library;

import 'dart:async';

import 'package:flutter/foundation.dart';

import 'link/device_link.dart';
import 'link/discovery.dart';
import 'macros.dart';

enum LinkState {
  /// Nothing running, auto mode off.
  idle,

  /// Auto mode is looking for a device. Nothing has ever connected (or the last one is gone
  /// for good) — this is the state the sleeping cat belongs to.
  searching,

  /// Handshake in flight.
  connecting,

  /// Mirroring.
  live,

  /// Had a link, lost it, expecting it back. Almost always a reboot.
  reconnecting,

  /// Gave up. Only reachable with auto mode off.
  failed,
}

class RemoteSession extends ChangeNotifier {
  DeviceLink? _link;
  Timer? _tick;
  bool _busy = false;
  bool _pollStopped = true;
  bool _hunting = false;

  LinkState state = LinkState.idle;
  String status = 'Not connected';
  ScreenFrame? frame;
  double fps = 0;
  int _misses = 0;
  DateTime? _last;

  /// Keep trying to connect whenever nothing is connected.
  bool auto = false;

  /// Where the device was last seen. Tried first on every hunt — a reboot nearly always
  /// lands on the same DHCP lease, so this is the cheap path that usually wins.
  String host = '';
  String user = 'admin';
  String password = 'bruce';

  /// Set by the UI so a flipped auto switch survives a restart.
  Future<void> Function(bool auto)? onAutoChanged;

  /// Set by the UI so recorded macros survive a restart.
  Future<void> Function(List<Macro> macros)? onMacrosChanged;

  final List<Macro> macros = [];

  /// Keys captured since recording started, or null when not recording.
  List<RemoteKey>? _recording;

  /// The macro currently playing, and how far along it is.
  Macro? playing;
  int playedSteps = 0;

  bool get isRecording => _recording != null;
  int get recordedSteps => _recording?.length ?? 0;

  DeviceLink? get link => _link;
  String get firmware => _link?.title ?? '-';
  Set<RemoteKey> get keys => _link?.keys ?? const {};
  bool get isLive => state == LinkState.live;

  /// True while there is nothing to show — the cat's cue.
  bool get isWaiting => !isLive && frame == null;

  int get scale => _link is LauncherLink ? (_link as LauncherLink).scale : 1;

  /// Bruce's command log is small enough to ask for often; a full-resolution launcher frame
  /// is 108 KB, so it gets a little more room.
  Duration get _interval {
    if (_link is LauncherLink) return Duration(milliseconds: scale <= 1 ? 160 : 110);
    return const Duration(milliseconds: 140);
  }

  // ---- auto mode ------------------------------------------------------------------------

  void setAuto(bool on) {
    if (auto == on) return;
    auto = on;
    onAutoChanged?.call(on);
    notifyListeners();
    if (on) {
      if (!isLive && !_hunting) unawaited(_hunt(afterLive: false));
    } else if (state == LinkState.searching || state == LinkState.reconnecting) {
      // The loop notices `auto` went false and unwinds; reflect that straight away.
      _set(LinkState.idle, 'Auto connect off');
    }
  }

  // ---- connecting -----------------------------------------------------------------------

  /// Explicit connect from the UI (tapping a found device, or the manual address).
  Future<void> connect(Found found, {String? user, String? password}) async {
    _teardown();
    host = found.host;
    if (user != null) this.user = user;
    if (password != null) this.password = password;

    _set(LinkState.connecting, 'Connecting to ${found.host} …');
    if (await _open(found)) return;
    if (auto) {
      unawaited(_hunt(afterLive: false));
    } else {
      _set(LinkState.failed, status);
    }
  }

  /// Builds the link and completes the handshake. Leaves the session live on success.
  Future<bool> _open(Found found) async {
    try {
      host = found.host;
      final link = switch (found.kind) {
        'bruce' => BruceLink(found.host, user: user, password: password),
        _ => LauncherLink(found.host),
      };
      await link.open();
      _link = link;
      _pollStopped = false;
      _misses = 0;
      _last = null;
      _set(LinkState.live, '${link.title} @ $host');
      _schedule(Duration.zero);
      return true;
    } on LinkException catch (e) {
      status = e.message;
      _link?.close();
      _link = null;
      return false;
    } catch (e) {
      status = 'Could not reach ${found.host}';
      _link?.close();
      _link = null;
      return false;
    }
  }

  /// One loop for both "find me something" and "it rebooted, get it back".
  Future<void> _hunt({required bool afterLive}) async {
    if (_hunting) return;
    _hunting = true;
    _tick?.cancel();
    _link?.close();
    _link = null;
    frame = null;
    fps = 0;
    _last = null;
    _pollStopped = true;

    _set(
      afterLive ? LinkState.reconnecting : LinkState.searching,
      afterLive && host.isNotEmpty ? 'Lost $host — waiting for it to come back …' : 'Looking for a device …',
    );

    try {
      for (var attempt = 0; _keepHunting(afterLive, attempt); attempt++) {
        // The address it had. Cheap, and it is the case that actually happens.
        Found? found;
        if (host.isNotEmpty) {
          found = await identify(host, timeout: const Duration(milliseconds: 700));
        }

        // Still nothing? Widen to mDNS and the local subnet, and adopt whatever answers —
        // the router may have handed out a different address, or this may be the first
        // connection of the session.
        if (found == null && _keepHunting(afterLive, attempt) && (host.isEmpty || attempt >= 4) && attempt % 3 == 1) {
          _set(state, 'Scanning the network …');
          found = await _searchNetwork();
        }

        if (found != null && _keepHunting(afterLive, attempt)) {
          _set(LinkState.connecting, 'Found ${found.kind} at ${found.host} …');
          if (await _open(found)) return;
          _set(afterLive ? LinkState.reconnecting : LinkState.searching, status);
        }

        // Hard for the first ten seconds, then back off so this is not a battery hog.
        await Future<void>.delayed(
          attempt < 12 ? const Duration(milliseconds: 800) : const Duration(seconds: 3),
        );
      }

      if (!isLive && !auto) {
        _set(
          LinkState.failed,
          host.isEmpty
              ? 'No device found.'
              : 'No answer from $host. It may have booted into something without a remote, '
                    'or opened its own hotspot.',
        );
      }
    } finally {
      _hunting = false;
    }
  }

  bool _keepHunting(bool afterLive, int attempt) {
    if (isLive) return false;
    if (auto) return true; // auto mode never gives up; the cat keeps sleeping
    return afterLive && attempt < 40; // ~2 minutes, then stop bothering the network
  }

  Future<Found?> _searchNetwork() async {
    try {
      await for (final f in discover()) {
        return f;
      }
    } catch (_) {
      // Discovery is best effort; the plain address probe keeps running either way.
    }
    return null;
  }

  // ---- streaming ------------------------------------------------------------------------

  /// Reboot into an install slot. The link drops with it; the hunt loop brings it back as
  /// whatever firmware comes up, which is exactly the behaviour we want here.
  Future<void> bootSlot(int n) async {
    final l = _link;
    if (l == null || !l.supportsSlots) return;
    try {
      await l.bootSlot(n);
    } catch (_) {
      // The reply often never arrives because the device is already restarting.
    }
    _misses = 3;
    unawaited(_hunt(afterLive: true));
  }

  void setScale(int s) {
    final l = _link;
    if (l is LauncherLink) {
      l.scale = s.clamp(1, 4);
      notifyListeners();
    }
  }

  // ---- macros ---------------------------------------------------------------------------

  void startRecording() {
    _recording = [];
    notifyListeners();
  }

  /// Ends recording. Returns the captured keys, or null if nothing was pressed.
  List<RemoteKey>? stopRecording() {
    final steps = _recording;
    _recording = null;
    notifyListeners();
    return (steps == null || steps.isEmpty) ? null : steps;
  }

  void addMacro(Macro m) {
    macros.add(m);
    onMacrosChanged?.call(macros);
    notifyListeners();
  }

  void removeMacro(Macro m) {
    macros.remove(m);
    onMacrosChanged?.call(macros);
    notifyListeners();
  }

  /// Replays a macro. The gap between steps is what the device needs to redraw between
  /// presses — fire them back to back and it drops half of them.
  Future<void> runMacro(Macro m) async {
    if (playing != null || !isLive) return;
    playing = m;
    playedSteps = 0;
    notifyListeners();
    try {
      for (final key in m.steps) {
        if (!isLive) break;
        await press(key, record: false);
        playedSteps++;
        notifyListeners();
        await Future<void>.delayed(const Duration(milliseconds: 260));
      }
    } finally {
      playing = null;
      playedSteps = 0;
      notifyListeners();
    }
  }

  Future<void> press(RemoteKey key, {bool record = true}) async {
    final l = _link;
    if (l == null || !isLive) return;
    if (record) _recording?.add(key);
    try {
      await l.send(key);
      _schedule(const Duration(milliseconds: 40)); // feel connected to the screen
    } catch (_) {
      // A dropped keypress is not worth tearing the session down for.
    }
  }

  void _schedule(Duration d) {
    _tick?.cancel();
    if (_pollStopped) return;
    _tick = Timer(d, _poll);
  }

  Future<void> _poll() async {
    if (_busy || _pollStopped) return;
    final l = _link;
    if (l == null) return;
    _busy = true;
    try {
      final f = await l.grab();
      final v = f.vector;
      if (v != null) unawaited(l.prefetchImages(v));

      frame = f;
      final now = DateTime.now();
      if (_last != null) {
        final dt = now.difference(_last!).inMicroseconds / 1e6;
        if (dt > 0) fps = fps == 0 ? 1 / dt : fps * 0.72 + (1 / dt) * 0.28;
      }
      _last = now;
      _misses = 0;
      if (!isLive) _set(LinkState.live, '${l.title} @ $host');
      notifyListeners();
      _schedule(_interval);
    } catch (_) {
      _misses++;
      // Three misses in a row is a reboot, not a hiccup.
      if (_misses >= 3) {
        unawaited(_hunt(afterLive: true));
      } else {
        _schedule(const Duration(milliseconds: 300));
      }
    } finally {
      _busy = false;
    }
  }

  // ---- teardown -------------------------------------------------------------------------

  /// Explicit stop from the UI. Also turns auto mode off — otherwise the loop would simply
  /// reconnect and "disconnect" would do nothing.
  Future<void> disconnect() async {
    if (auto) {
      auto = false;
      onAutoChanged?.call(false);
    }
    _teardown();
    _set(LinkState.idle, 'Not connected');
  }

  void _teardown() {
    _pollStopped = true;
    _tick?.cancel();
    _tick = null;
    _link?.close();
    _link = null;
    frame = null;
    fps = 0;
    _misses = 0;
    _last = null;
  }

  void _set(LinkState s, String msg) {
    state = s;
    status = msg;
    notifyListeners();
  }

  @override
  void dispose() {
    auto = false;
    _teardown();
    super.dispose();
  }
}
