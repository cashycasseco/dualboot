/// The look: near-black ground, one warm cream card carrying the controls, generous radii,
/// and almost no colour. Status is a small dot rather than a neon accent, so the screen you
/// are mirroring stays the brightest thing on the page.
library;

import 'package:flutter/material.dart';

abstract final class D {
  static const bg = Color(0xFF0F0F0F);
  static const surface = Color(0xFF191919); // the dark card the mirror sits in
  static const surfaceHi = Color(0xFF232323);

  static const cream = Color(0xFFEFEAE1);
  static const creamDeep = Color(0xFFE2DCD1); // chips and wells on the cream card
  static const ink = Color(0xFF16150F); // text on cream
  static const inkSoft = Color(0xFF8B857C);

  static const snow = Color(0xFFF5F3EF); // text on dark
  static const snowSoft = Color(0xFF7E7C78);

  static const live = Color(0xFF59A06B);
  static const warn = Color(0xFFC4643F);

  static const rCard = 30.0;
  static const rInner = 22.0;
  static const rChip = 16.0;
}

/// Display line — the product name, tight and confident.
TextStyle display({double size = 23, Color color = D.ink}) => TextStyle(
  fontSize: size,
  height: 1.15,
  fontWeight: FontWeight.w600,
  letterSpacing: -0.4,
  color: color,
);

/// The italic serif under it. Carries the address the way a coffee bag carries the varietal.
TextStyle serifSub({double size = 14, Color color = D.inkSoft}) => TextStyle(
  fontFamily: 'serif',
  fontFamilyFallback: const ['Noto Serif', 'Georgia', 'Times New Roman'],
  fontStyle: FontStyle.italic,
  fontSize: size,
  height: 1.3,
  color: color,
);

/// Small-caps label, for section headers and chip captions.
TextStyle micro({double size = 10, Color color = D.snowSoft, FontWeight w = FontWeight.w500}) =>
    TextStyle(fontSize: size, height: 1.3, fontWeight: w, letterSpacing: 1.5, color: color);

TextStyle body({double size = 13, Color color = D.inkSoft, FontWeight w = FontWeight.w400}) =>
    TextStyle(fontSize: size, height: 1.5, fontWeight: w, letterSpacing: 0, color: color);

ThemeData appTheme() {
  final base = ThemeData.dark(useMaterial3: true);
  return base.copyWith(
    scaffoldBackgroundColor: D.bg,
    colorScheme: base.colorScheme.copyWith(
      primary: D.cream,
      surface: D.surface,
      error: D.warn,
    ),
    splashFactory: NoSplash.splashFactory,
    highlightColor: Colors.transparent,
  );
}

// ---------------------------------------------------------------------------------------
// containers
// ---------------------------------------------------------------------------------------

class DarkCard extends StatelessWidget {
  const DarkCard({super.key, required this.child, this.padding = const EdgeInsets.all(14)});

  final Widget child;
  final EdgeInsets padding;

  @override
  Widget build(BuildContext context) => Container(
    width: double.infinity,
    padding: padding,
    decoration: BoxDecoration(
      color: D.surface,
      borderRadius: BorderRadius.circular(D.rCard),
    ),
    child: child,
  );
}

class CreamCard extends StatelessWidget {
  const CreamCard({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.fromLTRB(20, 20, 20, 20),
  });

  final Widget child;
  final EdgeInsets padding;

  @override
  Widget build(BuildContext context) => Container(
    width: double.infinity,
    padding: padding,
    decoration: BoxDecoration(
      color: D.cream,
      borderRadius: BorderRadius.circular(D.rCard),
    ),
    child: child,
  );
}

/// Section label on the dark ground, between cards.
class Label extends StatelessWidget {
  const Label(this.text, {super.key, this.trailing});

  final String text;
  final String? trailing;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(6, 26, 6, 10),
    child: Row(
      children: [
        Text(text.toUpperCase(), style: micro()),
        const Spacer(),
        if (trailing != null)
          Text(trailing!.toUpperCase(), style: micro(color: const Color(0xFF565452))),
      ],
    ),
  );
}

// ---------------------------------------------------------------------------------------
// pieces
// ---------------------------------------------------------------------------------------

