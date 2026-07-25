import 'package:flutter/material.dart';

import 'app_settings.dart';
import 'aquarium_app.dart';
import 'full_controller/controller_session.dart';
import 'full_controller/controller_shell.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await AppSettings.init();
  runApp(
    AquariumApp(
      title: 'AquaCYD DEV',
      home: ControllerShell(session: ControllerSession.development()),
    ),
  );
}
