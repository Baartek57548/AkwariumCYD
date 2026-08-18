import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

import '../aquahub/app_update.dart';
import '../aquahub/credentials_store.dart';
import '../aquahub/hub_discovery.dart';
import '../aquahub/setup_page.dart';
import '../data/credentials_store.dart';
import '../design/app_theme.dart';
import '../design/components.dart';
import 'biometric_gate.dart';
import 'controller.dart';
import 'onboarding.dart';
import 'preferences.dart';
import 'shell.dart';
import 'snapshot_cache.dart';
import 'strings.dart';

final class HomeControlApp extends StatefulWidget {
  const HomeControlApp({
    this.preferences,
    this.hubCredentialsStore,
    this.homeAssistantCredentialsStore,
    this.discoveryService,
    this.appUpdateService,
    this.snapshotCache,
    this.biometricAuthenticator,
    this.homeAssistantSourceFactory,
    this.enablePolling = true,
    super.key,
  });

  final HomeControlPreferences? preferences;
  final HubCredentialsStore? hubCredentialsStore;
  final CredentialsStore? homeAssistantCredentialsStore;
  final HubDiscoveryService? discoveryService;
  final AppUpdateService? appUpdateService;
  final HomeSnapshotCache? snapshotCache;
  final BiometricAuthenticator? biometricAuthenticator;
  final HomeAssistantSourceFactory? homeAssistantSourceFactory;
  final bool enablePolling;

  @override
  State<HomeControlApp> createState() => _HomeControlAppState();
}

final class _HomeControlAppState extends State<HomeControlApp>
    with WidgetsBindingObserver {
  final GlobalKey<NavigatorState> _navigatorKey = GlobalKey<NavigatorState>();
  late final HomeControlController controller;
  late final AppUpdateController appUpdateController;
  bool _updateDialogOpen = false;
  String? _promptedUpdateVersion;
  late ThemeMode _themeMode;
  late Locale _locale;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    controller = HomeControlController(
      preferences: widget.preferences ?? HomeControlPreferences(),
      hubCredentialsStore:
          widget.hubCredentialsStore ?? SecureHubCredentialsStore(),
      homeAssistantCredentialsStore:
          widget.homeAssistantCredentialsStore ?? SecureCredentialsStore(),
      snapshotCache: widget.snapshotCache ?? SecureHomeSnapshotCache(),
      biometricAuthenticator: widget.biometricAuthenticator,
      homeAssistantSourceFactory: widget.homeAssistantSourceFactory,
      enablePolling: widget.enablePolling,
    );
    _themeMode = controller.themeMode;
    _locale = controller.locale;
    controller.addListener(_handleAppConfiguration);
    appUpdateController = AppUpdateController(
      service: widget.appUpdateService ?? createDefaultAppUpdateService(),
    )..addListener(_handleAppUpdateState);
    controller.initialize();
    appUpdateController.initialize();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    final active = state == AppLifecycleState.resumed;
    controller.setAppActive(active);
    if (active) appUpdateController.onAppResumed();
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    appUpdateController
      ..removeListener(_handleAppUpdateState)
      ..dispose();
    controller.removeListener(_handleAppConfiguration);
    controller.dispose();
    super.dispose();
  }

  void _handleAppUpdateState() {
    final release = appUpdateController.release;
    if (!mounted ||
        release == null ||
        appUpdateController.phase != AppUpdatePhase.available ||
        _updateDialogOpen ||
        _promptedUpdateVersion == release.version) {
      return;
    }
    _promptedUpdateVersion = release.version;
    _updateDialogOpen = true;
    WidgetsBinding.instance.addPostFrameCallback((_) async {
      final context = _navigatorKey.currentContext;
      if (!mounted || context == null) {
        _updateDialogOpen = false;
        return;
      }
      await showDialog<void>(
        context: context,
        barrierDismissible: false,
        builder: (_) => AppUpdateDialog(
          controller: appUpdateController,
          authorizeInstall: controller.authorizeCriticalOperation,
        ),
      );
      _updateDialogOpen = false;
    });
  }

  void _handleAppConfiguration() {
    if (!mounted ||
        (_themeMode == controller.themeMode && _locale == controller.locale)) {
      return;
    }
    setState(() {
      _themeMode = controller.themeMode;
      _locale = controller.locale;
    });
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      navigatorKey: _navigatorKey,
      debugShowCheckedModeBanner: false,
      title: 'Home Control',
      theme: AquaTheme.light(),
      darkTheme: AquaTheme.dark(),
      themeMode: _themeMode,
      locale: _locale,
      supportedLocales: const <Locale>[Locale('pl'), Locale('en')],
      localizationsDelegates: const <LocalizationsDelegate<Object>>[
        HomeControlStrings.delegate,
        GlobalMaterialLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
      ],
      builder: (context, child) => AppUpdateScope(
        controller: appUpdateController,
        child: child ?? const SizedBox.shrink(),
      ),
      home: AnimatedBuilder(
        animation: controller,
        builder: (context, _) => _buildHome(),
      ),
    );
  }

  Widget _buildHome() => switch (controller.phase) {
    HomeControlPhase.booting => const _HomeLoading(keyName: 'booting'),
    HomeControlPhase.connecting => const _HomeLoading(keyName: 'connecting'),
    HomeControlPhase.onboarding => HomeControlOnboarding(
      controller: controller,
    ),
    HomeControlPhase.aquaHubSetup => _AquaHubSetupHost(
      controller: controller,
      discoveryService: widget.discoveryService ?? NativeHubDiscoveryService(),
    ),
    HomeControlPhase.ready => HomeControlShell(controller: controller),
    HomeControlPhase.failure => _HomeFailure(controller: controller),
  };
}

final class _HomeLoading extends StatelessWidget {
  const _HomeLoading({required this.keyName});

  final String keyName;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    return Scaffold(
      body: _ScrollableStateFrame(
        child: HomeLoadingState(
          leading: const HomeControlMark(size: 88),
          title: strings.t('appName'),
          message: strings.t(keyName),
        ),
      ),
    );
  }
}

final class _AquaHubSetupHost extends StatelessWidget {
  const _AquaHubSetupHost({
    required this.controller,
    required this.discoveryService,
  });

  final HomeControlController controller;
  final HubDiscoveryService discoveryService;

  @override
  Widget build(BuildContext context) {
    final hub = controller.hubSetupController;
    if (hub == null) return const _HomeLoading(keyName: 'connecting');
    return HubSetupPage(
      controller: hub,
      discoveryService: discoveryService,
      onBack: controller.cancelSetup,
    );
  }
}

final class _HomeFailure extends StatelessWidget {
  const _HomeFailure({required this.controller});

  final HomeControlController controller;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final failure = controller.failure;
    return Scaffold(
      body: _ScrollableStateFrame(
        child: HomeErrorState(
          title: strings.t('errorTitle'),
          message: strings.t(failure?.messageKey ?? 'errorUnknown'),
          retryLabel: strings.t('retry'),
          onRetry: controller.retry,
          secondaryActionLabel: strings.t('reconfigure'),
          onSecondaryAction: controller.switchSource,
        ),
      ),
    );
  }
}

final class _ScrollableStateFrame extends StatelessWidget {
  const _ScrollableStateFrame({required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) => SafeArea(
    child: LayoutBuilder(
      builder: (context, constraints) => SingleChildScrollView(
        child: ConstrainedBox(
          constraints: BoxConstraints(minHeight: constraints.maxHeight),
          child: child,
        ),
      ),
    ),
  );
}
