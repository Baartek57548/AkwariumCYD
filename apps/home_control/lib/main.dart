import 'package:flutter/material.dart';

import 'src/aquahub/app.dart';
import 'src/aquahub/credentials_store.dart';
import 'src/aquahub/demo.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  const demoMode = bool.fromEnvironment('AQUAHUB_DEMO');
  runApp(
    demoMode
        ? AquaHubApp(
            credentialsStore: DemoHubCredentialsStore(),
            apiFactory: createDemoHubApi,
            enablePolling: false,
          )
        : AquaHubApp(credentialsStore: SecureHubCredentialsStore()),
  );
}
