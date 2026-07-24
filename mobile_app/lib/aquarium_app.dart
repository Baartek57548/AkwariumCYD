import 'dart:async';

import 'package:flutter/material.dart';

import 'app_update/app_update_controller.dart';
import 'app_update/app_update_models.dart';
import 'app_update/app_update_ui.dart';
import 'connection_home_page.dart';
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
    AppSettings.languageNotifier.addListener(_onSettingsChanged);
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
    AppSettings.languageNotifier.removeListener(_onSettingsChanged);
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
          content: Text(message),
          backgroundColor: isError ? colors.error : null,
          behavior: SnackBarBehavior.floating,
        ),
      );
  }

  @override
  Widget build(BuildContext context) {
    const seed = Color(0xFF33A6B8);
    return DisplayRefreshRateScope(
      profile: refreshRateController.profile,
      state: refreshRateController.state,
      child: MaterialApp(
        navigatorKey: _navigatorKey,
        scaffoldMessengerKey: _scaffoldMessengerKey,
        title: widget.title,
        debugShowCheckedModeBanner: false,
        theme: _theme(seed, Brightness.light),
        darkTheme: _theme(seed, Brightness.dark),
        themeMode: AppSettings.themeModeNotifier.value,
        locale: AppSettings.languageNotifier.value,
        builder: (context, child) {
          final controller = _appUpdateController;
          if (controller == null) return child ?? const SizedBox.shrink();
          return AppUpdateScope(
            controller: controller,
            child: child ?? const SizedBox.shrink(),
          );
        },
        home: widget.home ?? const ConnectionHomePage(),
      ),
    );
  }

  ThemeData _theme(Color seed, Brightness brightness) {
    final dark = brightness == Brightness.dark;
    const borderRadius = BorderRadius.all(Radius.circular(5));
    const componentShape = RoundedRectangleBorder(borderRadius: borderRadius);
    final scheme = ColorScheme.fromSeed(
      seedColor: seed,
      brightness: brightness,
      surface: dark ? const Color(0xFF101112) : const Color(0xFFF7F8F8),
    );
    return ThemeData(
      colorScheme: scheme,
      scaffoldBackgroundColor: dark
          ? const Color(0xFF080909)
          : const Color(0xFFF1F4F4),
      useMaterial3: true,
      materialTapTargetSize: MaterialTapTargetSize.padded,
      cardTheme: CardThemeData(
        elevation: 0,
        color: dark ? const Color(0xFF151616) : Colors.white,
        margin: EdgeInsets.zero,
        shape: RoundedRectangleBorder(
          borderRadius: borderRadius,
          side: BorderSide(
            color: dark ? const Color(0xFF272929) : const Color(0xFFE1E6E6),
          ),
        ),
      ),
      appBarTheme: AppBarTheme(
        elevation: 0,
        scrolledUnderElevation: 0,
        centerTitle: false,
        backgroundColor: dark ? const Color(0xFF080909) : Colors.white,
        surfaceTintColor: Colors.transparent,
      ),
      navigationBarTheme: NavigationBarThemeData(
        elevation: 0,
        height: 72,
        backgroundColor: dark ? const Color(0xFF121313) : Colors.white,
        indicatorColor: scheme.primaryContainer,
        indicatorShape: componentShape,
        labelTextStyle: WidgetStateProperty.resolveWith((states) {
          return TextStyle(
            fontSize: 12,
            fontWeight: states.contains(WidgetState.selected)
                ? FontWeight.w700
                : FontWeight.w500,
          );
        }),
      ),
      dividerTheme: DividerThemeData(
        color: dark ? const Color(0xFF272929) : const Color(0xFFE1E6E6),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: dark ? const Color(0xFF111212) : Colors.white,
        border: const OutlineInputBorder(borderRadius: borderRadius),
      ),
      filledButtonTheme: const FilledButtonThemeData(
        style: ButtonStyle(
          minimumSize: WidgetStatePropertyAll(Size(64, 48)),
          shape: WidgetStatePropertyAll(componentShape),
        ),
      ),
      elevatedButtonTheme: const ElevatedButtonThemeData(
        style: ButtonStyle(
          minimumSize: WidgetStatePropertyAll(Size(64, 48)),
          shape: WidgetStatePropertyAll(componentShape),
        ),
      ),
      outlinedButtonTheme: const OutlinedButtonThemeData(
        style: ButtonStyle(
          minimumSize: WidgetStatePropertyAll(Size(64, 48)),
          shape: WidgetStatePropertyAll(componentShape),
        ),
      ),
      textButtonTheme: const TextButtonThemeData(
        style: ButtonStyle(
          minimumSize: WidgetStatePropertyAll(Size(64, 48)),
          shape: WidgetStatePropertyAll(componentShape),
        ),
      ),
      iconButtonTheme: const IconButtonThemeData(
        style: ButtonStyle(
          minimumSize: WidgetStatePropertyAll(Size.square(48)),
          shape: WidgetStatePropertyAll(componentShape),
        ),
      ),
      floatingActionButtonTheme: const FloatingActionButtonThemeData(
        shape: componentShape,
      ),
      chipTheme: ChipThemeData(shape: componentShape),
      dialogTheme: const DialogThemeData(shape: componentShape),
      bottomSheetTheme: const BottomSheetThemeData(shape: componentShape),
      popupMenuTheme: const PopupMenuThemeData(shape: componentShape),
      snackBarTheme: const SnackBarThemeData(shape: componentShape),
      navigationRailTheme: const NavigationRailThemeData(
        indicatorShape: componentShape,
      ),
      segmentedButtonTheme: const SegmentedButtonThemeData(
        style: ButtonStyle(shape: WidgetStatePropertyAll(componentShape)),
      ),
      menuTheme: const MenuThemeData(
        style: MenuStyle(shape: WidgetStatePropertyAll(componentShape)),
      ),
      dropdownMenuTheme: const DropdownMenuThemeData(
        menuStyle: MenuStyle(shape: WidgetStatePropertyAll(componentShape)),
      ),
      datePickerTheme: const DatePickerThemeData(shape: componentShape),
      timePickerTheme: const TimePickerThemeData(shape: componentShape),
      searchBarTheme: const SearchBarThemeData(
        shape: WidgetStatePropertyAll(componentShape),
      ),
      tooltipTheme: TooltipThemeData(
        decoration: BoxDecoration(
          color: scheme.inverseSurface,
          borderRadius: borderRadius,
        ),
        textStyle: TextStyle(color: scheme.onInverseSurface),
      ),
    );
  }
}
