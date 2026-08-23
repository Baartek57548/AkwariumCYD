import 'package:aquacyd_home/src/design/app_theme.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('AquaTheme', () {
    for (final configuration in <(String, ThemeData)>[
      ('light', AquaTheme.light()),
      ('dark', AquaTheme.dark()),
    ]) {
      final name = configuration.$1;
      final theme = configuration.$2;

      test('$name exposes an ordered surface ladder', () {
        final scheme = theme.colorScheme;
        final luminances = <double>[
          scheme.surfaceContainerLowest.computeLuminance(),
          scheme.surfaceContainerLow.computeLuminance(),
          scheme.surfaceContainer.computeLuminance(),
          scheme.surfaceContainerHigh.computeLuminance(),
          scheme.surfaceContainerHighest.computeLuminance(),
        ];

        for (var index = 1; index < luminances.length; index++) {
          if (theme.brightness == Brightness.light) {
            expect(luminances[index], lessThan(luminances[index - 1]));
          } else {
            expect(luminances[index], greaterThan(luminances[index - 1]));
          }
        }
        expect(theme.scaffoldBackgroundColor, scheme.surface);
        expect(theme.cardTheme.color, scheme.surfaceContainerLow);
        expect(
          theme.navigationBarTheme.backgroundColor,
          scheme.surfaceContainerLow,
        );
        expect(
          theme.navigationRailTheme.backgroundColor,
          scheme.surfaceContainerLow,
        );
      });

      test('$name semantic foregrounds meet WCAG contrast', () {
        final colors = theme.semanticColors;
        final pairs = <(Color, Color, String)>[
          (colors.success, colors.onSuccess, 'success'),
          (
            colors.successContainer,
            colors.onSuccessContainer,
            'successContainer',
          ),
          (colors.warning, colors.onWarning, 'warning'),
          (
            colors.warningContainer,
            colors.onWarningContainer,
            'warningContainer',
          ),
          (colors.info, colors.onInfo, 'info'),
          (colors.infoContainer, colors.onInfoContainer, 'infoContainer'),
        ];

        for (final pair in pairs) {
          expect(
            _contrastRatio(pair.$1, pair.$2),
            greaterThanOrEqualTo(4.5),
            reason: '$name ${pair.$3}',
          );
        }
      });

      test('$name core scheme foregrounds meet WCAG contrast', () {
        final scheme = theme.colorScheme;
        final pairs = <(Color, Color, String)>[
          (scheme.primary, scheme.onPrimary, 'primary'),
          (
            scheme.primaryContainer,
            scheme.onPrimaryContainer,
            'primaryContainer',
          ),
          (scheme.secondary, scheme.onSecondary, 'secondary'),
          (
            scheme.secondaryContainer,
            scheme.onSecondaryContainer,
            'secondaryContainer',
          ),
          (scheme.tertiary, scheme.onTertiary, 'tertiary'),
          (
            scheme.tertiaryContainer,
            scheme.onTertiaryContainer,
            'tertiaryContainer',
          ),
          (scheme.error, scheme.onError, 'error'),
          (scheme.errorContainer, scheme.onErrorContainer, 'errorContainer'),
          (scheme.surface, scheme.onSurface, 'surface'),
          (scheme.surface, scheme.onSurfaceVariant, 'surfaceVariantText'),
        ];

        for (final pair in pairs) {
          expect(
            _contrastRatio(pair.$1, pair.$2),
            greaterThanOrEqualTo(4.5),
            reason: '$name ${pair.$3}',
          );
        }
      });

      test('$name configures premium shared components', () {
        expect(theme.useMaterial3, isTrue);
        expect(theme.cardTheme.elevation, 0);
        expect(theme.appBarTheme.elevation, 0);
        expect(theme.bottomSheetTheme.surfaceTintColor, Colors.transparent);
        expect(theme.dialogTheme.surfaceTintColor, Colors.transparent);
        expect(theme.snackBarTheme.behavior, SnackBarBehavior.floating);
        expect(theme.chipTheme.shape, isA<RoundedRectangleBorder>());
        expect(theme.listTileTheme.shape, isA<RoundedRectangleBorder>());
        expect(theme.segmentedButtonTheme.style, isNotNull);
        expect(theme.iconButtonTheme.style, isNotNull);
        expect(theme.switchTheme.thumbColor, isNotNull);
        expect(theme.navigationBarTheme.indicatorShape, isNotNull);
        expect(theme.navigationRailTheme.indicatorShape, isNotNull);
      });

      test('$name uses a deliberate, system-font typography hierarchy', () {
        final text = theme.textTheme;
        expect(text.displaySmall?.fontWeight, FontWeight.w800);
        expect(text.headlineMedium?.fontWeight, FontWeight.w800);
        expect(text.titleLarge?.fontWeight, FontWeight.w700);
        expect(text.bodyMedium?.fontWeight, FontWeight.w400);
        expect(text.labelLarge?.fontWeight, FontWeight.w700);
        expect(text.bodyMedium?.height, greaterThanOrEqualTo(1.4));
        final platformTypography = ThemeData(
          useMaterial3: true,
          brightness: theme.brightness,
        ).textTheme;
        expect(
          text.displaySmall?.fontFamily,
          platformTypography.displaySmall?.fontFamily,
        );
        expect(
          text.displaySmall?.fontFeatures,
          contains(const FontFeature.tabularFigures()),
        );
      });
    }

    test('semantic extension interpolates every color', () {
      final midpoint = AquaSemanticColors.light.lerp(
        AquaSemanticColors.dark,
        0.5,
      );

      expect(
        midpoint.success,
        Color.lerp(
          AquaSemanticColors.light.success,
          AquaSemanticColors.dark.success,
          0.5,
        ),
      );
      expect(
        midpoint.onInfoContainer,
        Color.lerp(
          AquaSemanticColors.light.onInfoContainer,
          AquaSemanticColors.dark.onInfoContainer,
          0.5,
        ),
      );
    });
  });
}

double _contrastRatio(Color first, Color second) {
  final firstLuminance = first.computeLuminance();
  final secondLuminance = second.computeLuminance();
  final lighter = firstLuminance > secondLuminance
      ? firstLuminance
      : secondLuminance;
  final darker = firstLuminance > secondLuminance
      ? secondLuminance
      : firstLuminance;
  return (lighter + 0.05) / (darker + 0.05);
}
