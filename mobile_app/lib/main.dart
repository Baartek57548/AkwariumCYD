import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import 'alarm_center/background_sync.dart';
import 'app_settings.dart';
import 'aquarium_app.dart';
import 'controller_bootstrap_page.dart';
import 'remote_gateway/remote_push.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  RemotePushBootstrap.configureBackgroundHandling();

  try {
    await AppSettings.init().timeout(const Duration(seconds: 5));
  } on Object {
    // Ustawienia lokalne nie mogą zatrzymać pierwszej klatki po uszkodzeniu
    // magazynu systemowego; bezpieczne wartości domyślne są już w notifierach.
  }

  runApp(
    const AquariumApp(
      enableAppUpdates: kReleaseMode,
      home: ControllerBootstrapPage(enableRuntimeServices: true),
    ),
  );

  unawaited(_initializeBackgroundServices());
}

Future<void> _initializeBackgroundServices() async {
  try {
    await AquariumBackgroundService.initialize().timeout(
      const Duration(seconds: 15),
    );
  } on Object {
    // Aplikacja i alarmy pierwszoplanowe muszą działać nawet wtedy, gdy
    // producent telefonu zablokował inicjalizację zadania okresowego.
  }
}
