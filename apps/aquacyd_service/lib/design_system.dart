import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:aquacyd_design_system/aquacyd_design_system.dart';

/// Wspólne tokeny wizualne aplikacji. Interfejs sterownika korzysta z jednej
/// skali odstępów i promieni, dzięki czemu ekrany operacyjne i formularze
/// administracyjne zachowują tę samą hierarchię.
abstract final class AquaSpacing {
  static const double xxs = ProductSpacing.xxs;
  static const double xs = ProductSpacing.xs;
  static const double sm = ProductSpacing.sm;
  static const double md = ProductSpacing.md;
  static const double lg = ProductSpacing.lg;
  static const double xl = ProductSpacing.xl;
}

abstract final class AquaRadius {
  static const double control = ProductRadius.control;
  static const double card = ProductRadius.card;
  static const double hero = ProductRadius.hero;
}

@immutable
class AquaStatusColors extends ThemeExtension<AquaStatusColors> {
  const AquaStatusColors({
    required this.success,
    required this.onSuccess,
    required this.successContainer,
    required this.onSuccessContainer,
    required this.warning,
    required this.onWarning,
    required this.warningContainer,
    required this.onWarningContainer,
    required this.info,
    required this.onInfo,
    required this.infoContainer,
    required this.onInfoContainer,
  });

  final Color success;
  final Color onSuccess;
  final Color successContainer;
  final Color onSuccessContainer;
  final Color warning;
  final Color onWarning;
  final Color warningContainer;
  final Color onWarningContainer;
  final Color info;
  final Color onInfo;
  final Color infoContainer;
  final Color onInfoContainer;

  @override
  AquaStatusColors copyWith({
    Color? success,
    Color? onSuccess,
    Color? successContainer,
    Color? onSuccessContainer,
    Color? warning,
    Color? onWarning,
    Color? warningContainer,
    Color? onWarningContainer,
    Color? info,
    Color? onInfo,
    Color? infoContainer,
    Color? onInfoContainer,
  }) {
    return AquaStatusColors(
      success: success ?? this.success,
      onSuccess: onSuccess ?? this.onSuccess,
      successContainer: successContainer ?? this.successContainer,
      onSuccessContainer: onSuccessContainer ?? this.onSuccessContainer,
      warning: warning ?? this.warning,
      onWarning: onWarning ?? this.onWarning,
      warningContainer: warningContainer ?? this.warningContainer,
      onWarningContainer: onWarningContainer ?? this.onWarningContainer,
      info: info ?? this.info,
      onInfo: onInfo ?? this.onInfo,
      infoContainer: infoContainer ?? this.infoContainer,
      onInfoContainer: onInfoContainer ?? this.onInfoContainer,
    );
  }

  @override
  AquaStatusColors lerp(covariant AquaStatusColors? other, double t) {
    if (other == null) return this;
    return AquaStatusColors(
      success: Color.lerp(success, other.success, t)!,
      onSuccess: Color.lerp(onSuccess, other.onSuccess, t)!,
      successContainer: Color.lerp(
        successContainer,
        other.successContainer,
        t,
      )!,
      onSuccessContainer: Color.lerp(
        onSuccessContainer,
        other.onSuccessContainer,
        t,
      )!,
      warning: Color.lerp(warning, other.warning, t)!,
      onWarning: Color.lerp(onWarning, other.onWarning, t)!,
      warningContainer: Color.lerp(
        warningContainer,
        other.warningContainer,
        t,
      )!,
      onWarningContainer: Color.lerp(
        onWarningContainer,
        other.onWarningContainer,
        t,
      )!,
      info: Color.lerp(info, other.info, t)!,
      onInfo: Color.lerp(onInfo, other.onInfo, t)!,
      infoContainer: Color.lerp(infoContainer, other.infoContainer, t)!,
      onInfoContainer: Color.lerp(onInfoContainer, other.onInfoContainer, t)!,
    );
  }
}

const aquaStatusColorsDark = AquaStatusColors(
  success: Color(0xFF55D98B),
  onSuccess: Color(0xFF00210D),
  successContainer: Color(0xFF0E4327),
  onSuccessContainer: Color(0xFFB7F4CB),
  warning: Color(0xFFFFC857),
  onWarning: Color(0xFF2A1B00),
  warningContainer: Color(0xFF4B3810),
  onWarningContainer: Color(0xFFFFE3A1),
  info: Color(0xFF66C8F4),
  onInfo: Color(0xFF001F2C),
  infoContainer: Color(0xFF123E52),
  onInfoContainer: Color(0xFFC5E9FF),
);

