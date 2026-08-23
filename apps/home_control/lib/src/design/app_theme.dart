import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:flutter/material.dart';

abstract final class AquaColors {
  static const cyan = Color(0xFF36C5D8);
  static const blue = Color(0xFF4C8DFF);
  static const green = Color(0xFF4DD5A6);
  static const amber = Color(0xFFF6BE5A);
  static const red = Color(0xFFFF6B7A);
  static const ink = Color(0xFF07151C);
  static const darkSurface = Color(0xFF0D2029);
}

/// Semantic feedback colors that remain legible in both brightness modes.
///
/// Brand colors should not be used directly for text or status icons because
/// their contrast depends on the surface below them. This extension provides
/// complete foreground/container pairs for status UI while keeping
/// [ColorScheme] focused on the Material component palette.
@immutable
final class AquaSemanticColors extends ThemeExtension<AquaSemanticColors> {
  const AquaSemanticColors({
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

  static const AquaSemanticColors light = AquaSemanticColors(
    success: Color(0xFF006B4F),
    onSuccess: Color(0xFFFFFFFF),
    successContainer: Color(0xFF9CF4D1),
    onSuccessContainer: Color(0xFF002117),
    warning: Color(0xFF735C00),
    onWarning: Color(0xFFFFFFFF),
    warningContainer: Color(0xFFFFE16B),
    onWarningContainer: Color(0xFF231B00),
    info: Color(0xFF315DA8),
    onInfo: Color(0xFFFFFFFF),
    infoContainer: Color(0xFFD8E2FF),
    onInfoContainer: Color(0xFF001A41),
  );

  static const AquaSemanticColors dark = AquaSemanticColors(
    success: Color(0xFF82DAB9),
    onSuccess: Color(0xFF003828),
    successContainer: Color(0xFF00513D),
    onSuccessContainer: Color(0xFFA0F6D4),
    warning: Color(0xFFE7C44D),
    onWarning: Color(0xFF3C2F00),
    warningContainer: Color(0xFF584500),
    onWarningContainer: Color(0xFFFFE16B),
    info: Color(0xFFADC6FF),
    onInfo: Color(0xFF002E69),
    infoContainer: Color(0xFF17457F),
    onInfoContainer: Color(0xFFD8E2FF),
  );

  @override
  AquaSemanticColors copyWith({
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
  }) => AquaSemanticColors(
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

  @override
  AquaSemanticColors lerp(covariant AquaSemanticColors? other, double t) {
    if (other == null) return this;
    return AquaSemanticColors(
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

extension AquaThemeDataX on ThemeData {
  AquaSemanticColors get semanticColors =>
      extension<AquaSemanticColors>() ??
      (brightness == Brightness.dark
          ? AquaSemanticColors.dark
          : AquaSemanticColors.light);
}

abstract final class AquaTheme {
  static ThemeData light() => _theme(Brightness.light);

  static ThemeData dark() => _theme(Brightness.dark);

  static ThemeData _theme(Brightness brightness) {
    final dark = brightness == Brightness.dark;
    final semanticColors = dark
        ? AquaSemanticColors.dark
        : AquaSemanticColors.light;
    final scheme = ColorScheme.fromSeed(
      seedColor: AquaColors.cyan,
      brightness: brightness,
      dynamicSchemeVariant: DynamicSchemeVariant.fidelity,
      primary: dark ? const Color(0xFF65D7E4) : const Color(0xFF006875),
      onPrimary: dark ? const Color(0xFF00363D) : Colors.white,
      primaryContainer: dark
          ? const Color(0xFF004F58)
          : const Color(0xFF9CF0FA),
      onPrimaryContainer: dark
          ? const Color(0xFF9CF0FA)
          : const Color(0xFF001F24),
      secondary: dark ? const Color(0xFFADC6FF) : const Color(0xFF315DA8),
      onSecondary: dark ? const Color(0xFF002E69) : Colors.white,
      secondaryContainer: dark
          ? const Color(0xFF17457F)
          : const Color(0xFFD8E2FF),
      onSecondaryContainer: dark
          ? const Color(0xFFD8E2FF)
          : const Color(0xFF001A41),
      tertiary: semanticColors.warning,
      onTertiary: semanticColors.onWarning,
      tertiaryContainer: semanticColors.warningContainer,
      onTertiaryContainer: semanticColors.onWarningContainer,
      error: dark ? const Color(0xFFFFB2B9) : const Color(0xFFB91F35),
      onError: dark ? const Color(0xFF680015) : Colors.white,
      errorContainer: dark ? const Color(0xFF930021) : const Color(0xFFFFDADB),
      onErrorContainer: dark
          ? const Color(0xFFFFDADB)
          : const Color(0xFF410008),
      outline: dark ? const Color(0xFF82939A) : const Color(0xFF687A81),
      outlineVariant: dark ? const Color(0xFF33474F) : const Color(0xFFC5D4D9),
      surface: dark ? AquaColors.ink : const Color(0xFFF4F8FA),
      onSurface: dark ? const Color(0xFFE3EBEE) : const Color(0xFF142126),
      surfaceDim: dark ? const Color(0xFF07151C) : const Color(0xFFD8E2E6),
      surfaceBright: dark ? const Color(0xFF2A3B43) : const Color(0xFFF9FCFD),
      surfaceContainerLowest: dark
          ? const Color(0xFF041015)
          : const Color(0xFFFFFFFF),
      surfaceContainerLow: dark
          ? const Color(0xFF0B1C24)
          : const Color(0xFFEDF3F6),
      surfaceContainer: dark ? AquaColors.darkSurface : const Color(0xFFE8F0F3),
      surfaceContainerHigh: dark
          ? const Color(0xFF142933)
          : const Color(0xFFE1EBEF),
      surfaceContainerHighest: dark
          ? const Color(0xFF1B313B)
          : const Color(0xFFD9E5E9),
      onSurfaceVariant: dark
          ? const Color(0xFFBBC9CE)
          : const Color(0xFF45565D),
      inverseSurface: dark ? const Color(0xFFE3EBEE) : const Color(0xFF28363B),
      onInverseSurface: dark
          ? const Color(0xFF142126)
          : const Color(0xFFEDF3F6),
      inversePrimary: dark ? const Color(0xFF006875) : const Color(0xFF65D7E4),
      shadow: const Color(0xFF000000),
      scrim: const Color(0xFF000000),
      surfaceTint: Colors.transparent,
    );
    final textTheme = _textTheme(
      ThemeData(brightness: brightness, useMaterial3: true).textTheme,
      scheme,
    );
    final base = ThemeData(
      useMaterial3: true,
      brightness: brightness,
      colorScheme: scheme,
      scaffoldBackgroundColor: scheme.surface,
      textTheme: textTheme,
      visualDensity: VisualDensity.standard,
      extensions: <ThemeExtension<dynamic>>[semanticColors],
    );
    final controlShape = RoundedRectangleBorder(
      borderRadius: BorderRadius.circular(ProductRadius.control),
    );
    final indicatorShape = RoundedRectangleBorder(
      borderRadius: BorderRadius.circular(ProductRadius.pill),
    );
    final subtleSide = BorderSide(
      color: scheme.outlineVariant.withValues(alpha: dark ? 0.72 : 0.82),
    );
    final selectedFill = WidgetStateProperty.resolveWith<Color?>((states) {
      if (states.contains(WidgetState.disabled)) return null;
      if (states.contains(WidgetState.selected)) return scheme.primaryContainer;
      if (states.contains(WidgetState.pressed)) {
        return scheme.primary.withValues(alpha: 0.12);
      }
      if (states.contains(WidgetState.hovered)) {
        return scheme.primary.withValues(alpha: 0.08);
      }
      return null;
    });

    return base.copyWith(
      appBarTheme: AppBarTheme(
        centerTitle: false,
        elevation: 0,
        scrolledUnderElevation: 0,
        backgroundColor: scheme.surface,
        foregroundColor: scheme.onSurface,
        surfaceTintColor: Colors.transparent,
        iconTheme: IconThemeData(color: scheme.onSurfaceVariant, size: 24),
        actionsIconTheme: IconThemeData(
          color: scheme.onSurfaceVariant,
          size: 24,
        ),
        titleTextStyle: textTheme.titleLarge?.copyWith(color: scheme.onSurface),
      ),
      cardTheme: CardThemeData(
        elevation: 0,
        surfaceTintColor: Colors.transparent,
        color: scheme.surfaceContainerLow,
        margin: EdgeInsets.zero,
        clipBehavior: Clip.none,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(ProductRadius.card),
          side: subtleSide,
        ),
      ),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          minimumSize: const Size(48, 52),
          shape: controlShape,
          textStyle: textTheme.labelLarge,
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          minimumSize: const Size(48, 52),
          shape: controlShape,
          side: BorderSide(color: scheme.outline),
          textStyle: textTheme.labelLarge,
        ),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: scheme.surfaceContainerHigh,
        labelStyle: textTheme.bodyMedium?.copyWith(
          color: scheme.onSurfaceVariant,
        ),
        floatingLabelStyle: textTheme.bodyMedium?.copyWith(
          color: scheme.primary,
          fontWeight: FontWeight.w600,
        ),
        hintStyle: textTheme.bodyMedium?.copyWith(
          color: scheme.onSurfaceVariant.withValues(alpha: 0.82),
        ),
        border: OutlineInputBorder(
          borderRadius: BorderRadius.circular(ProductRadius.control),
          borderSide: BorderSide.none,
        ),
        enabledBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(ProductRadius.control),
          borderSide: subtleSide,
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(ProductRadius.control),
          borderSide: BorderSide(color: scheme.primary, width: 2),
        ),
        errorBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(ProductRadius.control),
          borderSide: BorderSide(color: scheme.error),
        ),
        focusedErrorBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(ProductRadius.control),
          borderSide: BorderSide(color: scheme.error, width: 2),
        ),
        contentPadding: const EdgeInsets.symmetric(
          horizontal: 18,
          vertical: 16,
        ),
      ),
      chipTheme: ChipThemeData(
        backgroundColor: scheme.surfaceContainerHigh,
        selectedColor: scheme.primaryContainer,
        disabledColor: scheme.surfaceContainerHighest.withValues(alpha: 0.55),
        checkmarkColor: scheme.onPrimaryContainer,
        side: subtleSide,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(ProductRadius.control),
        ),
        labelStyle: textTheme.labelMedium?.copyWith(color: scheme.onSurface),
        secondaryLabelStyle: textTheme.labelMedium?.copyWith(
          color: scheme.onPrimaryContainer,
        ),
        padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 2),
        elevation: 0,
        pressElevation: 0,
        surfaceTintColor: Colors.transparent,
      ),
      listTileTheme: ListTileThemeData(
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(ProductRadius.control),
        ),
        selectedColor: scheme.onPrimaryContainer,
        selectedTileColor: scheme.primaryContainer,
        iconColor: scheme.onSurfaceVariant,
        titleTextStyle: textTheme.titleMedium,
        subtitleTextStyle: textTheme.bodySmall?.copyWith(
          color: scheme.onSurfaceVariant,
        ),
        minVerticalPadding: 10,
        horizontalTitleGap: ProductSpacing.sm,
      ),
      bottomSheetTheme: BottomSheetThemeData(
        backgroundColor: scheme.surfaceContainerLow,
        modalBackgroundColor: scheme.surfaceContainerLow,
        surfaceTintColor: Colors.transparent,
        elevation: 0,
        modalElevation: 2,
        shadowColor: scheme.shadow.withValues(alpha: 0.2),
        modalBarrierColor: scheme.scrim.withValues(alpha: 0.48),
        showDragHandle: true,
        dragHandleColor: scheme.outline,
        dragHandleSize: const Size(36, 4),
        clipBehavior: Clip.antiAlias,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.vertical(
            top: Radius.circular(ProductRadius.hero),
          ),
        ),
      ),
      dialogTheme: DialogThemeData(
        backgroundColor: scheme.surfaceContainerLow,
        surfaceTintColor: Colors.transparent,
        elevation: 2,
        shadowColor: scheme.shadow.withValues(alpha: 0.2),
        barrierColor: scheme.scrim.withValues(alpha: 0.48),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(ProductRadius.hero),
          side: subtleSide,
        ),
        iconColor: scheme.primary,
        titleTextStyle: textTheme.headlineSmall?.copyWith(
          color: scheme.onSurface,
        ),
        contentTextStyle: textTheme.bodyMedium?.copyWith(
          color: scheme.onSurfaceVariant,
        ),
        actionsPadding: const EdgeInsets.fromLTRB(24, 8, 24, 20),
        insetPadding: const EdgeInsets.symmetric(horizontal: 24, vertical: 24),
        clipBehavior: Clip.antiAlias,
      ),
      snackBarTheme: SnackBarThemeData(
        behavior: SnackBarBehavior.floating,
        backgroundColor: scheme.inverseSurface,
        actionTextColor: scheme.inversePrimary,
        closeIconColor: scheme.onInverseSurface,
        contentTextStyle: textTheme.bodyMedium?.copyWith(
          color: scheme.onInverseSurface,
        ),
        elevation: 2,
        insetPadding: const EdgeInsets.all(ProductSpacing.md),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(ProductRadius.control),
        ),
      ),
      switchTheme: SwitchThemeData(
        thumbColor: WidgetStateProperty.resolveWith((states) {
          if (states.contains(WidgetState.disabled)) {
            return scheme.onSurface.withValues(alpha: 0.38);
          }
          if (states.contains(WidgetState.selected)) return scheme.onPrimary;
          return scheme.outline;
        }),
        trackColor: WidgetStateProperty.resolveWith((states) {
          if (states.contains(WidgetState.disabled)) {
            return scheme.onSurface.withValues(alpha: 0.12);
          }
          if (states.contains(WidgetState.selected)) return scheme.primary;
          return scheme.surfaceContainerHighest;
        }),
        trackOutlineColor: WidgetStateProperty.resolveWith((states) {
          if (states.contains(WidgetState.disabled)) return Colors.transparent;
          if (states.contains(WidgetState.selected)) return scheme.primary;
          return scheme.outline;
        }),
        overlayColor: WidgetStateProperty.resolveWith((states) {
          if (states.contains(WidgetState.pressed)) {
            return scheme.primary.withValues(alpha: 0.14);
          }
          if (states.contains(WidgetState.hovered) ||
              states.contains(WidgetState.focused)) {
            return scheme.primary.withValues(alpha: 0.1);
          }
          return null;
        }),
      ),
      sliderTheme: SliderThemeData(
        activeTrackColor: scheme.primary,
        inactiveTrackColor: scheme.surfaceContainerHighest,
        disabledActiveTrackColor: scheme.onSurface.withValues(alpha: 0.18),
        disabledInactiveTrackColor: scheme.onSurface.withValues(alpha: 0.08),
        thumbColor: scheme.primary,
        disabledThumbColor: scheme.onSurface.withValues(alpha: 0.38),
        overlayColor: scheme.primary.withValues(alpha: 0.12),
        valueIndicatorColor: scheme.inverseSurface,
        valueIndicatorTextStyle: textTheme.labelMedium?.copyWith(
          color: scheme.onInverseSurface,
        ),
        trackHeight: 4,
        thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 9),
        overlayShape: const RoundSliderOverlayShape(overlayRadius: 20),
        showValueIndicator: ShowValueIndicator.onlyForDiscrete,
      ),
      progressIndicatorTheme: ProgressIndicatorThemeData(
        color: scheme.primary,
        linearTrackColor: scheme.surfaceContainerHighest,
        circularTrackColor: scheme.surfaceContainerHighest,
      ),
      segmentedButtonTheme: SegmentedButtonThemeData(
        style: ButtonStyle(
          minimumSize: const WidgetStatePropertyAll(Size(48, 48)),
          padding: const WidgetStatePropertyAll(
            EdgeInsets.symmetric(horizontal: 16, vertical: 12),
          ),
          backgroundColor: selectedFill,
          foregroundColor: WidgetStateProperty.resolveWith((states) {
            if (states.contains(WidgetState.disabled)) {
              return scheme.onSurface.withValues(alpha: 0.38);
            }
            if (states.contains(WidgetState.selected)) {
              return scheme.onPrimaryContainer;
            }
            return scheme.onSurfaceVariant;
          }),
          textStyle: WidgetStatePropertyAll(textTheme.labelLarge),
          side: WidgetStatePropertyAll(subtleSide),
          shape: WidgetStatePropertyAll(controlShape),
          visualDensity: VisualDensity.standard,
        ),
      ),
      iconButtonTheme: IconButtonThemeData(
        style: ButtonStyle(
          minimumSize: const WidgetStatePropertyAll(Size.square(48)),
          iconSize: const WidgetStatePropertyAll(24),
          foregroundColor: WidgetStateProperty.resolveWith((states) {
            if (states.contains(WidgetState.disabled)) {
              return scheme.onSurface.withValues(alpha: 0.38);
            }
            if (states.contains(WidgetState.selected)) return scheme.primary;
            return scheme.onSurfaceVariant;
          }),
          backgroundColor: selectedFill,
          overlayColor: WidgetStateProperty.resolveWith((states) {
            if (states.contains(WidgetState.pressed)) {
              return scheme.primary.withValues(alpha: 0.12);
            }
            if (states.contains(WidgetState.hovered) ||
                states.contains(WidgetState.focused)) {
              return scheme.primary.withValues(alpha: 0.08);
            }
            return null;
          }),
          shape: WidgetStatePropertyAll(controlShape),
        ),
      ),
      navigationBarTheme: NavigationBarThemeData(
        height: 72,
        elevation: 0,
        backgroundColor: scheme.surfaceContainerLow,
        surfaceTintColor: Colors.transparent,
        indicatorColor: scheme.primaryContainer,
        indicatorShape: indicatorShape,
        labelTextStyle: WidgetStateProperty.resolveWith(
          (states) => textTheme.labelMedium?.copyWith(
            color: states.contains(WidgetState.selected)
                ? scheme.onSurface
                : scheme.onSurfaceVariant,
            fontWeight: states.contains(WidgetState.selected)
                ? FontWeight.w700
                : FontWeight.w600,
          ),
        ),
        iconTheme: WidgetStateProperty.resolveWith(
          (states) => IconThemeData(
            color: states.contains(WidgetState.selected)
                ? scheme.onPrimaryContainer
                : scheme.onSurfaceVariant,
            size: 24,
          ),
        ),
        overlayColor: WidgetStateProperty.resolveWith((states) {
          if (states.contains(WidgetState.pressed)) {
            return scheme.primary.withValues(alpha: 0.1);
          }
          return null;
        }),
      ),
      navigationRailTheme: NavigationRailThemeData(
        backgroundColor: scheme.surfaceContainerLow,
        elevation: 0,
        indicatorColor: scheme.primaryContainer,
        indicatorShape: indicatorShape,
        selectedIconTheme: IconThemeData(
          color: scheme.onPrimaryContainer,
          size: 24,
        ),
        unselectedIconTheme: IconThemeData(
          color: scheme.onSurfaceVariant,
          size: 24,
        ),
        selectedLabelTextStyle: textTheme.labelLarge?.copyWith(
          color: scheme.onSurface,
          fontWeight: FontWeight.w700,
        ),
        unselectedLabelTextStyle: textTheme.labelLarge?.copyWith(
          color: scheme.onSurfaceVariant,
        ),
        groupAlignment: -0.65,
      ),
      dividerTheme: DividerThemeData(
        color: scheme.outlineVariant.withValues(alpha: dark ? 0.72 : 0.82),
        space: 1,
        thickness: 1,
      ),
    );
  }

  static TextTheme _textTheme(TextTheme base, ColorScheme scheme) {
    TextStyle? style(
      TextStyle? source, {
      FontWeight? weight,
      double? height,
      double? letterSpacing,
      Color? color,
      List<FontFeature>? fontFeatures,
    }) => source?.copyWith(
      fontFamily: null,
      fontFamilyFallback: const <String>['sans-serif'],
      fontWeight: weight,
      height: height,
      letterSpacing: letterSpacing,
      color: color ?? scheme.onSurface,
      fontFeatures: fontFeatures,
    );

    return base.copyWith(
      displayLarge: style(
        base.displayLarge,
        weight: FontWeight.w800,
        height: 1.08,
        letterSpacing: -1.5,
        fontFeatures: const <FontFeature>[FontFeature.tabularFigures()],
      ),
      displayMedium: style(
        base.displayMedium,
        weight: FontWeight.w800,
        height: 1.1,
        letterSpacing: -1.2,
        fontFeatures: const <FontFeature>[FontFeature.tabularFigures()],
      ),
      displaySmall: style(
        base.displaySmall,
        weight: FontWeight.w800,
        height: 1.12,
        letterSpacing: -0.8,
        fontFeatures: const <FontFeature>[FontFeature.tabularFigures()],
      ),
      headlineLarge: style(
        base.headlineLarge,
        weight: FontWeight.w800,
        height: 1.16,
        letterSpacing: -0.5,
      ),
      headlineMedium: style(
        base.headlineMedium,
        weight: FontWeight.w800,
        height: 1.18,
        letterSpacing: -0.35,
      ),
      headlineSmall: style(
        base.headlineSmall,
        weight: FontWeight.w700,
        height: 1.22,
        letterSpacing: -0.2,
      ),
      titleLarge: style(
        base.titleLarge,
        weight: FontWeight.w700,
        height: 1.24,
        letterSpacing: -0.15,
      ),
      titleMedium: style(
        base.titleMedium,
        weight: FontWeight.w700,
        height: 1.3,
        letterSpacing: 0,
      ),
      titleSmall: style(
        base.titleSmall,
        weight: FontWeight.w600,
        height: 1.32,
        letterSpacing: 0.05,
      ),
      bodyLarge: style(base.bodyLarge, weight: FontWeight.w400, height: 1.5),
      bodyMedium: style(base.bodyMedium, weight: FontWeight.w400, height: 1.45),
      bodySmall: style(
        base.bodySmall,
        weight: FontWeight.w400,
        height: 1.42,
        color: scheme.onSurfaceVariant,
      ),
      labelLarge: style(
        base.labelLarge,
        weight: FontWeight.w700,
        height: 1.2,
        letterSpacing: 0.1,
      ),
      labelMedium: style(
        base.labelMedium,
        weight: FontWeight.w600,
        height: 1.2,
        letterSpacing: 0.25,
        color: scheme.onSurfaceVariant,
      ),
      labelSmall: style(
        base.labelSmall,
        weight: FontWeight.w600,
        height: 1.2,
        letterSpacing: 0.35,
        color: scheme.onSurfaceVariant,
      ),
    );
  }
}
