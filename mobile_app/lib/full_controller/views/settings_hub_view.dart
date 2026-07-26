import 'package:flutter/material.dart';

import '../../design_system.dart';
import '../../display_refresh_rate.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../data_access.dart';
import '../widgets.dart';
import 'diagnostics_view.dart';
import 'profile_settings_view.dart';
import 'settings_view.dart';
import 'system_view.dart';

class SettingsHubView extends StatelessWidget {
  const SettingsHubView({
    super.key,
    required this.session,
    required this.runAction,
    required this.ensureAdmin,
    this.onOpenConnection,
  });

  final ControllerSession session;
  final RunControllerAction runAction;
  final Future<bool> Function() ensureAdmin;
  final VoidCallback? onOpenConnection;

  void _open(BuildContext context, String title, Widget Function() builder) {
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (context) => Scaffold(
          appBar: AppBar(
            title: Text(title),
            actions: [
              if (onOpenConnection != null)
                IconButton(
                  key: const Key('subpage-connection-center-button'),
                  tooltip: 'Połączenia Wi‑Fi i Bluetooth',
                  onPressed: onOpenConnection,
                  icon: const Icon(Icons.hub_rounded),
                ),
              const SizedBox(width: 4),
            ],
          ),
          body: AnimatedBuilder(
            animation: session,
            builder: (context, _) => builder(),
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final status = session.status;
    final network = status.section('network');
    final system = status.section('system');
    final firmware = status.section('firmware');
    final display = status.section('display');
    final refresh = DisplayRefreshRateScope.stateOf(context);
    final advanced = session.supportsAdvancedConfiguration;
    final hasStoredData = session.hasStatusData;
    return ControllerPageBody(
      maxWidth: 980,
      padding: const EdgeInsets.fromLTRB(14, 16, 14, 28),
      onRefresh: () => session.refresh(),
      children: [
        const SectionHeader(
          title: 'System i administracja',
          description:
              'Konfiguracja sterownika, kondycja sprzętu, firmware oraz '
              'preferencje aplikacji.',
        ),
        _SystemIdentityCard(
          device: status.text('device', 'cydAkwarium'),
          firmware: firmware.text('version', 'nieznana'),
          transport: session.displayName,
          ip: session.baseUri?.host ?? network.text('ip', 'lokalny'),
          uptime: hasStoredData ? formatUptime(system.integer('uptime')) : '—',
          freeHeap: hasStoredData
              ? formatBytes(
                  system.integer('freeHeap', status.integer('heap_free')),
                )
              : '—',
        ),
        const SizedBox(height: AquaSpacing.md),
        ResponsiveGrid(
          minimumChildWidth: 320,
          spacing: AquaSpacing.sm,
          children: [
            _SystemLinkCard(
              icon: Icons.router_rounded,
              title: 'Sterownik, sieć i ekran',
              description:
                  'Wi‑Fi, punkt dostępowy, zegar, NTP, jasność i profil ekranu CYD.',
              status: hasStoredData
                  ? '${network.text('configuredStaSsid', 'Brak profilu')} · '
                        '${display.integer('appliedBrightness', display.integer('brightness'))}%'
                  : 'Brak zapisanej konfiguracji',
              enabled: advanced,
              disabledReason: 'Pełna konfiguracja wymaga Wi‑Fi lub BLE v2.',
              onTap: () => _open(
                context,
                'Konfiguracja sterownika',
                () => SettingsView(
                  session: session,
                  runAction: runAction,
                  ensureAdmin: ensureAdmin,
                ),
              ),
            ),
            _SystemLinkCard(
              icon: Icons.memory_rounded,
              title: 'Diagnostyka sprzętu',
              description:
                  'Magistrale I²C i OneWire, czujniki, pamięć oraz przyczyna restartu.',
              status: session.canIssueCommands
                  ? 'Gotowa do skanowania'
                  : session.hasCachedSnapshot
                  ? 'Ostatni zapis · tylko odczyt'
                  : 'Brak zapisanych danych',
              enabled: advanced,
              disabledReason: 'Diagnostyka wymaga Wi‑Fi lub BLE v2.',
              onTap: () => _open(
                context,
                'Diagnostyka sprzętu',
                () =>
                    DiagnosticsView(session: session, ensureAdmin: ensureAdmin),
              ),
            ),
            _SystemLinkCard(
              icon: Icons.cable_rounded,
              title: 'Kanały przekaźników',
              description:
                  'Mapa ośmiu kanałów MCP23017 i ich stanów awaryjnych.',
              status: 'Edycja zablokowana bezpiecznie',
              enabled: false,
              disabledReason:
                  'Bieżący firmware zapisuje profil, ale jeszcze go nie '
                  'stosuje. Funkcja wróci po dodaniu odczytu, walidacji i '
                  'atomowego rollbacku po stronie sterownika.',
              onTap: () {},
            ),
            _SystemLinkCard(
              icon: Icons.system_update_alt_rounded,
              title: 'Firmware, energia i OTA',
              description:
                  'Aktualizacja sterownika, tryb ECO, pamięć SD, restart i reset.',
              status: session.supportsFirmwareUpload
                  ? 'OTA dostępne'
                  : 'OTA tylko przez Wi‑Fi',
              enabled: advanced,
              disabledReason:
                  'Operacje systemowe nie są dostępne w ograniczonym BLE.',
              onTap: () => _open(
                context,
                'Firmware i zasilanie',
                () => SystemView(
                  session: session,
                  runAction: runAction,
                  ensureAdmin: ensureAdmin,
                ),
              ),
            ),
            _SystemLinkCard(
              icon: Icons.palette_outlined,
              title: 'Aplikacja i operator',
              description:
                  'Motyw, profil użytkownika, odświeżanie ekranu i aktualizacje aplikacji.',
              status:
                  '${formatRefreshRate(refresh.activeRefreshRate)} Hz · '
                  '${Theme.of(context).brightness == Brightness.dark ? "Dark" : "Light"}',
              onTap: () => _open(
                context,
                'Aplikacja i operator',
                () => const ProfileSettingsView(),
              ),
            ),
          ],
        ),
        if (session.isLegacyBluetooth) ...[
          const SizedBox(height: AquaSpacing.md),
          const StatusBanner(
            icon: Icons.info_outline_rounded,
            title: 'Tryb zgodności BLE v1',
            message:
                'Widoczne są wyłącznie funkcje potwierdzone przez protokół. '
                'Połącz sterownik przez Wi‑Fi, aby uzyskać diagnostykę, '
                'historię, konfigurację i OTA.',
            isError: false,
          ),
        ],
      ],
    );
  }
}

class _SystemIdentityCard extends StatelessWidget {
  const _SystemIdentityCard({
    required this.device,
    required this.firmware,
    required this.transport,
    required this.ip,
    required this.uptime,
    required this.freeHeap,
  });

  final String device;
  final String firmware;
  final String transport;
  final String ip;
  final String uptime;
  final String freeHeap;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Card(
      color: colors.primaryContainer.withValues(alpha: 0.34),
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.lg),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Container(
                  width: 52,
                  height: 52,
                  decoration: BoxDecoration(
                    color: colors.primary.withValues(alpha: 0.14),
                    borderRadius: BorderRadius.circular(AquaRadius.control),
                  ),
                  child: Icon(
                    Icons.developer_board_rounded,
                    color: colors.primary,
                  ),
                ),
                const SizedBox(width: AquaSpacing.md),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        device,
                        style: Theme.of(context).textTheme.titleLarge?.copyWith(
                          fontWeight: FontWeight.w900,
                        ),
                      ),
                      Text(
                        '$firmware · $transport · $ip',
                        style: TextStyle(color: colors.onSurfaceVariant),
                      ),
                    ],
                  ),
                ),
              ],
            ),
            const SizedBox(height: AquaSpacing.md),
            Wrap(
              spacing: AquaSpacing.lg,
              runSpacing: AquaSpacing.sm,
              children: [
                _IdentityMetric(
                  icon: Icons.schedule_rounded,
                  label: 'Uptime',
                  value: uptime,
                ),
                _IdentityMetric(
                  icon: Icons.memory_rounded,
                  label: 'Wolna pamięć',
                  value: freeHeap,
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _IdentityMetric extends StatelessWidget {
  const _IdentityMetric({
    required this.icon,
    required this.label,
    required this.value,
  });

  final IconData icon;
  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: 18, color: colors.primary),
        const SizedBox(width: AquaSpacing.xs),
        Flexible(
          child: Text.rich(
            TextSpan(
              children: [
                TextSpan(
                  text: '$label: ',
                  style: TextStyle(color: colors.onSurfaceVariant),
                ),
                TextSpan(
                  text: value,
                  style: const TextStyle(fontWeight: FontWeight.w800),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }
}

class _SystemLinkCard extends StatelessWidget {
  const _SystemLinkCard({
    required this.icon,
    required this.title,
    required this.description,
    required this.onTap,
    this.status,
    this.enabled = true,
    this.disabledReason,
  });

  final IconData icon;
  final String title;
  final String description;
  final String? status;
  final bool enabled;
  final String? disabledReason;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Card(
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: enabled ? onTap : null,
        child: Padding(
          padding: const EdgeInsets.all(AquaSpacing.md),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Container(
                width: 44,
                height: 44,
                decoration: BoxDecoration(
                  color: colors.primaryContainer,
                  borderRadius: BorderRadius.circular(AquaRadius.control),
                ),
                child: Icon(
                  icon,
                  color: enabled
                      ? colors.onPrimaryContainer
                      : colors.onSurfaceVariant,
                ),
              ),
              const SizedBox(width: AquaSpacing.sm),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      title,
                      style: const TextStyle(fontWeight: FontWeight.w900),
                    ),
                    const SizedBox(height: AquaSpacing.xxs),
                    Text(
                      enabled ? description : disabledReason ?? description,
                      style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: colors.onSurfaceVariant,
                        height: 1.35,
                      ),
                    ),
                    if (status != null) ...[
                      const SizedBox(height: AquaSpacing.xs),
                      Text(
                        status!,
                        style: Theme.of(context).textTheme.labelMedium
                            ?.copyWith(
                              color: enabled
                                  ? colors.primary
                                  : colors.onSurfaceVariant,
                              fontWeight: FontWeight.w800,
                            ),
                      ),
                    ],
                  ],
                ),
              ),
              const SizedBox(width: AquaSpacing.xs),
              Icon(
                enabled ? Icons.chevron_right_rounded : Icons.lock_rounded,
                color: colors.onSurfaceVariant,
              ),
            ],
          ),
        ),
      ),
    );
  }
}
