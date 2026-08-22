/// Renders a synthetic Bruce screen through the real decoder and painter and writes it to
/// build/render.png, so the renderer can be eyeballed without a device on the bench.
library;

import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tembed_remote/src/protocol/screen_ops.dart';
import 'package:tembed_remote/src/protocol/screen_painter.dart';

/// Builds packets the same way tftLogger.cpp does: 0xAA, size, fn, big-endian payload.
class LogBuilder {
  final _out = <int>[];

  void op(int fn, List<int> words, {String? text, List<int> bytes = const []}) {
    final payload = <int>[];
    for (final w in words) {
      payload..add((w >> 8) & 0xFF)..add(w & 0xFF);
    }
    payload.addAll(bytes);
    if (text != null) payload.addAll(text.codeUnits);
    _out..add(0xAA)..add(payload.length + 3)..add(fn)..addAll(payload);
  }

  Uint8List done() => Uint8List.fromList(_out);
}

int rgb(int r, int g, int b) =>
    ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

void main() {
  test('renders a screen to build/render.png', () async {
    final b = LogBuilder();
    final bg = rgb(5, 7, 12);
    final accent = rgb(61, 250, 160);
    final ink = rgb(217, 230, 242);
    final dim = rgb(127, 146, 173);

    b.op(Op.screenInfo, [320, 170], bytes: [3]);
    b.op(Op.fillScreen, [bg]);

    // title bar
    b.op(Op.fillRect, [0, 0, 320, 20, rgb(11, 16, 32)]);
    b.op(Op.drawFastHLine, [0, 20, 320, rgb(27, 44, 70)]);
    b.op(Op.drawString, [8, 6, 1, accent, bg], text: 'BRUCE  1.16.1');
    b.op(Op.drawRightString, [312, 6, 1, dim, bg], text: '86%');

    // selected row
    b.op(Op.fillRoundRect, [10, 34, 300, 26, 3, rgb(17, 66, 48)]);
    b.op(Op.drawString, [22, 41, 1, accent, rgb(17, 66, 48)], text: 'WiFi');

    b.op(Op.drawString, [22, 71, 1, ink, bg], text: 'Bluetooth');
    b.op(Op.drawString, [22, 91, 1, ink, bg], text: 'RF');
    b.op(Op.drawString, [22, 111, 1, ink, bg], text: 'RFID');

    // a few shapes so every branch of the painter runs
    b.op(Op.drawRect, [230, 34, 70, 70, rgb(27, 44, 70)]);
    b.op(Op.fillCircle, [265, 69, 18, rgb(34, 211, 238)]);
    b.op(Op.drawTriangle, [240, 120, 300, 120, 270, 150, accent]);
    b.op(Op.drawLine, [10, 160, 310, 160, rgb(27, 44, 70)]);
    b.op(Op.drawCentreString, [160, 140, 2, ink, bg], text: 'MIRROR');

    final screen = decodeScreen(b.done());
    expect(screen.width, 320);
    expect(screen.height, 170);
    expect(screen.ops.length, greaterThan(10));

    const scale = 3.0;
    final recorder = ui.PictureRecorder();
    final canvas = Canvas(recorder);
    ScreenPainter(screen: screen).paint(canvas, const Size(320 * scale, 170 * scale));
    final image = await recorder.endRecording().toImage(
      (320 * scale).round(),
      (170 * scale).round(),
    );
    final png = await image.toByteData(format: ui.ImageByteFormat.png);

    final out = File('build/render.png');
    out.parent.createSync(recursive: true);
    out.writeAsBytesSync(png!.buffer.asUint8List());
    expect(out.lengthSync(), greaterThan(1000));
  });
}
