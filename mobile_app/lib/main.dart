import 'package:flutter/material.dart';
import 'app_settings.dart';
import 'aquarium_app.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await AppSettings.init();
  runApp(const AquariumApp());
}
