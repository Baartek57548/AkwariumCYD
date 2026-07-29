import 'package:flutter/material.dart';

import 'app_update/app_update_ui.dart';
import 'ble_scanner_page.dart';
import 'controller_page.dart';
import 'design_system.dart';
import 'full_controller/controller_session.dart';
import 'full_controller/controller_shell.dart';
import 'full_controller/wifi_connect_page.dart';

class ConnectionHomePage extends StatelessWidget {
  const ConnectionHomePage({
    super.key,
    this.brandName = 'AquaCYD Control',
    this.showDevelopment = false,
    this.showLegacyWebView = false,
    this.lastController,
    this.resumeError,
    this.onRetryLast,
  });

  final String brandName;
  final bool showDevelopment;
  final bool showLegacyWebView;
  final Uri? lastController;
  final String? resumeError;
  final VoidCallback? onRetryLast;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Scaffold(
      body: Stack(
        children: [
          const Positioned.fill(child: _CommandBackground()),
          SafeArea(
            child: SingleChildScrollView(
              keyboardDismissBehavior: ScrollViewKeyboardDismissBehavior.onDrag,
              padding: const EdgeInsets.fromLTRB(
                AquaSpacing.md,
                AquaSpacing.lg,
                AquaSpacing.md,
                AquaSpacing.xl,
              ),
              child: Center(
                child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 980),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      _BrandHero(brandName: brandName),
                      const SizedBox(height: AquaSpacing.lg),
                      if (resumeError != null) ...[
                        _ResumePanel(
                          address: lastController,
                          message: resumeError!,
                          onRetry: onRetryLast,
                        ),
                        const SizedBox(height: AquaSpacing.md),
                      ],
                      Text(
                        'Połącz centrum dowodzenia',
                        style: Theme.of(context).textTheme.headlineSmall
                            ?.copyWith(fontWeight: FontWeight.w800),
                      ),
                      const SizedBox(height: AquaSpacing.xs),
                      Text(
                        'Wybierz kanał komunikacji. Wi‑Fi udostępnia wszystkie '
                        'funkcje sterownika, a BLE zapewnia lokalny dostęp awaryjny.',
                        style: Theme.of(context).textTheme.bodyLarge?.copyWith(
                          color: colors.onSurfaceVariant,
                          height: 1.45,
                        ),
                      ),
                      const SizedBox(height: AquaSpacing.md),
                      LayoutBuilder(
                        builder: (context, constraints) {
                          final stacked =
                              constraints.maxWidth < 700 ||
                              MediaQuery.textScalerOf(context).scale(1) > 1.25;
                          final cards = [
                            _ConnectionCard(
                              icon: Icons.router_rounded,
                              eyebrow: 'PEŁNY DOSTĘP',
                              title: 'Sterownik przez Wi‑Fi',
                              description:
                                  'Telemetria na żywo, automatyka, harmonogramy, '
                                  'historia, diagnostyka, konfiguracja i OTA.',
                              actionLabel: 'Połącz przez Wi‑Fi',
                              accent: colors.primary,
                              primary: true,
                              capabilities: const [
                                'REST API',
                                'Historia',
                                'Firmware OTA',
                              ],
                              onTap: () =>
                                  _open(context, const WifiConnectPage()),
                            ),
                            _ConnectionCard(
                              icon: Icons.bluetooth_connected_rounded,
                              eyebrow: 'DOSTĘP LOKALNY',
                              title: 'Sterownik przez BLE',
                              description:
                                  'Bezpośrednia telemetria i podstawowe sterowanie '
                                  'bez routera. Zakres zależy od wersji firmware.',
                              actionLabel: 'Skanuj urządzenia BLE',
                              accent: colors.secondary,
                              capabilities: const [
                                'Bez Wi‑Fi',
                                'Niski pobór',
                                'Tryb awaryjny',
                              ],
                              onTap: () =>
                                  _open(context, const BleScannerPage()),
                            ),
                          ];
                          if (stacked) {
                            return Column(
                              children: [
                                cards.first,
                                const SizedBox(height: AquaSpacing.sm),
                                cards.last,
                              ],
                            );
                          }
                          return Row(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Expanded(child: cards.first),
                              const SizedBox(width: AquaSpacing.md),
                              Expanded(child: cards.last),
                            ],
                          );
                        },
                      ),
                      if (showDevelopment || showLegacyWebView) ...[
                        const SizedBox(height: AquaSpacing.lg),
                        ExpansionTile(
                          tilePadding: const EdgeInsets.symmetric(
                            horizontal: AquaSpacing.md,
                          ),
                          childrenPadding: const EdgeInsets.fromLTRB(
                            AquaSpacing.md,
                            0,
                            AquaSpacing.md,
                            AquaSpacing.md,
                          ),
                          leading: const Icon(Icons.developer_mode_rounded),
                          title: const Text(
                            'Narzędzia serwisowe',
                            style: TextStyle(fontWeight: FontWeight.w800),
                          ),
                          subtitle: const Text(
                            'Symulator oraz zgodność ze starszym panelem',
                          ),
                          children: [
                            if (showDevelopment)
                              ListTile(
                                leading: const Icon(Icons.science_rounded),
                                title: const Text(
                                  'Symulator całego sterownika',
                                ),
                                subtitle: const Text(
                                  'Dane pozostają wyłącznie w pamięci telefonu.',
                                ),
                                trailing: const Icon(
                                  Icons.arrow_forward_rounded,
                                ),
                                onTap: () => _open(
                                  context,
                                  ControllerShell(
                                    session: ControllerSession.development(),
                                  ),
                                ),
                              ),
                            if (showLegacyWebView)
                              ListTile(
                                leading: const Icon(Icons.language_rounded),
                                title: const Text('Panel WWW zgodności'),
                                subtitle: const Text(
                                  'Interfejs hostowany bezpośrednio przez ESP32.',
                                ),
                                trailing: const Icon(
                                  Icons.arrow_forward_rounded,
                                ),
                                onTap: () =>
                                    _open(context, const ControllerPage()),
                              ),
                          ],
                        ),
                      ],
                      const SizedBox(height: AquaSpacing.lg),
                      const _SecurityNote(),
                      const SizedBox(height: AquaSpacing.md),
                      const AppUpdateFooter(),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  static void _open(BuildContext context, Widget page) {
    Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => page));
  }
}

class _CommandBackground extends StatelessWidget {
  const _CommandBackground();

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return DecoratedBox(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            colors.surface,
            colors.surface,
            colors.primaryContainer.withValues(alpha: 0.22),
          ],
          stops: const [0, 0.58, 1],
        ),
      ),
      child: CustomPaint(painter: _GridPainter(colors.outlineVariant)),
    );
  }
}

