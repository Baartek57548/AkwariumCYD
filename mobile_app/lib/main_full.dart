import 'package:flutter/material.dart';

import 'app_settings.dart';
import 'aquarium_app.dart';
import 'connection_home_page.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await AppSettings.init();
  runApp(
    const AquariumApp(
      title: 'AquaCYD Full',
      home: ConnectionHomePage(
        brandName: 'AquaCYD Full',
        showDevelopment: false,
        showLegacyWebView: false,
      ),
    ),
  );
}
