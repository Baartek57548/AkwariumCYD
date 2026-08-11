import 'package:flutter/material.dart';

import 'src/aquahub/app.dart';
import 'src/aquahub/credentials_store.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(AquaHubApp(credentialsStore: SecureHubCredentialsStore()));
}
