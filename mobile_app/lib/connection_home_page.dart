import 'package:flutter/material.dart';

import 'ble_scanner_page.dart';
import 'controller_page.dart';
import 'full_controller/controller_session.dart';
import 'full_controller/controller_shell.dart';
import 'full_controller/wifi_connect_page.dart';

class ConnectionHomePage extends StatelessWidget {
  const ConnectionHomePage({super.key});

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
                        borderRadius: BorderRadius.circular(24),
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
                    Text(
                      'AquaCYD Control',
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.displaySmall?.copyWith(
                        fontWeight: FontWeight.w800,
                      ),
                    ),
                    Text(
                      'Inteligentne sterowanie akwarium',
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.titleMedium?.copyWith(
                        color: colors.onSurfaceVariant,
                      ),
                    ),
                    const SizedBox(height: 30),
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
                    const SizedBox(height: 12),
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
                    const SizedBox(height: 12),
                    _ModeCard(
                      icon: Icons.wifi_rounded,
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
        child: Padding(
          padding: const EdgeInsets.all(18),
          child: Row(
            children: [
              Container(
                width: 52,
                height: 52,
                decoration: BoxDecoration(
                  color: colors.primaryContainer,
                  borderRadius: BorderRadius.circular(16),
                ),
                child: Icon(icon, color: colors.primary),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    if (badge != null)
                      Padding(
                        padding: const EdgeInsets.only(bottom: 5),
                        child: Text(
                          badge!,
                          style: Theme.of(context).textTheme.labelSmall
                              ?.copyWith(
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
                    const SizedBox(height: 4),
                    Text(
                      description,
                      style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: colors.onSurfaceVariant,
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 8),
              const Icon(Icons.chevron_right_rounded),
            ],
          ),
        ),
      ),
    );
  }
}