class _GridPainter extends CustomPainter {
  const _GridPainter(this.color);

  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = color.withValues(alpha: 0.14)
      ..strokeWidth = 1;
    const step = 36.0;
    for (var x = 0.0; x <= size.width; x += step) {
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), paint);
    }
    for (var y = 0.0; y <= size.height; y += step) {
      canvas.drawLine(Offset(0, y), Offset(size.width, y), paint);
    }
  }

  @override
  bool shouldRepaint(covariant _GridPainter oldDelegate) =>
      oldDelegate.color != color;
}

class _BrandHero extends StatelessWidget {
  const _BrandHero({required this.brandName});

  final String brandName;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Card(
      color: colors.surfaceContainerLow.withValues(alpha: 0.92),
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.lg),
        child: LayoutBuilder(
          builder: (context, constraints) {
            final compact =
                constraints.maxWidth < 560 ||
                MediaQuery.textScalerOf(context).scale(1) > 1.35;
            final logo = ClipRRect(
              borderRadius: BorderRadius.circular(AquaRadius.card),
              child: Image.asset(
                'assets/branding/aquacyd-control-icon.png',
                width: compact ? 72 : 92,
                height: compact ? 72 : 92,
                fit: BoxFit.cover,
                semanticLabel: 'Logo AquaCYD Control',
              ),
            );
            final copy = Column(
              crossAxisAlignment: compact
                  ? CrossAxisAlignment.center
                  : CrossAxisAlignment.start,
              children: [
                DecoratedBox(
                  decoration: BoxDecoration(
                    color: colors.primaryContainer,
                    borderRadius: BorderRadius.circular(AquaRadius.control),
                  ),
                  child: Padding(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 10,
                      vertical: 5,
                    ),
                    child: Text(
                      'AQUARIUM OPERATIONS',
                      style: Theme.of(context).textTheme.labelSmall?.copyWith(
                        color: colors.onPrimaryContainer,
                        fontWeight: FontWeight.w900,
                        letterSpacing: 1.1,
                      ),
                    ),
                  ),
                ),
                const SizedBox(height: AquaSpacing.sm),
                Text(
                  brandName,
                  textAlign: compact ? TextAlign.center : TextAlign.start,
                  style: Theme.of(context).textTheme.displaySmall?.copyWith(
                    fontWeight: FontWeight.w900,
                    letterSpacing: -1.2,
                  ),
                ),
                const SizedBox(height: AquaSpacing.xs),
                Text(
                  'Profesjonalne centrum monitoringu, automatyki '
                  'i bezpieczeństwa sterownika CYD.',
                  textAlign: compact ? TextAlign.center : TextAlign.start,
                  style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    color: colors.onSurfaceVariant,
                    height: 1.4,
                  ),
                ),
                const SizedBox(height: AquaSpacing.md),
                const Wrap(
                  alignment: WrapAlignment.center,
                  spacing: AquaSpacing.xs,
                  runSpacing: AquaSpacing.xs,
                  children: [
                    _CapabilityPill(
                      icon: Icons.monitor_heart_rounded,
                      label: 'Live',
                    ),
                    _CapabilityPill(
                      icon: Icons.auto_mode_rounded,
                      label: 'Automatyka',
                    ),
                    _CapabilityPill(
                      icon: Icons.security_rounded,
                      label: 'Failsafe',
                    ),
                  ],
                ),
              ],
            );
            if (compact) {
              return Column(
                children: [
                  logo,
                  const SizedBox(height: AquaSpacing.md),
                  copy,
                ],
              );
            }
            return Row(
              children: [
                logo,
                const SizedBox(width: AquaSpacing.lg),
                Expanded(child: copy),
              ],
            );
          },
        ),
      ),
    );
  }
}

