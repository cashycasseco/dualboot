/// Remembers the last device and the auto-connect setting between app starts.
///
/// Deliberately a plain file rather than shared_preferences: that package is the only
/// dependency here with native Android code, and dragging in a platform plugin for one IP
/// address and a boolean is not worth the build-toolchain coupling. `systemTemp` resolves to
/// the app's own private cache directory on Android and iOS, which is the right lifetime for
/// a remembered address anyway — worst case the app rediscovers the device.
library;

import 'dart:convert';
import 'dart:io';

class Store {
  Store._(this._file, this._data);

  final File _file;
  final Map<String, dynamic> _data;

  static Future<Store> open() async {
    final file = File('${Directory.systemTemp.path}/tembed_remote.json');
    var data = <String, dynamic>{};
    try {
      if (await file.exists()) {
        final decoded = jsonDecode(await file.readAsString());
        if (decoded is Map<String, dynamic>) data = decoded;
      }
    } catch (_) {
      // Corrupt or unreadable: start clean rather than fail to launch.
    }
    return Store._(file, data);
  }

  String get host => _data['host'] as String? ?? '';
  String get user => _data['user'] as String? ?? 'admin';
  String get password => _data['pass'] as String? ?? 'bruce';
  bool get auto => _data['auto'] as bool? ?? false;

  List<dynamic> get macros => _data['macros'] as List<dynamic>? ?? const [];

  Future<void> saveDevice({
    required String host,
    required String user,
    required String password,
  }) => _write({'host': host, 'user': user, 'pass': password});

  Future<void> saveAuto(bool auto) => _write({'auto': auto});

  Future<void> saveMacros(List<Map<String, dynamic>> macros) => _write({'macros': macros});

  Future<void> _write(Map<String, dynamic> values) async {
    _data.addAll(values);
    try {
      await _file.writeAsString(jsonEncode(_data), flush: true);
    } catch (_) {
      // Not being able to remember is not worth surfacing.
    }
  }
}
