/// T-Embed Remote — screen mirror and remote control for the LilyGo T-Embed CC1101.
///
/// Speaks two protocols so one app covers the whole device:
///   * the dual-boot launcher, which serves raw RGB565 out of its PSRAM canvas, and
///   * stock Bruce, which serves the drawing commands its WebUI navigator uses.
///
/// Nothing here needs a patched Bruce.
library;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'src/session.dart';
import 'src/ui/home_page.dart';
import 'src/ui/design.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setSystemUIOverlayStyle(
    const SystemUiOverlayStyle(
      statusBarColor: Colors.transparent,
      statusBarIconBrightness: Brightness.light,
      systemNavigationBarColor: D.bg,
    ),
  );
  runApp(const TEmbedRemoteApp());
}

class TEmbedRemoteApp extends StatefulWidget {
  const TEmbedRemoteApp({super.key});

  @override
  State<TEmbedRemoteApp> createState() => _TEmbedRemoteAppState();
}

class _TEmbedRemoteAppState extends State<TEmbedRemoteApp> {
  final _session = RemoteSession();

  @override
  void dispose() {
    _session.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'T-Embed Remote',
    debugShowCheckedModeBanner: false,
    theme: appTheme(),
    home: HomePage(session: _session),
  );
}
