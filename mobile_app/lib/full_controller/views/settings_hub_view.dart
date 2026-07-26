import 'package:flutter/material.dart';

import '../../app_settings.dart';
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

  Future<void> _enableExpertMode(BuildContext context) async {
    if (!await ensureAdmin() || !context.mounted) return;
    try {
      await AppSettings.setExpertMode(true);
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Tryb ekspercki został włączony.'),
          behavior: SnackBarBehavior.floating,
        ),
      );
    } on Object {
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Nie udało się zapisać trybu eksperckiego.'),
          behavior: SnackBarBehavior.floating,
        ),
      );
    }
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
          title: 'Ustawienia i diagnostyka',
          description:
              'Najważniejsze ustawienia sterownika i aplikacji oraz '
              'narzędzia serwisowe.',
        ),
        _SystemIdentityCard(
          device: status.text('device', 'cydAkwarium'),
          firmware: firmware.text('version', 'nieznana'),
          transport: session.displayName,
          ip: session.baseUri?.host ?? network.text('ip', 'lokalny'),
          uptime: hasStoredData ? formatUptime(system.integer('uptime')) : '—',
        ),
        const SizedBox(height: AquaSpacing.md),
        ResponsiveGrid(
          minimumChildWidth: 320,
          spacing: AquaSpacing.sm,
          children: [
            _SystemLinkCard(
              icon: Icons.router_rounded,
              title: 'Urządzenie i połączenie',
              description: 'Sieć Wi‑Fi, punkt dostępowy, zegar i ekran CYD.',
              status: hasStoredData
                  ? '${network.text('configuredStaSsid', 'Brak profilu')} · '
                        '${display.integer('appliedBrightness', display.integer('brightness'))}%'
                  : 'Brak zapisanej konfiguracji',
              enabled: advanced,
              disabledReason: 'Pełna konfiguracja wymaga Wi‑Fi lub BLE v2.',
              onTap: () => _open(
                context,
                'Urządzenie i połączenie',
                () => SettingsView(
                  session: session,
                  runAction: runAction,
                  ensureAdmin: ensureAdmin,
                ),
              ),
            ),
            _SystemLinkCard(
              icon: Icons.palette_outlined,
              title: 'Ustawienia aplikacji',
              description:
                  'Połączenie przy starcie, wygląd, język i aktualizacje aplikacji.',
              status:
                  '${formatRefreshRate(refresh.activeRefreshRate)} Hz · '
                  '${Theme.of(context).brightness == Brightness.dark ? "Dark" : "Light"}',
              onTap: () => _open(
                context,
                'Ustawienia aplikacji',
                () => const ProfileSettingsView(),
              ),
            ),
          ],
        ),
        const SizedBox(height: AquaSpacing.md),
        ValueListenableBuilder<bool>(
          valueListenable: AppSettings.expertModeNotifier,
          builder: (context, expertMode, _) => Card(
            margin: EdgeInsets.zero,
            clipBehavior: Clip.antiAlias,
            child: ExpansionTile(
              key: const Key('service-tools-section'),
              initiallyExpanded: false,
              leading: Icon(
                expertMode
                    ? Icons.build_circle_outlined
                    : Icons.lock_outline_rounded,
              ),
              title: const Text(
                'Narzędzia serwisowe',
                style: TextStyle(fontWeight: FontWeight.w900),
              ),
              subtitle: Text(
                expertMode
                    ? 'Diagnostyka, kanały oraz aktualizacje sterownika.'
                    : 'Ukryte w trybie prostym · wymagają PIN-u administratora.',
              ),
              childrenPadding: const EdgeInsets.fromLTRB(
                AquaSpacing.sm,
                0,
                AquaSpacing.sm,
                AquaSpacing.sm,
              ),
              children: [
                if (!expertMode)
                  _ExpertModeGate(onEnable: () => _enableExpertMode(context))
                else
                  ResponsiveGrid(
                    minimumChildWidth: 300,
                    spacing: AquaSpacing.sm,
                    children: [
                      _SystemLinkCard(
                        icon: Icons.memory_rounded,
                        title: 'Diagnostyka sprzętu',
                        description:
                            'Czujniki, magistrale i stan pamięci sterownika.',
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
                          () => DiagnosticsView(
                            session: session,
                            ensureAdmin: ensureAdmin,
                          ),
                        ),
                      ),
                      _SystemLinkCard(
                        icon: Icons.cable_rounded,
                        title: 'Kanały przekaźników',
                        description: 'Mapa kanałów układu MCP23017.',
                        enabled: false,
                        disabledReason: 'Niedostępne w tej wersji firmware.',
                        onTap: () {},
                      ),
                      _SystemLinkCard(
                        icon: Icons.system_update_alt_rounded,
                        title: 'Aktualizacje i zasilanie',
                        description: 'Firmware OTA, tryb ECO i pamięć SD.',
                        status: session.supportsFirmwareUpload
                            ? 'OTA dostępne'
                            : 'OTA tylko przez Wi‑Fi',
                        enabled: advanced,
                        disabledReason:
                            'Operacje systemowe wymagają Wi‑Fi lub BLE v2.',
                        onTap: () => _open(
                          context,
                          'Aktualizacje i zasilanie',
                          () => SystemView(
                            session: session,
                            runAction: runAction,
                            ensureAdmin: ensureAdmin,
                          ),
                        ),
                      ),
                    ],
                  ),
              ],
            ),
          ),
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
  });

  final String device;
  final String firmware;
  final String transport;
  final String ip;
  final String uptime;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Card(
      margin: EdgeInsets.zero,
      clipBehavior: Clip.antiAlias,
      color: colors.primaryContainer.withValues(alpha: 0.34),
      child: ExpansionTile(
        key: const Key('device-details-section'),
        leading: Container(
          width: 44,
          height: 44,
          decoration: BoxDecoration(
            color: colors.primary.withValues(alpha: 0.14),
            borderRadius: BorderRadius.circular(AquaRadius.control),
          ),
          child: Icon(Icons.developer_board_rounded, color: colors.primary),
        ),
        title: Text(
          device,
          style: Theme.of(
            context,
          ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w900),
        ),
        subtitle: const Text('Informacje o sterowniku'),
        childrenPadding: const EdgeInsets.fromLTRB(
          AquaSpacing.lg,
          0,
          AquaSpacing.lg,
          AquaSpacing.lg,
        ),
        children: [
          Align(
            alignment: Alignment.centerLeft,
            child: _IdentityMetric(
              icon: Icons.code_rounded,
              label: 'Firmware',
              value: firmware,
            ),
          ),
          const SizedBox(height: AquaSpacing.sm),
          Align(
            alignment: Alignment.centerLeft,
            child: _IdentityMetric(
              icon: Icons.swap_horiz_rounded,
              label: 'Połączenie',
              value: transport,
            ),
          ),
          const SizedBox(height: AquaSpacing.sm),
          Align(
            alignment: Alignment.centerLeft,
            child: _IdentityMetric(
              icon: Icons.lan_outlined,
              label: 'Adres',
              value: ip,
            ),
          ),
          const SizedBox(height: AquaSpacing.sm),
          Align(
            alignment: Alignment.centerLeft,
            child: _IdentityMetric(
              icon: Icons.schedule_rounded,
              label: 'Uptime',
              value: uptime,
            ),
          ),
        ],
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

class _ExpertModeGate extends StatelessWidget {
  const _ExpertModeGate({required this.onEnable});

  final VoidCallback onEnable;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(AquaSpacing.md),
      decoration: BoxDecoration(
        color: colors.surfaceContainerHighest.withValues(alpha: 0.55),
        borderRadius: BorderRadius.circular(AquaRadius.control),
        border: Border.all(color: colors.outlineVariant),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Icon(Icons.shield_outlined, color: colors.primary),
              const SizedBox(width: AquaSpacing.sm),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Tryb prosty chroni ustawienia serwisowe',
                      style: Theme.of(context).textTheme.titleSmall?.copyWith(
                        fontWeight: FontWeight.w900,
                      ),
                    ),
                    const SizedBox(height: AquaSpacing.xxs),
                    Text(
                      'Po autoryzacji PIN-em uzyskasz dostęp do diagnostyki, '
                      'kanałów sprzętowych, firmware OTA i operacji zasilania.',
                      style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: colors.onSurfaceVariant,
                        height: 1.4,
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: AquaSpacing.md),
          SizedBox(
            width: double.infinity,
            child: FilledButton.icon(
              key: const Key('enable-expert-mode-button'),
              onPressed: onEnable,
              icon: const Icon(Icons.admin_panel_settings_outlined),
              label: const Text('Odblokuj tryb ekspercki'),
            ),
          ),
        ],
      ),
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
