import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import 'alarm_center/background_sync.dart';
import 'app_settings.dart';
import 'aquarium_app.dart';
import 'controller_bootstrap_page.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  try {
    await AquariumBackgroundService.initialize();
  } on Object {
    // Aplikacja i alarmy pierwszoplanowe muszą działać nawet wtedy, gdy
    // producent telefonu zablokował inicjalizację zadania okresowego.
  }
  await AppSettings.init();
  runApp(
    const AquariumApp(
      enableAppUpdates: kReleaseMode,
      home: ControllerBootstrapPage(enableRuntimeServices: true),
    ),
  );
}
