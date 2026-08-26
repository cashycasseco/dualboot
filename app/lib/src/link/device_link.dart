/// One client, two firmwares.
///
/// The launcher and Bruce expose the screen in completely different shapes — the launcher
/// has a real PSRAM framebuffer and hands out raw RGB565, Bruce has no framebuffer at all
/// and hands out the drawing commands it issued. Both are wrapped here behind [DeviceLink]
/// so the UI never has to care which one it is talking to.
library;

import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:http/http.dart' as http;

import '../protocol/file_list.dart';
import '../protocol/screen_ops.dart';

/// The four things the hardware can do, plus the two extra directions Bruce accepts.
enum RemoteKey { prev, next, sel, esc, up, down }

/// A grabbed screen: vector ops from Bruce, or a decoded bitmap from the launcher.
class ScreenFrame {
  ScreenFrame.vector(this.vector, this.bytes) : bitmap = null;
  ScreenFrame.bitmap(this.bitmap, this.bytes) : vector = null;

  final VectorScreen? vector;
  final ui.Image? bitmap;

  /// Wire size of this frame, for the stats line.
  final int bytes;
}

class LinkException implements Exception {
  LinkException(this.message);
  final String message;
  @override
  String toString() => message;
}

abstract class DeviceLink {
  DeviceLink(this.host) : client = http.Client();

  final String host;
  final http.Client client;

  /// Shown in the UI, and persisted so a reconnect skips detection.
  String get kind;
  String get title;

  Uri uri(String path, [Map<String, String>? q]) =>
      Uri.http(host, path, q);

  /// Handshake. Throws [LinkException] with something a human can act on.
  Future<void> open();

  Future<ScreenFrame> grab();

  Future<void> send(RemoteKey key);

  /// Which keys this firmware actually understands.
  Set<RemoteKey> get keys =>
      {RemoteKey.prev, RemoteKey.next, RemoteKey.sel, RemoteKey.esc};

  /// Device file path -> image, filled in lazily for Bruce's DRAWIMAGE opcode.
  Map<String, ui.Image> get images => const {};
  Future<void> prefetchImages(VectorScreen screen) async {}

  // ---- files ---------------------------------------------------------------------------
  // Both firmwares answer the same four calls with the same shapes, so this lives here once
  // rather than twice. Only the auth header and the filesystem name differ.

  /// Whether this firmware serves the card at all.
  bool get supportsFiles => true;

  /// Bruce can also browse its internal flash; the launcher only has the card.
  String get fsName => 'SD';

  /// Sent with every request. Bruce adds its session cookie here.
  Map<String, String> get authHeaders => const {};

  Future<FileListing> listFiles(String folder) async {
    final res = await client
        .get(uri('/listfiles', {'fs': fsName, 'folder': folder}), headers: authHeaders)
        .timeout(const Duration(seconds: 15));
    if (res.statusCode != 200) {
      throw LinkException('Could not list $folder (${res.statusCode})');
    }
    return parseListing(res.body, folder);
  }

  Future<void> deleteEntry(String path) => _fileAction(path, 'delete');

  Future<void> makeFolder(String path) => _fileAction(path, 'create');

  Future<void> _fileAction(String path, String action) async {
    final res = await client
        .get(
          uri('/file', {'fs': fsName, 'name': path, 'action': action}),
          headers: authHeaders,
        )
        .timeout(const Duration(seconds: 15));
    if (res.statusCode != 200) {
      throw LinkException(res.body.isEmpty ? '$action failed' : res.body.trim());
    }
  }

  Future<Uint8List> download(String path) async {
    final res = await client
        .get(
          uri('/file', {'fs': fsName, 'name': path, 'action': 'download'}),
          headers: authHeaders,
        )
        .timeout(const Duration(minutes: 2));
    if (res.statusCode != 200) throw LinkException('Download failed (${res.statusCode})');
    return res.bodyBytes;
  }

