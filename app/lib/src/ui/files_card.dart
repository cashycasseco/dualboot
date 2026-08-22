/// Browsing the device's SD card.
///
/// Both firmwares answer the same calls, so this widget does not know or care which one is
/// on the other end — it just lists, walks into folders, and deletes.
library;

import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../link/device_link.dart';
import '../protocol/file_list.dart';
import 'design.dart';

class FilesCard extends StatefulWidget {
  const FilesCard({super.key, required this.link});

  final DeviceLink link;

  @override
  State<FilesCard> createState() => _FilesCardState();
}

class _FilesCardState extends State<FilesCard> {
  FileListing? _listing;
  String _folder = '/';
  String? _error;
  bool _busy = false;
  String? _sending;

  @override
  void initState() {
    super.initState();
    _load('/');
  }

  @override
  void didUpdateWidget(FilesCard old) {
    super.didUpdateWidget(old);
    // A different device (or a reboot into other firmware) means a different card.
    if (old.link != widget.link) _load('/');
  }

  Future<void> _load(String folder) async {
    setState(() {
      _busy = true;
      _error = null;
    });
    try {
      final listing = await widget.link.listFiles(folder);
      if (!mounted) return;
      setState(() {
        _listing = listing;
        _folder = listing.folder;
        _busy = false;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _error = e is LinkException ? e.message : 'Could not read $folder';
        _busy = false;
      });
    }
  }

  Future<void> _delete(RemoteEntry entry) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (context) => _ConfirmSheet(entry: entry),
    );
    if (ok != true) return;
    setState(() => _busy = true);
    try {
      await widget.link.deleteEntry(entry.path);
    } catch (e) {
      if (mounted) {
        setState(() => _error = e is LinkException ? e.message : 'Could not delete');
      }
    }
    await _load(_folder);
  }

  Future<void> _upload() async {
    final picked = await FilePicker.platform.pickFiles();
    final file = picked?.files.singleOrNull;
    if (file == null) return;

    // Prefer the path: reading straight off disk keeps a 4 MB firmware image out of the
    // picker's own copy of the bytes.
    List<int>? bytes = file.bytes;
    if (file.path != null) {
      try {
        bytes = await File(file.path!).readAsBytes();
      } catch (_) {
        // Fall through to whatever the picker handed us.
      }
    }
    if (bytes == null) {
      setState(() => _error = 'Could not read ${file.name}');
      return;
    }

    setState(() {
      _busy = true;
      _sending = file.name;
      _error = null;
    });
    try {
      // Bruce takes the destination from the last listing, so make sure it is this folder.
      await widget.link.listFiles(_folder);
      await widget.link.upload(_folder, file.name, bytes);
    } catch (e) {
      if (mounted) {
        setState(() => _error = e is LinkException ? e.message : 'Upload failed');
      }
    }
    if (mounted) setState(() => _sending = null);
    await _load(_folder);
  }

  Future<void> _newFolder() async {
    final name = await showDialog<String>(
      context: context,
      builder: (context) => const _NameSheet(),
    );
    if (name == null || name.isEmpty) return;
    setState(() => _busy = true);
    try {
      await widget.link.makeFolder(_folder == '/' ? '/$name' : '$_folder/$name');
    } catch (e) {
      if (mounted) {
        setState(() => _error = e is LinkException ? e.message : 'Could not create folder');
      }
    }
    await _load(_folder);
  }

  @override
  Widget build(BuildContext context) {
    final listing = _listing;
    final parent = listing?.parent;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Label('Files', trailing: _folder),
        CreamCard(
          padding: const EdgeInsets.fromLTRB(8, 8, 8, 14),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              if (parent != null)
                _Row(
                  icon: Icons.arrow_upward,
                  title: '..',
                  subtitle: parent,
                  onTap: _busy ? null : () => _load(parent),
                ),

              if (_error != null)
                Padding(
                  padding: const EdgeInsets.fromLTRB(12, 12, 12, 8),
                  child: Text(_error!, style: body(size: 12, color: D.warn)),
                )
              else if (listing == null || (_busy && listing.entries.isEmpty))
                const Padding(
                  padding: EdgeInsets.symmetric(vertical: 22),
                  child: Center(
                    child: SizedBox(
                      width: 18,
                      height: 18,
                      child: CircularProgressIndicator(strokeWidth: 1.8, color: D.ink),
                    ),
                  ),
                )
              else if (listing.entries.isEmpty)
                Padding(
                  padding: const EdgeInsets.fromLTRB(12, 14, 12, 10),
                  child: Text('This folder is empty.', style: body(size: 12.5)),
                )
              else
                for (final e in listing.entries)
                  _Row(
                    icon: e.isDir ? Icons.folder_outlined : Icons.description_outlined,
                    title: e.name,
                    subtitle: e.isDir ? 'folder' : e.size,
                    onTap: _busy ? null : (e.isDir ? () => _load(e.path) : null),
                    onDelete: _busy ? null : () => _delete(e),
                  ),

              if (_sending != null)
                Padding(
                  padding: const EdgeInsets.fromLTRB(12, 6, 12, 2),
                  child: Row(
                    children: [
                      const SizedBox(
                        width: 14,
                        height: 14,
                        child: CircularProgressIndicator(strokeWidth: 1.6, color: D.ink),
                      ),
                      const SizedBox(width: 11),
                      Expanded(
                        child: Text(
                          'Sending $_sending …',
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: body(size: 12),
                        ),
                      ),
                    ],
                  ),
                ),

              const SizedBox(height: 10),
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 4),
                child: Column(
                  children: [
                    Pill(
                      label: 'Upload a file here',
                      height: 46,
                      enabled: !_busy,
                      onTap: _upload,
                    ),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        Expanded(
                          child: Pill(
                            label: 'Reload',
                            height: 44,
                            quiet: true,
                            enabled: !_busy,
                            onTap: () => _load(_folder),
                          ),
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Pill(
                            label: 'New folder',
                            height: 44,
                            quiet: true,
                            enabled: !_busy,
                            onTap: _newFolder,
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _Row extends StatelessWidget {
  const _Row({
    required this.icon,
    required this.title,
    required this.subtitle,
    this.onTap,
    this.onDelete,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback? onTap;
  final VoidCallback? onDelete;

  @override
  Widget build(BuildContext context) => Material(
    color: Colors.transparent,
    child: InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(D.rChip),
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 11, 6, 11),
        child: Row(
          children: [
            Icon(icon, size: 19, color: D.inkSoft),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w500,
                      letterSpacing: -0.1,
                      color: D.ink,
                    ),
                  ),
                  if (subtitle.isNotEmpty) Text(subtitle, style: body(size: 11.5)),
                ],
              ),
            ),
            if (onDelete != null)
              IconButton(
                onPressed: onDelete,
                icon: Icon(Icons.delete_outline, size: 19, color: D.inkSoft),
                visualDensity: VisualDensity.compact,
              ),
          ],
        ),
      ),
    ),
  );
}