class _ResumePanel extends StatelessWidget {
  const _ResumePanel({
    required this.address,
    required this.message,
    required this.onRetry,
  });

  final Uri? address;
  final String message;
  final VoidCallback? onRetry;

  @override
  Widget build(BuildContext context) {
    final status = context.statusColors;
    return DecoratedBox(
      decoration: BoxDecoration(
        color: status.warningContainer,
        borderRadius: BorderRadius.circular(AquaRadius.card),
        border: Border.all(color: status.warning.withValues(alpha: 0.65)),
      ),
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.md),
        child: LayoutBuilder(
          builder: (context, constraints) {
            final copy = Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Icon(Icons.wifi_off_rounded, color: status.warning),
                const SizedBox(width: AquaSpacing.sm),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Ostatni sterownik nie odpowiedział',
                        style: TextStyle(
                          color: status.onWarningContainer,
                          fontWeight: FontWeight.w800,
                        ),
                      ),
                      const SizedBox(height: AquaSpacing.xxs),
                      Text(
                        address == null
                            ? message
                            : '${address!.host} · $message',
                        style: TextStyle(color: status.onWarningContainer),
                      ),
                    ],
                  ),
                ),
              ],
            );
            final retry = OutlinedButton.icon(
              onPressed: onRetry,
              icon: const Icon(Icons.refresh_rounded),
              label: const Text('Ponów'),
              style: OutlinedButton.styleFrom(
                foregroundColor: status.onWarningContainer,
                side: BorderSide(color: status.warning),
              ),
            );
            if (constraints.maxWidth < 540) {
              return Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  copy,
                  const SizedBox(height: AquaSpacing.sm),
                  retry,
                ],
              );
            }
            return Row(
              children: [
                Expanded(child: copy),
                const SizedBox(width: AquaSpacing.md),
                retry,
              ],
            );
          },
        ),
      ),
    );
  }
}

