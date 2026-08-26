/// The two cards that turn the remote from "a d-pad" into something quicker: jumping
/// straight into an install slot, and replaying a route you have walked before.
library;

import 'package:flutter/material.dart';

import '../link/device_link.dart';
import '../macros.dart';
import '../session.dart';
import 'design.dart';

// =========================================================================================
// slots
// =========================================================================================

class SlotsCard extends StatefulWidget {
  const SlotsCard({super.key, required this.session});

  final RemoteSession session;

  @override
  State<SlotsCard> createState() => _SlotsCardState();
}

class _SlotsCardState extends State<SlotsCard> {
  List<AppSlotInfo>? _slots;
  bool _busy = false;

  @override
  void initState() {
    super.initState();
    _load();
  }

  @override
  void didUpdateWidget(SlotsCard old) {
    super.didUpdateWidget(old);
    if (old.session.link != widget.session.link) _load();
  }

  Future<void> _load() async {
    final link = widget.session.link;
    if (link == null || !link.supportsSlots) return;
    try {
      final slots = await link.slots();
      if (mounted) setState(() => _slots = slots);
    } catch (_) {
      if (mounted) setState(() => _slots = const []);
    }
  }

  Future<void> _boot(AppSlotInfo slot) async {
    setState(() => _busy = true);
    await widget.session.bootSlot(slot.index);
    if (mounted) setState(() => _busy = false);
  }

  @override
  Widget build(BuildContext context) {
    final slots = _slots;
    if (slots == null || slots.isEmpty) return const SizedBox.shrink();
    final usable = slots.where((s) => s.installed).toList();

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const Label('Boot into', trailing: 'slots'),
        CreamCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              if (usable.isEmpty)
                Text('No firmware installed yet.', style: body(size: 12.5))
              else ...[
                Text(
                  'The device restarts into your choice. The app waits for it and picks up '
                  'whatever comes back.',
                  style: body(size: 12),
                ),
                const SizedBox(height: 14),
                for (final s in usable) ...[
                  Pill(
                    label: s.name,
                    height: 48,
                    enabled: !_busy && widget.session.isLive,
                    onTap: () => _boot(s),
                  ),
                  const SizedBox(height: 8),
                ],
              ],
            ],
          ),
        ),
      ],
    );
  }
}

// =========================================================================================
// macros
// =========================================================================================

class MacrosCard extends StatelessWidget {
  const MacrosCard({super.key, required this.session});

  final RemoteSession session;

  Future<void> _finishRecording(BuildContext context) async {
    final steps = session.stopRecording();
    if (steps == null) return;
    if (!context.mounted) return;

    final name = await showDialog<String>(
      context: context,
      builder: (context) => _NameMacroSheet(count: steps.length),
    );
    if (name == null || name.isEmpty) return;
    session.addMacro(Macro(name, steps));
  }

  @override
  Widget build(BuildContext context) {
    final recording = session.isRecording;
    final playing = session.playing;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Label('Shortcuts', trailing: recording ? 'recording' : '${session.macros.length}'),
        CreamCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              if (session.macros.isEmpty && !recording)
                Text(
                  'Record the taps you keep repeating — the way into the WiFi menu, say — '
                  'and replay them with one button.',
                  style: body(size: 12),
                ),

              for (final m in session.macros)
                _MacroRow(
                  macro: m,
                  running: identical(playing, m),
                  progress: identical(playing, m) ? session.playedSteps : 0,
                  enabled: session.isLive && playing == null && !recording,
                  onRun: () => session.runMacro(m),
                  onDelete: () => session.removeMacro(m),
                ),

              const SizedBox(height: 12),
              if (recording)
                Pill(
                  label: session.recordedSteps == 0
                      ? 'Press some keys, then stop'
                      : 'Stop — ${session.recordedSteps} step${session.recordedSteps == 1 ? '' : 's'}',
                  height: 46,
                  onTap: () => _finishRecording(context),
                )
              else
                Pill(
                  label: 'Record a shortcut',
                  height: 46,
                  quiet: session.macros.isNotEmpty,
                  enabled: session.isLive && playing == null,
                  onTap: session.startRecording,
                ),
              if (recording) ...[
                const SizedBox(height: 10),
                Text(
                  'Everything you press on the pad above is being recorded.',
                  style: body(size: 11.5),
                ),
              ],
            ],
          ),
        ),
      ],
    );
  }
}

class _MacroRow extends StatelessWidget {
  const _MacroRow({
    required this.macro,
    required this.running,
    required this.progress,
    required this.enabled,
    required this.onRun,
    required this.onDelete,
  });

  final Macro macro;
  final bool running;
  final int progress;
  final bool enabled;
  final VoidCallback onRun;
  final VoidCallback onDelete;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 8),
    child: Material(
      color: running ? D.ink : D.creamDeep,
      borderRadius: BorderRadius.circular(D.rInner),
      child: InkWell(
        onTap: enabled ? onRun : null,
        borderRadius: BorderRadius.circular(D.rInner),
        child: Padding(
          padding: const EdgeInsets.fromLTRB(15, 12, 6, 12),
          child: Row(
            children: [
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      macro.name,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        fontSize: 14.5,
                        fontWeight: FontWeight.w600,
                        letterSpacing: -0.2,
                        color: running ? D.cream : D.ink,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      running
                          ? 'playing … $progress / ${macro.steps.length}'
                          : macro.summary,
                      style: body(
                        size: 11.5,
                        color: running ? const Color(0xFF9A958C) : D.inkSoft,
                      ),
                    ),
                  ],
                ),
              ),
              if (!running)
                IconButton(
                  onPressed: onDelete,
                  icon: Icon(Icons.close, size: 18, color: D.inkSoft),
                  visualDensity: VisualDensity.compact,
                ),
            ],
          ),
        ),
      ),
    ),
  );
}

class _NameMacroSheet extends StatefulWidget {
  const _NameMacroSheet({required this.count});

  final int count;

  @override
  State<_NameMacroSheet> createState() => _NameMacroSheetState();
}

class _NameMacroSheetState extends State<_NameMacroSheet> {
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
          Text('Name it', style: display(size: 19)),
          const SizedBox(height: 4),
          Text(
            '${widget.count} step${widget.count == 1 ? '' : 's'} recorded',
            style: serifSub(size: 13),
          ),
          const SizedBox(height: 14),
          SoftField(
            controller: _c,
            hint: 'WiFi menu',
            onSubmitted: (v) => Navigator.of(context).pop(v.trim()),
          ),
          const SizedBox(height: 14),
          Row(
            children: [
              Expanded(
                child: Pill(
                  label: 'Discard',
                  height: 46,
                  quiet: true,
                  onTap: () => Navigator.of(context).pop(),
                ),
              ),
              const SizedBox(width: 9),
              Expanded(
                child: Pill(
                  label: 'Save',
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
