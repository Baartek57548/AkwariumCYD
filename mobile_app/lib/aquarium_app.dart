import 'package:flutter/material.dart';

import 'connection_home_page.dart';
import 'display_refresh_rate.dart';

class AquariumApp extends StatefulWidget {
  const AquariumApp({super.key});

  @override
  State<AquariumApp> createState() => _AquariumAppState();
}

class _AquariumAppState extends State<AquariumApp> {
  late final DisplayRefreshRateController refreshRateController;

  @override
  void initState() {
    super.initState();
    refreshRateController = DisplayRefreshRateController()
      ..addListener(_onRefreshRateChanged);
  }

  void _onRefreshRateChanged() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    refreshRateController
      ..removeListener(_onRefreshRateChanged)
      ..dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    const seed = Color(0xFF0891B2);
    return DisplayRefreshRateScope(
      profile: refreshRateController.profile,
      child: MaterialApp(
        title: 'cydAkwarium',
        debugShowCheckedModeBanner: false,
        theme: ThemeData(
          colorScheme: ColorScheme.fromSeed(seedColor: seed),
          scaffoldBackgroundColor: const Color(0xFFF4FAFC),
          useMaterial3: true,
        ),
        darkTheme: ThemeData(
          colorScheme: ColorScheme.fromSeed(
            seedColor: seed,
            brightness: Brightness.dark,
          ),
          useMaterial3: true,
        ),
        themeMode: ThemeMode.system,
        home: const ConnectionHomePage(),
      ),
    );
  }
}
