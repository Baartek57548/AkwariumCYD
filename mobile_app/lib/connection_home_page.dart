import 'package:flutter/material.dart';

import 'ble_scanner_page.dart';
import 'controller_page.dart';
import 'full_controller/controller_session.dart';
import 'full_controller/controller_shell.dart';
import 'full_controller/wifi_connect_page.dart';

class ConnectionHomePage extends StatelessWidget {
  const ConnectionHomePage({
    super.key,
    this.brandName = 'AquaCYD Control',
    this.showDevelopment = true,
    this.showLegacyWebView = true,
  });

  final String brandName;
  final bool showDevelopment;
  final bool showLegacyWebView;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Scaffold(
      body: DecoratedBox(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
            colors: [
              colors.primaryContainer,
              colors.surface,
              colors.tertiaryContainer.withValues(alpha: 0.55),
            ],
          ),
        ),
        child: SafeArea(
          child: Center(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(20),
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 560),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Center(
                      child: ClipRRect(
                        borderRadius: BorderRadius.circular(5),
                        child: Image.asset(
                          'assets/branding/aquacyd-control-icon.png',
                          width: 88,
                          height: 88,
                          fit: BoxFit.cover,
                          semanticLabel: 'Logo AquaCYD Control',
                        ),
                      ),
                    ),
                    const SizedBox(height: 12),
                    Semantics(
                      header: true,
                      child: Text(
                        brandName,
                        textAlign: TextAlign.center,
                        style: Theme.of(context).textTheme.headlineLarge
                            ?.copyWith(fontWeight: FontWeight.w800),
                      ),
                    ),
                    Text(
                      'Inteligentne sterowanie akwarium',
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.titleMedium?.copyWith(
                        color: colors.onSurfaceVariant,
                      ),
                    ),
                    const SizedBox(height: 28),
                    Semantics(
                      header: true,
                      child: Text(
                        'Wybierz sposób połączenia',
                        style: Theme.of(context).textTheme.titleLarge?.copyWith(
                          fontWeight: FontWeight.w800,
                        ),
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      'Możesz zmienić połączenie później z menu urządzenia.',
                      style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                        color: colors.onSurfaceVariant,
                      ),
                    ),
                    const SizedBox(height: 14),
                    _ModeCard(
                      icon: Icons.wifi_rounded,
                      title: 'Pełna aplikacja przez Wi‑Fi',
                      description:
                          'Natywny odpowiednik całego panelu WWW: wykresy, automatyka, harmonogramy, przekaźniki, logi, diagnostyka, ustawienia i OTA.',
                      badge: 'PEŁNA FUNKCJONALNOŚĆ',
                      onTap: () {
                        Navigator.of(context).push(
                          MaterialPageRoute<void>(
                            builder: (_) => const WifiConnectPage(),
                          ),
                        );
                      },
                    ),
                    const SizedBox(height: 12),
                    _ModeCard(
                      icon: Icons.bluetooth_searching_rounded,
                      title: 'Połącz przez Bluetooth BLE',
                      description:
                          'Telemetria i sterowanie bez sieci Wi‑Fi. Wymaga firmware z usługą cydAkwarium BLE.',
                      badge: 'ZALECANE',
                      onTap: () {
                        Navigator.of(context).push(
                          MaterialPageRoute<void>(
                            builder: (_) => const BleScannerPage(),
                          ),
                        );
                      },
                    ),
                    if (showDevelopment || showLegacyWebView) ...[
                      const SizedBox(height: 28),
                      Semantics(
                        header: true,
                        child: Text(
                          'Narzędzia i zgodność',
                          style: Theme.of(context).textTheme.titleLarge
                              ?.copyWith(fontWeight: FontWeight.w800),
                        ),
                      ),
                      const SizedBox(height: 4),
                      Text(
                        'Tryby pomocnicze do testowania i starszych wersji panelu.',
                        style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                          color: colors.onSurfaceVariant,
                        ),
                      ),
                      if (showDevelopment) ...[
                        const SizedBox(height: 14),
                        _ModeCard(
                          icon: Icons.science_outlined,
                          title: 'Uruchom tryb DEV',
                          description:
                              'Symulowane czujniki i moduły w pamięci RAM. Nie wykonuje operacji na sprzęcie.',
                          badge: 'BEZ SPRZĘTU',
                          onTap: () {
                            Navigator.of(context).push(
                              MaterialPageRoute<void>(
                                builder: (_) => ControllerShell(
                                  session: ControllerSession.development(),
                                ),
                              ),
                            );
                          },
                        ),
                      ],
                      if (showLegacyWebView) ...[
                        const SizedBox(height: 12),
                        _ModeCard(
                          icon: Icons.language_rounded,
                          title: 'Oryginalny panel WWW',
                          description:
                              'Tryb zgodności WebView — dokładny interfejs strony hostowanej przez sterownik.',
                          onTap: () {
                            Navigator.of(context).push(
                              MaterialPageRoute<void>(
                                builder: (_) => const ControllerPage(),
                              ),
                            );
                          },
                        ),
                      ],
                    ],
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _ModeCard extends StatelessWidget {
  const _ModeCard({
    required this.icon,
    required this.title,
    required this.description,
    required this.onTap,
    this.badge,
  });

  final IconData icon;
  final String title;
  final String description;
  final VoidCallback onTap;
  final String? badge;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Card(
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onTap,
        child: LayoutBuilder(
          builder: (context, constraints) {
            final compact =
                constraints.maxWidth < 360 ||
                MediaQuery.textScalerOf(context).scale(1) > 1.35;
            final modeIcon = ExcludeSemantics(
              child: Container(
                width: 52,
                height: 52,
                decoration: BoxDecoration(
                  color: colors.primaryContainer,
                  borderRadius: BorderRadius.circular(5),
                ),
                child: Icon(icon, color: colors.primary),
              ),
            );
            final heading = Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                if (badge != null)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 5),
                    child: Text(
                      badge!,
                      style: Theme.of(context).textTheme.labelSmall?.copyWith(
                        color: colors.primary,
                        fontWeight: FontWeight.w800,
                        letterSpacing: 0.8,
                      ),
                    ),
                  ),
                Text(
                  title,
                  style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ],
            );
            final descriptionWidget = Text(
              description,
              style: Theme.of(
                context,
              ).textTheme.bodySmall?.copyWith(color: colors.onSurfaceVariant),
            );

            if (compact) {
              return Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Row(
                      children: [
                        modeIcon,
                        const SizedBox(width: 14),
                        Expanded(child: heading),
                        const SizedBox(width: 8),
                        const Icon(Icons.chevron_right_rounded),
                      ],
                    ),
                    const SizedBox(height: 12),
                    descriptionWidget,
                  ],
                ),
              );
            }
            return Padding(
              padding: const EdgeInsets.all(18),
              child: Row(
                children: [
                  modeIcon,
                  const SizedBox(width: 16),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        heading,
                        const SizedBox(height: 4),
                        descriptionWidget,
                      ],
                    ),
                  ),
                  const SizedBox(width: 8),
                  const Icon(Icons.chevron_right_rounded),
                ],
              ),
            );
          },
        ),
      ),
    );
  }
}
