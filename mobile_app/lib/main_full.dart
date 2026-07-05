import 'package:flutter/material.dart';

import 'aquarium_app.dart';
import 'connection_home_page.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
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
