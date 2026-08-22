/// Decoder for Bruce's screen log — the same byte stream its own WebUI navigator reads
/// from `GET /getscreen`.
///
/// Bruce does not ship pixels. Its display wrapper (`tft_logger`) records the *drawing
/// commands* the firmware issued — fill this rect, print that string — into a ring buffer,
/// and hands the buffer out on request. A whole screen is a few hundred bytes instead of
/// the 108 KB a raw RGB565 frame would cost, which is why this stays smooth over WiFi and
/// would even fit through BLE.
///
/// Packet layout, repeated until the buffer ends:
///
///     0xAA | size | fn | payload…        (size counts the three header bytes too)
///
/// Payload fields are big-endian int16 in the order given by [_keys], except `rotation`
/// and `fs`, which are a single byte, and `txt`/`file`, which swallow the rest of the
/// packet as UTF-8. Opcode numbers must match `enum tftFuncs` in Bruce's tftLogger.h.
library;

import 'dart:convert';
import 'dart:typed_data';

const int kPacketHeader = 0xAA;

/// Opcode numbers, from `enum tftFuncs`. The order is fixed by the firmware.
abstract final class Op {
  static const fillScreen = 0;
  static const drawRect = 1;
  static const fillRect = 2;
  static const drawRoundRect = 3;
  static const fillRoundRect = 4;
  static const drawCircle = 5;
  static const fillCircle = 6;
  static const drawTriangle = 7;
  static const fillTriangle = 8;
  static const drawEllipse = 9;
  static const fillEllipse = 10;
  static const drawLine = 11;
  static const drawArc = 12;
  static const drawWideLine = 13;
  static const drawCentreString = 14;
  static const drawRightString = 15;
  static const drawString = 16;
  static const print = 17;
  static const drawImage = 18;
  static const drawPixel = 19;
  static const drawFastVLine = 20;
  static const drawFastHLine = 21;
  static const screenInfo = 99;
}

/// Field names per opcode, in wire order.
const Map<int, List<String>> _keys = {
  0: ['fg'],
  1: ['x', 'y', 'w', 'h', 'fg'],
  2: ['x', 'y', 'w', 'h', 'fg'],
  3: ['x', 'y', 'w', 'h', 'r', 'fg'],
  4: ['x', 'y', 'w', 'h', 'r', 'fg'],
  5: ['x', 'y', 'r', 'fg'],
  6: ['x', 'y', 'r', 'fg'],
  7: ['x', 'y', 'x2', 'y2', 'x3', 'y3', 'fg'],
  8: ['x', 'y', 'x2', 'y2', 'x3', 'y3', 'fg'],
  9: ['x', 'y', 'rx', 'ry', 'fg'],
  10: ['x', 'y', 'rx', 'ry', 'fg'],
  11: ['x', 'y', 'x1', 'y1', 'fg'],
  12: ['x', 'y', 'r', 'ir', 'startAngle', 'endAngle', 'fg', 'bg'],
  13: ['x', 'y', 'bx', 'by', 'wd', 'fg', 'bg'],
  14: ['x', 'y', 'size', 'fg', 'bg', 'txt'],
  15: ['x', 'y', 'size', 'fg', 'bg', 'txt'],
  16: ['x', 'y', 'size', 'fg', 'bg', 'txt'],
  17: ['x', 'y', 'size', 'fg', 'bg', 'txt'],
  18: ['x', 'y', 'center', 'ms', 'fs', 'file'],
  19: ['x', 'y', 'fg'],
  20: ['x', 'y', 'h', 'fg'],
  21: ['x', 'y', 'w', 'fg'],
  99: ['w', 'h', 'rotation'],
};

/// One decoded drawing command.
class ScreenOp {
  ScreenOp(this.fn, this.args, {this.text});

  final int fn;
  final Map<String, int> args;

  /// The `txt` of a string op, or the `file` path of an image op.
  final String? text;

  int operator [](String k) => args[k] ?? 0;
  bool has(String k) => args.containsKey(k);

  @override
  String toString() => 'ScreenOp($fn, $args${text == null ? '' : ', "$text"'})';
}

/// A decoded screen: the ops to replay plus the panel size the device reported.
class VectorScreen {
  VectorScreen(this.ops, this.width, this.height);

  final List<ScreenOp> ops;
  final int width;
  final int height;

  bool get isEmpty => ops.isEmpty;
}

/// Decodes a `/getscreen` body. Stops at the first byte that is not a packet header, the
/// way the firmware's own client does — a truncated tail is normal when the ring buffer
/// wrapped mid-read, and the ops before it are still good.
VectorScreen decodeScreen(Uint8List data, {int fallbackW = 320, int fallbackH = 170}) {
  final ops = <ScreenOp>[];
  var w = fallbackW, h = fallbackH;
  var offset = 0;

  while (offset + 3 <= data.length) {
    if (data[offset] != kPacketHeader) break;
    final size = data[offset + 1];
    final fn = data[offset + 2];
    if (size < 3 || offset + size > data.length) break;

    final keys = _keys[fn];
    if (keys != null) {
      var p = offset + 3;
      final end = offset + size;
      final args = <String, int>{};
      String? text;

      for (final key in keys) {
        if (key == 'txt' || key == 'file') {
          text = utf8.decode(data.sublist(p, end), allowMalformed: true);
          p = end;
        } else if (key == 'rotation' || key == 'fs') {
          if (p >= end) break;
          args[key] = data[p++];
        } else {
          if (p + 2 > end) break;
          args[key] = (data[p] << 8) | data[p + 1];
          p += 2;
        }
      }

      if (fn == Op.screenInfo) {
        if ((args['w'] ?? 0) > 0) w = args['w']!;
        if ((args['h'] ?? 0) > 0) h = args['h']!;
      }
      ops.add(ScreenOp(fn, args, text: text));
    }
    offset += size;
  }

  return VectorScreen(ops, w, h);
}
