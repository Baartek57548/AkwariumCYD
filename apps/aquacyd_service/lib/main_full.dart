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
    await AquariumBackgroundService.initialize();
  } on Object {
    // Synchronizacja okresowa jest dodatkiem; interfejs nie może od niej zależeć.
  }
  await AppSettings.init();
  runApp(
    const AquariumApp(
      title: 'AquaCYD Full',
      home: ControllerBootstrapPage(
        brandName: 'AquaCYD Full',
        showDevelopment: false,
        showLegacyWebView: false,
        enableRuntimeServices: true,
      ),
    ),
  );
}