/// The little value/caption box — a reading, not a control.
class StatChip extends StatelessWidget {
  const StatChip({super.key, required this.value, required this.caption, this.onCream = true});

  final String value;
  final String caption;
  final bool onCream;

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.fromLTRB(13, 10, 13, 11),
    decoration: BoxDecoration(
      color: onCream ? D.creamDeep : D.surfaceHi,
      borderRadius: BorderRadius.circular(D.rChip),
    ),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          value,
          style: TextStyle(
            fontSize: 15,
            height: 1.1,
            fontWeight: FontWeight.w600,
            letterSpacing: -0.2,
            color: onCream ? D.ink : D.snow,
          ),
        ),
        const SizedBox(height: 3),
        Text(
          caption.toUpperCase(),
          style: micro(size: 8.5, color: onCream ? D.inkSoft : D.snowSoft),
        ),
      ],
    ),
  );
}

/// Full-width pill. Dark on the cream card, cream on the dark ground.
class Pill extends StatelessWidget {
  const Pill({
    super.key,
    required this.label,
    required this.onTap,
    this.dark = true,
    this.quiet = false,
    this.enabled = true,
    this.height = 52,
    this.leading,
  });

  final String label;
  final VoidCallback? onTap;
  final bool dark;

  /// A secondary action sitting *on* the cream card: same shape, no contrast grab.
  final bool quiet;
  final bool enabled;
  final double height;
  final Widget? leading;

  @override
  Widget build(BuildContext context) {
    final Color bg;
    final Color fg;
    if (quiet) {
      bg = D.creamDeep;
      fg = enabled ? D.ink : const Color(0xFFAEA89E);
    } else if (!enabled) {
      bg = dark ? const Color(0xFFCFC9BE) : D.surfaceHi;
      fg = dark ? const Color(0xFF8C877E) : D.snowSoft;
    } else {
      bg = dark ? D.ink : D.cream;
      fg = dark ? D.cream : D.ink;
    }

    return Opacity(
      opacity: enabled ? 1 : 0.85,
      child: Material(
        color: bg,
        borderRadius: BorderRadius.circular(100),
        child: InkWell(
          onTap: enabled ? onTap : null,
          borderRadius: BorderRadius.circular(100),
          child: SizedBox(
            height: height,
            child: Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                if (leading != null) ...[
                  IconTheme(data: IconThemeData(color: fg, size: 17), child: leading!),
                  const SizedBox(width: 9),
                ],
                Text(
                  label,
                  style: TextStyle(
                    fontSize: 13.5,
                    fontWeight: FontWeight.w600,
                    letterSpacing: 0.2,
                    color: fg,
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// A rounded control key. Softer than the old hard-edged one, still gives a press.
class SoftKey extends StatefulWidget {
  const SoftKey({
    super.key,
    required this.child,
    required this.onTap,
    this.enabled = true,
    this.height = 56,
    this.filled = false,
  });

  final Widget child;
  final VoidCallback? onTap;
  final bool enabled;
  final double height;

  /// Filled keys read as the primary one in a row (the OK in the middle).
  final bool filled;

  @override
  State<SoftKey> createState() => _SoftKeyState();
}

class _SoftKeyState extends State<SoftKey> {
  bool _down = false;

  @override
  Widget build(BuildContext context) {
    final on = widget.enabled;
    final pressed = _down && on;
    final Color bg;
    final Color fg;
    if (!on) {
      bg = const Color(0xFFE7E1D6);
      fg = const Color(0xFFB3ADA3);
    } else if (widget.filled || pressed) {
      bg = D.ink;
      fg = D.cream;
    } else {
      bg = D.creamDeep;
      fg = D.ink;
    }

    return GestureDetector(
      onTapDown: on ? (_) => setState(() => _down = true) : null,
      onTapUp: on ? (_) => setState(() => _down = false) : null,
      onTapCancel: on ? () => setState(() => _down = false) : null,
      onTap: on ? widget.onTap : null,
      child: AnimatedScale(
        scale: pressed ? 0.96 : 1,
        duration: const Duration(milliseconds: 90),
        curve: Curves.easeOut,
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 110),
          height: widget.height,
          decoration: BoxDecoration(
            color: bg,
            borderRadius: BorderRadius.circular(D.rInner),
          ),
          child: Center(
            child: DefaultTextStyle.merge(
              style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: fg),
              child: IconTheme(data: IconThemeData(color: fg, size: 22), child: widget.child),
            ),
          ),
        ),
      ),
    );
  }
}

/// Minimal toggle: a track and a dot, no chrome.
class SoftSwitch extends StatelessWidget {
  const SoftSwitch({
    super.key,
    required this.value,
    required this.onChanged,
    required this.title,
    this.note,
  });

  final bool value;
  final ValueChanged<bool> onChanged;
  final String title;
  final String? note;

  @override
  Widget build(BuildContext context) => GestureDetector(
    behavior: HitTestBehavior.opaque,
    onTap: () => onChanged(!value),
    child: Row(
      children: [
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                title,
                style: TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.w600,
                  letterSpacing: -0.1,
                  color: D.ink,
                ),
              ),
              if (note != null)
                Padding(
                  padding: const EdgeInsets.only(top: 3, right: 12),
                  child: Text(note!, style: body(size: 11.5)),
                ),
            ],
          ),
        ),
        AnimatedContainer(
          duration: const Duration(milliseconds: 140),
          curve: Curves.easeOut,
          width: 48,
          height: 28,
          padding: const EdgeInsets.all(3),
          alignment: value ? Alignment.centerRight : Alignment.centerLeft,
          decoration: BoxDecoration(
            color: value ? D.ink : const Color(0xFFD6D0C4),
            borderRadius: BorderRadius.circular(100),
          ),
          child: Container(
            width: 22,
            height: 22,
            decoration: BoxDecoration(
              color: value ? D.cream : const Color(0xFFF7F4EE),
              shape: BoxShape.circle,
            ),
          ),
        ),
      ],
    ),
  );
}

