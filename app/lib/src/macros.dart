/// Recorded key sequences.
///
/// Getting anywhere on the device means a lot of wheel turns — five to the right, press, two
/// down, press. A macro records the taps you already made and plays them back, so the routes
/// you walk every day become one button.
library;

import 'link/device_link.dart';

class Macro {
  const Macro(this.name, this.steps);

  final String name;
  final List<RemoteKey> steps;

  Map<String, dynamic> toJson() => {
    'name': name,
    'steps': steps.map((k) => k.name).toList(),
  };

  static Macro? fromJson(Object? raw) {
    if (raw is! Map) return null;
    final name = raw['name'];
    final steps = raw['steps'];
    if (name is! String || steps is! List) return null;

    final keys = <RemoteKey>[];
    for (final s in steps) {
      final key = RemoteKey.values.where((k) => k.name == s).firstOrNull;
      // A macro recorded against Bruce can hold up/down, which the launcher has no use for.
      // Drop what we cannot name rather than throwing the whole macro away.
      if (key != null) keys.add(key);
    }
    if (keys.isEmpty) return null;
    return Macro(name, keys);
  }

  /// Short human summary, e.g. "▶ ▶ OK · 3 steps".
  String get summary {
    const glyph = {
      RemoteKey.prev: '◀',
      RemoteKey.next: '▶',
      RemoteKey.sel: 'OK',
      RemoteKey.esc: 'BACK',
      RemoteKey.up: '▲',
      RemoteKey.down: '▼',
    };
    final head = steps.take(6).map((k) => glyph[k]).join(' ');
    return steps.length > 6 ? '$head …  ${steps.length} steps' : head;
  }
}
