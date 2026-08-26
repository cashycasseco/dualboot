import 'dart:ui' as ui;

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tembed_remote/src/link/device_link.dart';
import 'package:tembed_remote/src/macros.dart';
import 'package:tembed_remote/src/protocol/file_list.dart';
import 'package:tembed_remote/src/protocol/screen_ops.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('decodes a Bruce screen log', () {
    // SCREEN_INFO 320x170 rot 3, then FILLRECT(2,3,4,5) in 0xF800, then DRAWSTRING "HI".
    final bytes = Uint8List.fromList([
      0xAA, 8, 99, 0x01, 0x40, 0x00, 0xAA, 0x03,
      0xAA, 13, 2, 0, 2, 0, 3, 0, 4, 0, 5, 0xF8, 0x00,
      0xAA, 15, 16, 0, 10, 0, 20, 0, 1, 0xFF, 0xFF, 0x00, 0x00, 0x48, 0x49,
    ]);

    final screen = decodeScreen(bytes);
    expect(screen.width, 320);
    expect(screen.height, 170);
    expect(screen.ops.length, 3);

    expect(screen.ops[1].fn, Op.fillRect);
    expect(screen.ops[1]['x'], 2);
    expect(screen.ops[1]['h'], 5);
    expect(screen.ops[1]['fg'], 0xF800);

    expect(screen.ops[2].fn, Op.drawString);
    expect(screen.ops[2]['size'], 1);
    expect(screen.ops[2].text, 'HI');
  });

  test('stops cleanly on a truncated tail', () {
    // The ring buffer really does hand out half a packet; the ops before it must survive.
    final bytes = Uint8List.fromList([0xAA, 5, 0, 0x00, 0x1F, 0xAA, 40, 2, 0, 1]);
    final screen = decodeScreen(bytes);
    expect(screen.ops.length, 1);
    expect(screen.ops.first.fn, Op.fillScreen);
    expect(screen.ops.first['fg'], 0x001F);
  });

  // The waiting state is a bundled GIF; if it ever falls out of pubspec.yaml the tube would
  // silently go blank on the phone and nothing else would complain.
  test('the sleeping cat is bundled and decodable', () async {
    final data = await rootBundle.load('assets/cat-sleep.gif');
    expect(data.lengthInBytes, greaterThan(1000));

    final codec = await ui.instantiateImageCodec(data.buffer.asUint8List());
    expect(codec.frameCount, greaterThan(1), reason: 'it has to actually animate');

    final frame = await codec.getNextFrame();
    expect(frame.image.width, 150);
    expect(frame.image.height, 138);
  });

  // The card listing is the one format both firmwares share. If the parser drifts, browsing
  // shows the wrong thing rather than failing outright.
  group('card listing', () {
    test('splits folders from files and sorts each group', () {
      const body =
          'pa:/catcolor:0\n'
          'Fi:theme.txt:412 B\n'
          'Fo:dedsec:0\n'
          'Fi:Aurora.txt:1.2 KB\n'
          'Fo:amber:0\n';

      final listing = parseListing(body, '/catcolor');
      expect(listing.folder, '/catcolor');
      expect(
        listing.entries.map((e) => e.name),
        ['amber', 'dedsec', 'Aurora.txt', 'theme.txt'],
      );
      expect(listing.entries.first.isDir, isTrue);
      expect(listing.entries.last.path, '/catcolor/theme.txt');
      expect(listing.entries.last.size, '412 B');
      expect(listing.parent, '/');
    });

    test('keeps colons that belong to the name', () {
      final listing = parseListing('pa:/:0\nFi:12:30 dump.bin:8 KB\n', '/');
      expect(listing.entries.single.name, '12:30 dump.bin');
      expect(listing.entries.single.size, '8 KB');
      expect(listing.entries.single.path, '/12:30 dump.bin');
    });

    test('root has no parent', () {
      expect(parseListing('pa:/:0\n', '/').parent, isNull);
    });
  });

  // A macro recorded against Bruce can hold keys the launcher does not have. It has to come
  // back usable rather than being thrown away wholesale.
  group('macros', () {
    test('survive a round trip through storage', () {
      const m = Macro('WiFi menu', [RemoteKey.next, RemoteKey.next, RemoteKey.sel]);
      final back = Macro.fromJson(m.toJson())!;
      expect(back.name, 'WiFi menu');
      expect(back.steps, [RemoteKey.next, RemoteKey.next, RemoteKey.sel]);
    });

    test('drop steps the firmware does not know, keep the rest', () {
      final back = Macro.fromJson({
        'name': 'from Bruce',
        'steps': ['up', 'nonsense', 'sel'],
      })!;
      expect(back.steps, [RemoteKey.up, RemoteKey.sel]);
    });

    test('reject junk instead of half-building something', () {
      expect(Macro.fromJson({'name': 'x', 'steps': <String>[]}), isNull);
      expect(Macro.fromJson({'steps': ['sel']}), isNull);
      expect(Macro.fromJson('nope'), isNull);
    });

    test('summarise short and long sequences differently', () {
      expect(const Macro('a', [RemoteKey.prev, RemoteKey.sel]).summary, '◀ OK');
      expect(
        Macro('b', List.filled(9, RemoteKey.next)).summary,
        endsWith('9 steps'),
      );
    });
  });

  // The slot list drives the boot buttons; a malformed entry must not take the list with it.
  test('slot list skips entries without an index', () {
    expect(AppSlotInfo.fromJson({'n': 2, 'name': 'Bruce', 'installed': true})?.name, 'Bruce');
    expect(AppSlotInfo.fromJson({'n': 3})?.name, 'Slot 3');
    expect(AppSlotInfo.fromJson({'name': 'no index'}), isNull);
  });
}
