import 'package:flutter/material.dart';

import 'aquarium_app.dart';
import 'full_controller/controller_session.dart';
import 'full_controller/controller_shell.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(
    AquariumApp(
      title: 'AquaCYD DEV',
      home: ControllerShell(session: ControllerSession.development()),
    ),
  );
}