/// Small round button, the way the reference puts one in the top corner.
class RoundButton extends StatelessWidget {
  const RoundButton({super.key, required this.icon, required this.onTap, this.onDark = true});

  final IconData icon;
  final VoidCallback? onTap;
  final bool onDark;

  @override
  Widget build(BuildContext context) => Material(
    color: onDark ? D.surface : D.creamDeep,
    shape: const CircleBorder(),
    child: InkWell(
      customBorder: const CircleBorder(),
      onTap: onTap,
      child: SizedBox(
        width: 42,
        height: 42,
        child: Icon(icon, size: 19, color: onDark ? D.snow : D.ink),
      ),
    ),
  );
}

/// Text field that sits on the cream card without a box around it.
class SoftField extends StatelessWidget {
  const SoftField({
    super.key,
    required this.controller,
    required this.hint,
    this.obscure = false,
    this.keyboard,
    this.onSubmitted,
  });

  final TextEditingController controller;
  final String hint;
  final bool obscure;
  final TextInputType? keyboard;
  final ValueChanged<String>? onSubmitted;

  @override
  Widget build(BuildContext context) => TextField(
    controller: controller,
    obscureText: obscure,
    keyboardType: keyboard,
    autocorrect: false,
    enableSuggestions: false,
    onSubmitted: onSubmitted,
    style: TextStyle(fontSize: 14, color: D.ink, fontWeight: FontWeight.w500),
    cursorColor: D.ink,
    cursorWidth: 1.5,
    decoration: InputDecoration(
      isDense: true,
      contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 15),
      hintText: hint,
      hintStyle: body(size: 13.5, color: const Color(0xFFA9A399)),
      filled: true,
      fillColor: D.creamDeep,
      border: OutlineInputBorder(
        borderRadius: BorderRadius.circular(D.rChip),
        borderSide: BorderSide.none,
      ),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(D.rChip),
        borderSide: BorderSide.none,
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(D.rChip),
        borderSide: const BorderSide(color: D.ink, width: 1.4),
      ),
    ),
  );
}

/// The status dot used in the header and in lists.
class Dot extends StatelessWidget {
  const Dot({super.key, required this.color, this.size = 7});

  final Color color;
  final double size;

  @override
  Widget build(BuildContext context) => Container(
    width: size,
    height: size,
    decoration: BoxDecoration(color: color, shape: BoxShape.circle),
  );
}