  /// Streams a file onto the card.
  ///
  /// Bruce takes the destination from the folder of the *last* `/listfiles` call rather than
  /// from the request, so the caller has to list the folder first. The launcher reads `dir`
  /// off the query string; sending both keeps one code path for the two of them.
  Future<void> upload(String folder, String name, List<int> bytes) async {
    final req = http.MultipartRequest('POST', uri('/upload', {'fs': fsName, 'dir': folder}))
      ..headers.addAll(authHeaders)
      ..files.add(http.MultipartFile.fromBytes('file', bytes, filename: name));
    final res = await req.send().timeout(const Duration(minutes: 5));
    final body = await res.stream.bytesToString();
    if (res.statusCode != 200) {
      throw LinkException(body.isEmpty ? 'Upload failed (${res.statusCode})' : body.trim());
    }
  }

  // ---- slots ---------------------------------------------------------------------------
  // Only the launcher owns the slots; Bruce is one of the things sitting in them.

  bool get supportsSlots => false;

  Future<List<AppSlotInfo>> slots() async => const [];

  /// Reboots the device into that slot. The link dies with it — that is expected.
  Future<void> bootSlot(int n) async {}

  void close() => client.close();
}

/// One of the launcher's install slots.
class AppSlotInfo {
  const AppSlotInfo(this.index, this.name, this.installed);

  final int index;
  final String name;
  final bool installed;

  static AppSlotInfo? fromJson(Object? raw) {
    if (raw is! Map) return null;
    final n = raw['n'];
    if (n is! int) return null;
    return AppSlotInfo(n, raw['name'] as String? ?? 'Slot $n', raw['installed'] == true);
  }
}

// ---------------------------------------------------------------------------------------
// Bruce — draw-command stream, session cookie, `nav` commands.
// ---------------------------------------------------------------------------------------

class BruceLink extends DeviceLink {
  BruceLink(super.host, {this.user = 'admin', this.password = 'bruce'});

  final String user;
  final String password;
  String? _session;

  final _images = <String, ui.Image>{};
  final _pending = <String>{};

  @override
  String get kind => 'bruce';
  @override
  String get title => 'Bruce';
  @override
  Set<RemoteKey> get keys => RemoteKey.values.toSet();
  @override
  Map<String, ui.Image> get images => _images;

  @override
  Map<String, String> get authHeaders =>
      _session == null ? const {} : {'Cookie': 'BRUCESESSION=$_session'};

  Map<String, String> get _headers => authHeaders;

  @override
  Future<void> open() async {
    // The WebUI answers /login with a 302 plus a Set-Cookie; the redirect must not be
    // followed or the header is lost.
    final req = http.Request('POST', uri('/login'))
      ..followRedirects = false
      ..bodyFields = {'username': user, 'password': password};
    final res = await client.send(req).timeout(const Duration(seconds: 6));
    await res.stream.drain<void>();

    final cookie = res.headers['set-cookie'];
    final token = cookie == null
        ? null
        : RegExp(r'BRUCESESSION=([^;]+)').firstMatch(cookie)?.group(1);
    if (token == null || token.isEmpty) {
      throw LinkException(
        'Bruce refused the login. Check the user and password it shows '
        'on screen when the WebUI starts (default admin / bruce).',
      );
    }
    _session = token;
  }

  @override
  Future<ScreenFrame> grab() async {
    final res = await client
        .get(uri('/getscreen'), headers: _headers)
        .timeout(const Duration(seconds: 4));
    if (res.statusCode == 401) {
      _session = null;
      throw LinkException('Session expired');
    }
    if (res.statusCode != 200) {
      throw LinkException('getscreen returned ${res.statusCode}');
    }
    final screen = decodeScreen(res.bodyBytes);
    return ScreenFrame.vector(screen, res.bodyBytes.length);
  }

  @override
  Future<void> send(RemoteKey key) async {
    const names = {
      RemoteKey.prev: 'prev',
      RemoteKey.next: 'next',
      RemoteKey.sel: 'sel',
      RemoteKey.esc: 'esc',
      RemoteKey.up: 'up',
      RemoteKey.down: 'down',
    };
    await client
        .post(uri('/cm'), headers: _headers, body: {'cmnd': 'nav ${names[key]}'})
        .timeout(const Duration(seconds: 4));
  }

  /// Bruce refers to images by path; fetch each one once and keep it.
  @override
  Future<void> prefetchImages(VectorScreen screen) async {
    for (final op in screen.ops) {
      final path = op.text;
      if (op.fn != Op.drawImage || path == null || path.isEmpty) continue;
      if (_images.containsKey(path) || _pending.contains(path)) continue;
      _pending.add(path);
      unawaited(_fetchImage(path, op['fs'] == 0 ? 'SD' : 'FS'));
    }
  }

