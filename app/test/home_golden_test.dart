/// Captures the whole page to test/goldens/home.png so the layout can be reviewed without a
/// phone. Run with `flutter test --update-goldens test/home_golden_test.dart`.
///
/// The test font renders every glyph as a filled box, which makes a screenshot useless for
/// judging a text-heavy design — so a real monospace face is loaded first when one is
/// available on the machine.
library;

import 'dart:io';


import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tembed_remote/main.dart';

/// flutter_test renders every glyph as a filled box unless real fonts are registered, which
/// makes a screenshot useless for judging a design. Load whatever the host machine has.
Future<void> _loadFonts() async {
  const faces = {
    // family in the app -> candidates on disk, first hit wins
    'Roboto': [
      r'C:\Windows\Fonts\segoeui.ttf',
      '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
      '/System/Library/Fonts/Helvetica.ttc',
    ],
    'serif': [
      r'C:\Windows\Fonts\georgiai.ttf',
      r'C:\Windows\Fonts\georgia.ttf',
      '/usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf',
      '/System/Library/Fonts/Times.ttc',
    ],
  };

  for (final entry in faces.entries) {
    for (final path in entry.value) {
      final file = File(path);
      if (!file.existsSync()) continue;
      final bytes = file.readAsBytesSync();
      final loader = FontLoader(entry.key)
        ..addFont(Future.value(ByteData.sublistView(bytes)));
      await loader.load();
      break;
    }
  }
}

void main() {
  testWidgets('home page renders', (tester) async {
    await _loadFonts();
    tester.view
      ..physicalSize = const Size(390 * 3, 1500 * 3)
      ..devicePixelRatio = 3.0;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(const TEmbedRemoteApp());
    await tester.pump(const Duration(milliseconds: 100));
    await tester.pump(const Duration(milliseconds: 100));
    // The sleeping cat stays blank here: flutter_test never runs the real image decoder, so
    // the tube comes out empty. That the GIF is bundled and decodable is checked directly in
    // widget_test.dart instead.

    await expectLater(
      find.byType(TEmbedRemoteApp),
      matchesGoldenFile('goldens/home.png'),
    );
  });
}
