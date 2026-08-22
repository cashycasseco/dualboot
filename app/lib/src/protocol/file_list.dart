/// The card listing, in the format Bruce's WebUI already speaks.
///
/// The launcher now answers `/listfiles` with the same lines on purpose, so browsing either
/// device needs exactly one parser and one set of calls:
///
///     pa:/folder:0          the folder that was listed
///     Fo:name:0             a directory
///     Fi:name:12.3 KB       a file, with the size already formatted by the device
library;

class RemoteEntry {
  const RemoteEntry({
    required this.name,
    required this.isDir,
    required this.size,
    required this.path,
  });

  final String name;
  final bool isDir;

  /// As the device formatted it. Not parsed back into bytes — it is only ever displayed,
  /// and the two firmwares round differently.
  final String size;

  /// Full path, ready to hand back to `/file?name=`.
  final String path;
}

class FileListing {
  const FileListing(this.folder, this.entries);

  final String folder;
  final List<RemoteEntry> entries;

  bool get isRoot => folder == '/' || folder.isEmpty;

  /// The folder above this one, or null at the root.
  String? get parent {
    if (isRoot) return null;
    final cut = folder.lastIndexOf('/');
    return cut <= 0 ? '/' : folder.substring(0, cut);
  }
}

FileListing parseListing(String body, String requested) {
  var folder = requested;
  final dirs = <RemoteEntry>[];
  final files = <RemoteEntry>[];

  String join(String name) => folder == '/' ? '/$name' : '$folder/$name';

  for (final raw in body.split('\n')) {
    final line = raw.trimRight();
    if (line.length < 4) continue;
    final tag = line.substring(0, 3);
    final rest = line.substring(3);

    // Names may contain colons; the size is whatever follows the last one.
    final cut = rest.lastIndexOf(':');
    if (cut < 0) continue;
    final name = rest.substring(0, cut);
    final size = rest.substring(cut + 1);
    if (name.isEmpty) continue;

    switch (tag) {
      case 'pa:':
        folder = name.isEmpty ? '/' : name;
      case 'Fo:':
        dirs.add(RemoteEntry(name: name, isDir: true, size: '', path: join(name)));
      case 'Fi:':
        files.add(RemoteEntry(name: name, isDir: false, size: size, path: join(name)));
    }
  }

  // Folders first, then files, each alphabetically — the device returns raw card order.
  int byName(RemoteEntry a, RemoteEntry b) =>
      a.name.toLowerCase().compareTo(b.name.toLowerCase());
  dirs.sort(byName);
  files.sort(byName);

  return FileListing(folder, [...dirs, ...files]);
}