  Future<void> _fetchImage(String path, String fs) async {
    try {
      final res = await client
          .get(
            uri('/file', {'fs': fs, 'name': path, 'action': 'image'}),
            headers: _headers,
          )
          .timeout(const Duration(seconds: 8));
      if (res.statusCode == 200 && res.bodyBytes.isNotEmpty) {
        final codec = await ui.instantiateImageCodec(res.bodyBytes);
        _images[path] = (await codec.getNextFrame()).image;
      }
    } catch (_) {
      // A missing or unreadable image just stays unpainted.
    } finally {
      _pending.remove(path);
    }
  }
}

// ---------------------------------------------------------------------------------------
// Launcher — raw RGB565 out of the PSRAM canvas.
// ---------------------------------------------------------------------------------------

class LauncherLink extends DeviceLink {
  LauncherLink(super.host, {this.scale = 2});

  /// 1 = full 320x170, 2 = half, 3/4 = smaller still. Straight through to `/fb?s=`.
  int scale;

  @override
  String get kind => 'launcher';
  @override
  String get title => 'Launcher';

  @override
  Future<void> open() async {
    final res = await client
        .get(uri('/fb', {'s': '4'}))
        .timeout(const Duration(seconds: 4));
    if (res.statusCode != 200) {
      throw LinkException('The launcher is not serving its screen (${res.statusCode}).');
    }
  }

  @override
  Future<ScreenFrame> grab() async {
    final res = await client
        .get(uri('/fb', {'s': '$scale'}))
        .timeout(const Duration(seconds: 5));
    if (res.statusCode != 200) throw LinkException('fb returned ${res.statusCode}');

    final w = int.tryParse(res.headers['x-width'] ?? '') ?? 160;
    final h = int.tryParse(res.headers['x-height'] ?? '') ?? 85;
    final body = res.bodyBytes;
    if (body.length < w * h * 2) throw LinkException('Short frame');

    final image = await _decodeRgb565(body, w, h);
    return ScreenFrame.bitmap(image, body.length);
  }

  @override
  bool get supportsSlots => true;

  @override
  Future<List<AppSlotInfo>> slots() async {
    final res = await client.get(uri('/slots')).timeout(const Duration(seconds: 5));
    if (res.statusCode != 200) throw LinkException('slots returned ${res.statusCode}');
    final raw = jsonDecode(res.body);
    if (raw is! List) throw LinkException('Unexpected slot list');
    return raw.map(AppSlotInfo.fromJson).whereType<AppSlotInfo>().toList();
  }

  @override
  Future<void> bootSlot(int n) async {
    final res = await client
        .get(uri('/boot', {'slot': '$n'}))
        .timeout(const Duration(seconds: 6));
    if (res.statusCode != 200) {
      throw LinkException(res.body.isEmpty ? 'Could not boot slot $n' : res.body.trim());
    }
  }

  @override
  Future<void> send(RemoteKey key) async {
    const names = {
      RemoteKey.prev: 'left',
      RemoteKey.next: 'right',
      RemoteKey.sel: 'ok',
      RemoteKey.esc: 'back',
    };
    final name = names[key];
    if (name == null) return;
    await client.get(uri('/key', {'k': name})).timeout(const Duration(seconds: 4));
  }
}

/// Little-endian RGB565 -> RGBA8888, then straight into a GPU texture.
Future<ui.Image> _decodeRgb565(Uint8List src, int w, int h) {
  final n = w * h;
  final out = Uint8List(n * 4);
  for (var i = 0; i < n; i++) {
    final p = src[i * 2] | (src[i * 2 + 1] << 8);
    out[i * 4] = ((p >> 11) & 0x1F) * 255 ~/ 31;
    out[i * 4 + 1] = ((p >> 5) & 0x3F) * 255 ~/ 63;
    out[i * 4 + 2] = (p & 0x1F) * 255 ~/ 31;
    out[i * 4 + 3] = 0xFF;
  }
  final done = Completer<ui.Image>();
  ui.decodeImageFromPixels(out, w, h, ui.PixelFormat.rgba8888, done.complete);
  return done.future;
}