const aquaStatusColorsLight = AquaStatusColors(
  success: Color(0xFF006D3B),
  onSuccess: Colors.white,
  successContainer: Color(0xFFB8F3CA),
  onSuccessContainer: Color(0xFF00210D),
  warning: Color(0xFF795900),
  onWarning: Colors.white,
  warningContainer: Color(0xFFFFE29B),
  onWarningContainer: Color(0xFF261A00),
  info: Color(0xFF00658A),
  onInfo: Colors.white,
  infoContainer: Color(0xFFC4E7FA),
  onInfoContainer: Color(0xFF001E2C),
);

extension AquaThemeContext on BuildContext {
  AquaStatusColors get statusColors {
    final theme = Theme.of(this);
    return theme.extension<AquaStatusColors>() ??
        (theme.brightness == Brightness.dark
            ? aquaStatusColorsDark
            : aquaStatusColorsLight);
  }
}

abstract final class AquaTheme {
  static const Color _seed = ProductColors.aqua;

  static ThemeData light() => _build(Brightness.light);

  static ThemeData dark() => _build(Brightness.dark);

  static ThemeData _build(Brightness brightness) {
    final dark = brightness == Brightness.dark;
    final generated = ColorScheme.fromSeed(
      seedColor: _seed,
      brightness: brightness,
    );
    final scheme = generated.copyWith(
      primary: dark ? const Color(0xFF4EDBC7) : const Color(0xFF006B60),
      onPrimary: dark ? const Color(0xFF003730) : Colors.white,
      primaryContainer: dark
          ? const Color(0xFF0B4E47)
          : const Color(0xFFA9F2E7),
      onPrimaryContainer: dark
          ? const Color(0xFFB8FFF3)
          : const Color(0xFF00201C),
      secondary: dark ? const Color(0xFF66C8F4) : const Color(0xFF00658A),
      secondaryContainer: dark
          ? const Color(0xFF123E52)
          : const Color(0xFFC4E7FA),
      tertiary: dark ? const Color(0xFFFFC857) : const Color(0xFF7A5700),
      tertiaryContainer: dark
          ? const Color(0xFF4D390C)
          : const Color(0xFFFFE09A),
      error: dark ? const Color(0xFFFF6B7A) : const Color(0xFFBA1A1A),
      errorContainer: dark ? const Color(0xFF5B1C27) : const Color(0xFFFFDAD6),
      surface: dark ? const Color(0xFF071215) : const Color(0xFFF4F8F8),
      surfaceContainerLowest: dark ? const Color(0xFF050C0E) : Colors.white,
      surfaceContainerLow: dark
          ? const Color(0xFF0B181C)
          : const Color(0xFFF0F5F5),
      surfaceContainer: dark
          ? const Color(0xFF0E1D21)
          : const Color(0xFFEAF1F1),
      surfaceContainerHigh: dark
          ? const Color(0xFF14262B)
          : const Color(0xFFE2EAEA),
      surfaceContainerHighest: dark
          ? const Color(0xFF1B3036)
          : const Color(0xFFD9E4E4),
      outline: dark ? const Color(0xFF5C7378) : const Color(0xFF6F7979),
      outlineVariant: dark ? const Color(0xFF273D42) : const Color(0xFFBEC9C9),
    );
    final status = dark ? aquaStatusColorsDark : aquaStatusColorsLight;
    const controlShape = RoundedRectangleBorder(
      borderRadius: BorderRadius.all(Radius.circular(AquaRadius.control)),
    );
    const inputBorder = OutlineInputBorder(
      borderRadius: BorderRadius.all(Radius.circular(AquaRadius.control)),
    );
    const cardShape = RoundedRectangleBorder(
      borderRadius: BorderRadius.all(Radius.circular(AquaRadius.card)),
    );

    return ThemeData(
      useMaterial3: true,
      brightness: brightness,
      colorScheme: scheme,
      scaffoldBackgroundColor: scheme.surface,
      canvasColor: scheme.surface,
      visualDensity: VisualDensity.standard,
      materialTapTargetSize: MaterialTapTargetSize.padded,
      extensions: [status],
      textTheme: Typography.material2021().white.apply(
        bodyColor: scheme.onSurface,
        displayColor: scheme.onSurface,
        decorationColor: scheme.onSurface,
      ),
      appBarTheme: AppBarTheme(
        elevation: 0,
        scrolledUnderElevation: 0,
        backgroundColor: scheme.surface.withValues(alpha: 0.96),
        foregroundColor: scheme.onSurface,
        surfaceTintColor: Colors.transparent,
        systemOverlayStyle: dark
            ? SystemUiOverlayStyle.light
            : SystemUiOverlayStyle.dark,
      ),
      cardTheme: CardThemeData(
        elevation: 0,
        margin: EdgeInsets.zero,
        color: scheme.surfaceContainerLow,
        surfaceTintColor: Colors.transparent,
        shape: cardShape.copyWith(
          side: BorderSide(color: scheme.outlineVariant),
        ),
      ),
      dividerTheme: DividerThemeData(
        color: scheme.outlineVariant,
        thickness: 1,
        space: 1,
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: scheme.surfaceContainerLowest,
        contentPadding: const EdgeInsets.symmetric(
          horizontal: AquaSpacing.md,
          vertical: 15,
        ),
        border: inputBorder.copyWith(
          borderSide: BorderSide(color: scheme.outlineVariant),
        ),
        enabledBorder: inputBorder.copyWith(
          borderSide: BorderSide(color: scheme.outlineVariant),
        ),
        focusedBorder: inputBorder.copyWith(
          borderSide: BorderSide(color: scheme.primary, width: 2),
        ),
        errorBorder: inputBorder.copyWith(
          borderSide: BorderSide(color: scheme.error),
        ),
      ),
      filledButtonTheme: const FilledButtonThemeData(
        style: ButtonStyle(
          minimumSize: WidgetStatePropertyAll(Size(64, 50)),
          padding: WidgetStatePropertyAll(
            EdgeInsets.symmetric(horizontal: 18, vertical: 13),
          ),
          shape: WidgetStatePropertyAll(controlShape),
          textStyle: WidgetStatePropertyAll(
            TextStyle(fontWeight: FontWeight.w700),
          ),
        ),
      ),
      elevatedButtonTheme: const ElevatedButtonThemeData(
        style: ButtonStyle(
          minimumSize: WidgetStatePropertyAll(Size(64, 50)),
          shape: WidgetStatePropertyAll(controlShape),
        ),
      ),
      outlinedButtonTheme: const OutlinedButtonThemeData(
        style: ButtonStyle(
          minimumSize: WidgetStatePropertyAll(Size(64, 50)),
          shape: WidgetStatePropertyAll(controlShape),
          textStyle: WidgetStatePropertyAll(
            TextStyle(fontWeight: FontWeight.w700),
          ),
        ),
      ),
      textButtonTheme: const TextButtonThemeData(
        style: ButtonStyle(
          minimumSize: WidgetStatePropertyAll(Size(48, 48)),
          shape: WidgetStatePropertyAll(controlShape),
        ),
      ),
      iconButtonTheme: const IconButtonThemeData(
        style: ButtonStyle(
          minimumSize: WidgetStatePropertyAll(Size.square(48)),
          shape: WidgetStatePropertyAll(controlShape),
        ),
      ),
      switchTheme: SwitchThemeData(
        trackOutlineColor: WidgetStateProperty.resolveWith(
          (states) => states.contains(WidgetState.selected)
              ? scheme.primary
              : scheme.outline,
        ),
      ),
      navigationBarTheme: NavigationBarThemeData(
        height: 76,
        elevation: 0,
        backgroundColor: scheme.surfaceContainerLow,
        indicatorColor: scheme.primaryContainer,
        indicatorShape: controlShape,
        labelBehavior: NavigationDestinationLabelBehavior.alwaysShow,
        labelTextStyle: WidgetStateProperty.resolveWith(
          (states) => TextStyle(
            fontSize: 11,
            fontWeight: states.contains(WidgetState.selected)
                ? FontWeight.w800
                : FontWeight.w600,
          ),
        ),
      ),
      navigationRailTheme: NavigationRailThemeData(
        elevation: 0,
        backgroundColor: scheme.surfaceContainerLowest,
        indicatorColor: scheme.primaryContainer,
        indicatorShape: controlShape,
        selectedLabelTextStyle: const TextStyle(fontWeight: FontWeight.w800),
      ),
      chipTheme: ChipThemeData(
        shape: controlShape,
        side: BorderSide(color: scheme.outlineVariant),
      ),
      dialogTheme: const DialogThemeData(shape: cardShape),
      bottomSheetTheme: const BottomSheetThemeData(
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.vertical(
            top: Radius.circular(AquaRadius.hero),
          ),
        ),
      ),
      popupMenuTheme: const PopupMenuThemeData(shape: controlShape),
      snackBarTheme: SnackBarThemeData(
        behavior: SnackBarBehavior.floating,
        shape: controlShape,
        backgroundColor: scheme.inverseSurface,
        contentTextStyle: TextStyle(color: scheme.onInverseSurface),
      ),
      tooltipTheme: TooltipThemeData(
        decoration: BoxDecoration(
          color: scheme.inverseSurface,
          borderRadius: BorderRadius.circular(AquaRadius.control),
        ),
        textStyle: TextStyle(color: scheme.onInverseSurface),
      ),
      progressIndicatorTheme: ProgressIndicatorThemeData(
        color: scheme.primary,
        linearTrackColor: scheme.surfaceContainerHighest,
      ),
    );
  }
}
