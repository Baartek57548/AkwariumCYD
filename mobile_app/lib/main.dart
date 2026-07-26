import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'app_settings.dart';
import 'aquarium_app.dart';
import 'controller_bootstrap_page.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await AppSettings.init();
  runApp(
    const AquariumApp(
      enableAppUpdates: kReleaseMode,
      home: ControllerBootstrapPage(),
    ),
  );
}
