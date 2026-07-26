import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

import 'app_update/app_update_controller.dart';
import 'app_update/app_update_models.dart';
import 'app_update/app_update_ui.dart';
import 'connection_home_page.dart';
import 'design_system.dart';
import 'display_refresh_rate.dart';
import 'app_settings.dart';

class AquariumApp extends StatefulWidget {
  const AquariumApp({
    super.key,
    this.title = 'AquaCYD Control',
    this.home,
    this.enableAppUpdates = false,
  });

  final String title;
  final Widget? home;
  final bool enableAppUpdates;

  @override
  State<AquariumApp> createState() => _AquariumAppState();
}

class _AquariumAppState extends State<AquariumApp> with WidgetsBindingObserver {
  late final DisplayRefreshRateController refreshRateController;
  final _navigatorKey = GlobalKey<NavigatorState>();
  final _scaffoldMessengerKey = GlobalKey<ScaffoldMessengerState>();
  AppUpdateController? _appUpdateController;
  String? _lastPromptedVersion;
  int _lastFeedbackEventId = -1;
  bool _promptOpen = false;
  bool _progressDialogOpen = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    refreshRateController = DisplayRefreshRateController()
      ..addListener(_onRefreshRateChanged);
    AppSettings.themeModeNotifier.addListener(_onSettingsChanged);
    if (widget.enableAppUpdates) {
      _appUpdateController = AppUpdateController()
        ..addListener(_onAppUpdateChanged);
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) unawaited(_appUpdateController!.start());
      });
    }
  }

  void _onRefreshRateChanged() {
    if (mounted) setState(() {});
  }

  void _onSettingsChanged() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    refreshRateController
      ..removeListener(_onRefreshRateChanged)
      ..dispose();
    AppSettings.themeModeNotifier.removeListener(_onSettingsChanged);
    _appUpdateController
      ?..removeListener(_onAppUpdateChanged)
      ..dispose();
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      final controller = _appUpdateController;
      if (controller != null) unawaited(controller.onAppResumed());
    } else {
      _appUpdateController?.onAppBackgrounded();
    }
  }

  void _onAppUpdateChanged() {
    if (!mounted) return;
    final controller = _appUpdateController;
    if (controller == null) return;
    final state = controller.state;

    if (state.phase == AppUpdatePhase.idle && state.release != null) {
      _lastPromptedVersion = null;
    }

    if (state.phase == AppUpdatePhase.available && state.release != null) {
      final version = state.release!.version.toString();
      if (!_promptOpen && (state.isManual || _lastPromptedVersion != version)) {
        _lastPromptedVersion = version;
        WidgetsBinding.instance.addPostFrameCallback((_) {
          if (mounted) unawaited(_showUpdatePrompt(state.release!));
        });
      }
      return;
    }

    if (state.eventId == _lastFeedbackEventId || !state.isManual) return;
    if (state.phase == AppUpdatePhase.upToDate) {
      _lastFeedbackEventId = state.eventId;
      _showSnackBar('Masz najnowszą wersję AquaCYD.');
    } else if (state.phase == AppUpdatePhase.failed && !_progressDialogOpen) {
      _lastFeedbackEventId = state.eventId;
      _showSnackBar(
        state.message ?? 'Nie udało się sprawdzić aktualizacji.',
        isError: true,
      );
    }
  }

  Future<void> _showUpdatePrompt(AppRelease release) async {
    if (_promptOpen || !mounted) return;
    final context = _navigatorKey.currentContext;
    if (context == null) return;
    _promptOpen = true;
    final action = await showDialog<AppUpdatePromptAction>(
      context: context,
      barrierDismissible: false,
      builder: (_) => AppUpdatePromptDialog(release: release),
    );
    _promptOpen = false;
    if (!mounted || action == null) return;
    final controller = _appUpdateController;
    if (controller == null) return;
    switch (action) {
      case AppUpdatePromptAction.install:
        await _showProgressDialogAndStart(controller);
      case AppUpdatePromptAction.remindLater:
        await controller.remindLater();
      case AppUpdatePromptAction.skip:
        await controller.skipCurrentVersion();
    }
  }

  Future<void> _showProgressDialogAndStart(
    AppUpdateController controller,
  ) async {
    if (_progressDialogOpen || !mounted) return;
    final context = _navigatorKey.currentContext;
    if (context == null) return;
    _progressDialogOpen = true;
    final dialog = showDialog<void>(
      context: context,
      barrierDismissible: false,
      builder: (_) => AppUpdateProgressDialog(controller: controller),
    ).whenComplete(() => _progressDialogOpen = false);
    unawaited(controller.downloadAndInstall());
    await dialog;
  }

  void _showSnackBar(String message, {bool isError = false}) {
    final messenger = _scaffoldMessengerKey.currentState;
    final navigatorContext = _navigatorKey.currentContext;
    if (messenger == null || navigatorContext == null) return;
    final colors = Theme.of(navigatorContext).colorScheme;
    messenger
      ..hideCurrentSnackBar()
      ..showSnackBar(
        SnackBar(
          content: Text(
            message,
            style: isError ? TextStyle(color: colors.onError) : null,
          ),
          backgroundColor: isError ? colors.error : null,
          behavior: SnackBarBehavior.floating,
          showCloseIcon: true,
          closeIconColor: isError ? colors.onError : null,
        ),
      );
  }

  @override
  Widget build(BuildContext context) {
    return DisplayRefreshRateScope(
      profile: refreshRateController.profile,
      state: refreshRateController.state,
      child: MaterialApp(
        navigatorKey: _navigatorKey,
        scaffoldMessengerKey: _scaffoldMessengerKey,
        title: widget.title,
        debugShowCheckedModeBanner: false,
        theme: AquaTheme.light(),
        darkTheme: AquaTheme.dark(),
        themeMode: AppSettings.themeModeNotifier.value,
        locale: const Locale('pl'),
        supportedLocales: const [Locale('pl')],
        localizationsDelegates: const [
          GlobalMaterialLocalizations.delegate,
          GlobalWidgetsLocalizations.delegate,
          GlobalCupertinoLocalizations.delegate,
        ],
        builder: (context, child) {
          final theme = Theme.of(context);
          final textScale = MediaQuery.textScalerOf(context).scale(1);
          final accessibilityGrowth = (textScale - 1).clamp(0.0, 2.0);
          final navigationBarHeight = (72 + accessibilityGrowth * 12).clamp(
            72.0,
            96.0,
          );
          Widget content = Theme(
            data: theme.copyWith(
              navigationBarTheme: theme.navigationBarTheme.copyWith(
                height: navigationBarHeight,
              ),
            ),
            child: child ?? const SizedBox.shrink(),
          );
          final controller = _appUpdateController;
          if (controller != null) {
            content = AppUpdateScope(controller: controller, child: content);
          }
          return content;
        },
        home: widget.home ?? const ConnectionHomePage(),
      ),
    );
  }
}