class _ConnectionCard extends StatelessWidget {
  const _ConnectionCard({
    required this.icon,
    required this.eyebrow,
    required this.title,
    required this.description,
    required this.actionLabel,
    required this.accent,
    required this.capabilities,
    required this.onTap,
    this.primary = false,
  });

  final IconData icon;
  final String eyebrow;
  final String title;
  final String description;
  final String actionLabel;
  final Color accent;
  final List<String> capabilities;
  final VoidCallback onTap;
  final bool primary;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Card(
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.all(AquaSpacing.lg),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Container(
                    width: 54,
                    height: 54,
                    decoration: BoxDecoration(
                      color: accent.withValues(alpha: 0.14),
                      borderRadius: BorderRadius.circular(AquaRadius.control),
                    ),
                    child: Icon(icon, color: accent, size: 28),
                  ),
                  const SizedBox(width: AquaSpacing.sm),
                  Expanded(
                    child: Text(
                      eyebrow,
                      textAlign: TextAlign.end,
                      style: Theme.of(context).textTheme.labelSmall?.copyWith(
                        color: accent,
                        fontWeight: FontWeight.w900,
                        letterSpacing: 0.9,
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: AquaSpacing.md),
              Text(
                title,
                style: Theme.of(
                  context,
                ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w900),
              ),
              const SizedBox(height: AquaSpacing.xs),
              Text(
                description,
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                  color: colors.onSurfaceVariant,
                  height: 1.45,
                ),
              ),
              const SizedBox(height: AquaSpacing.md),
              Wrap(
                spacing: AquaSpacing.xs,
                runSpacing: AquaSpacing.xs,
                children: [
                  for (final capability in capabilities)
                    Chip(
                      visualDensity: VisualDensity.compact,
                      label: Text(capability),
                    ),
                ],
              ),
              const SizedBox(height: AquaSpacing.md),
              SizedBox(
                width: double.infinity,
                child: primary
                    ? FilledButton.icon(
                        onPressed: onTap,
                        icon: const Icon(Icons.arrow_forward_rounded),
                        label: Text(actionLabel),
                      )
                    : OutlinedButton.icon(
                        onPressed: onTap,
                        icon: const Icon(Icons.arrow_forward_rounded),
                        label: Text(actionLabel),
                      ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _CapabilityPill extends StatelessWidget {
  const _CapabilityPill({required this.icon, required this.label});

  final IconData icon;
  final String label;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return DecoratedBox(
      decoration: BoxDecoration(
        color: colors.surfaceContainerHighest.withValues(alpha: 0.7),
        borderRadius: BorderRadius.circular(AquaRadius.control),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 7),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 16, color: colors.primary),
            const SizedBox(width: 6),
            Flexible(
              child: Text(
                label,
                style: const TextStyle(fontWeight: FontWeight.w700),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _SecurityNote extends StatelessWidget {
  const _SecurityNote();

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Icon(Icons.lock_outline_rounded, size: 20, color: colors.primary),
        const SizedBox(width: AquaSpacing.sm),
        Expanded(
          child: Text(
            'Zmiany konfiguracji i sterowanie wymagają autoryzacji PIN. '
            'Dane sterownika pozostają w sieci lokalnej.',
            style: Theme.of(context).textTheme.bodySmall?.copyWith(
              color: colors.onSurfaceVariant,
              height: 1.4,
            ),
          ),
        ),
      ],
    );
  }
}
