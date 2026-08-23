import 'package:flutter/material.dart';

import 'app_settings.dart';
import 'aquarium_app.dart';
import 'full_controller/controller_session.dart';
import 'full_controller/controller_shell.dart';
import 'remote_gateway/remote_push.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  RemotePushBootstrap.configureBackgroundHandling();
  await AppSettings.init();
  runApp(
    AquariumApp(
      title: 'AquaCYD DEV',
      home: ControllerShell(session: ControllerSession.development()),
    ),
  );
}
