import 'package:flutter/material.dart';

import 'connection_home_page.dart';
import 'display_refresh_rate.dart';
import 'app_settings.dart';

class AquariumApp extends StatefulWidget {
  const AquariumApp({super.key, this.title = 'AquaCYD Control', this.home});

  final String title;
  final Widget? home;

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
    AppSettings.themeModeNotifier.addListener(_onSettingsChanged);
    AppSettings.languageNotifier.addListener(_onSettingsChanged);
  }

  void _onRefreshRateChanged() {
    if (mounted) setState(() {});
  }

  void _onSettingsChanged() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    refreshRateController
      ..removeListener(_onRefreshRateChanged)
      ..dispose();
    AppSettings.themeModeNotifier.removeListener(_onSettingsChanged);
    AppSettings.languageNotifier.removeListener(_onSettingsChanged);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    const seed = Color(0xFF33A6B8);
    return DisplayRefreshRateScope(
      profile: refreshRateController.profile,
      state: refreshRateController.state,
      child: MaterialApp(
        title: widget.title,
        debugShowCheckedModeBanner: false,
        theme: _theme(seed, Brightness.light),
        darkTheme: _theme(seed, Brightness.dark),
        themeMode: AppSettings.themeModeNotifier.value,
        locale: AppSettings.languageNotifier.value,
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
        style: ButtonStyle(shape: WidgetStatePropertyAll(componentShape)),
      ),
      elevatedButtonTheme: const ElevatedButtonThemeData(
        style: ButtonStyle(shape: WidgetStatePropertyAll(componentShape)),
      ),
      outlinedButtonTheme: const OutlinedButtonThemeData(
        style: ButtonStyle(shape: WidgetStatePropertyAll(componentShape)),
      ),
      textButtonTheme: const TextButtonThemeData(
        style: ButtonStyle(shape: WidgetStatePropertyAll(componentShape)),
      ),
      iconButtonTheme: const IconButtonThemeData(
        style: ButtonStyle(shape: WidgetStatePropertyAll(componentShape)),
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