class _ConfirmSheet extends StatelessWidget {
  const _ConfirmSheet({required this.entry});

  final RemoteEntry entry;

  @override
  Widget build(BuildContext context) => Dialog(
    backgroundColor: Colors.transparent,
    insetPadding: const EdgeInsets.symmetric(horizontal: 24),
    child: CreamCard(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('Delete ${entry.isDir ? 'folder' : 'file'}?', style: display(size: 19)),
          const SizedBox(height: 5),
          Text(entry.path, style: serifSub(size: 13)),
          if (entry.isDir) ...[
            const SizedBox(height: 10),
            Text(
              'A folder has to be empty before the device will remove it.',
              style: body(size: 12),
            ),
          ],
          const SizedBox(height: 18),
          Row(
            children: [
              Expanded(
                child: Pill(
                  label: 'Keep',
                  height: 46,
                  quiet: true,
                  onTap: () => Navigator.of(context).pop(false),
                ),
              ),
              const SizedBox(width: 9),
              Expanded(
                child: Pill(
                  label: 'Delete',
                  height: 46,
                  onTap: () => Navigator.of(context).pop(true),
                ),
              ),
            ],
          ),
        ],
      ),
    ),
  );
}

class _NameSheet extends StatefulWidget {
  const _NameSheet();

  @override
  State<_NameSheet> createState() => _NameSheetState();
}

class _NameSheetState extends State<_NameSheet> {
  final _c = TextEditingController();

  @override
  void dispose() {
    _c.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Dialog(
    backgroundColor: Colors.transparent,
    insetPadding: const EdgeInsets.symmetric(horizontal: 24),
    child: CreamCard(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text('New folder', style: display(size: 19)),
          const SizedBox(height: 14),
          SoftField(
            controller: _c,
            hint: 'name',
            onSubmitted: (v) => Navigator.of(context).pop(v.trim()),
          ),
          const SizedBox(height: 14),
          Row(
            children: [
              Expanded(
                child: Pill(
                  label: 'Cancel',
                  height: 46,
                  quiet: true,
                  onTap: () => Navigator.of(context).pop(),
                ),
              ),
              const SizedBox(width: 9),
              Expanded(
                child: Pill(
                  label: 'Create',
                  height: 46,
                  onTap: () => Navigator.of(context).pop(_c.text.trim()),
                ),
              ),
            ],
          ),
        ],
      ),
    ),
  );
}
