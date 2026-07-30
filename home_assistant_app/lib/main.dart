import 'package:flutter/material.dart';

import 'src/app.dart';
import 'src/data/credentials_store.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(AquaCydHomeApp(credentialsStore: SecureCredentialsStore()));
}
