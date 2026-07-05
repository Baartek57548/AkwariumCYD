import 'package:flutter/material.dart';

import '../../display_refresh_rate.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../data_access.dart';
import '../widgets.dart';
import 'diagnostics_view.dart';
import 'logs_view.dart';
import 'settings_view.dart';
import 'system_view.dart';

class SettingsHubView extends StatelessWidget {
  const SettingsHubView({
    super.key,
    required this.session,
    required this.runAction,
    required this.ensureAdmin,
  });

  final ControllerSession session;
  final RunControllerAction runAction;
  final Future<bool> Function() ensureAdmin;

  void _open(BuildContext context, String title, Widget Function() builder) {
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (context) => Scaffold(
          appBar: AppBar(title: Text(title)),
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
    final display = status.section('display');
    final refresh = DisplayRefreshRateScope.stateOf(context);
    final active = refresh.activeMode;
    final requested = refresh.requestedMode;
    return ControllerPageBody(
      onRefresh: () => session.refresh(includeHistory: true),
      children: [
        const SectionHeader(title: 'Połączenie'),
        _SettingsGroup(
          children: [
            InfoRow(
              icon: session.isBluetooth
                  ? Icons.bluetooth_rounded
                  : Icons.wifi_rounded,
              label: 'Transport',
              value: session.displayName,
            ),
            InfoRow(
              icon: session.connected
                  ? Icons.check_circle_outline_rounded
                  : Icons.cloud_off_rounded,
              label: 'Stan sesji',
              value: session.connected ? 'Połączono' : 'Rozłączono',
            ),
            InfoRow(
              icon: Icons.router_outlined,
              label: 'Adres sterownika',
              value: session.baseUri?.host ?? network.text('ip', 'lokalny'),
            ),
            _SettingsLink(
              icon: Icons.wifi_rounded,
              title: 'Wi‑Fi i zegar',
              subtitle: 'Profil sieci, czas telefonu i synchronizacja NTP',
              onTap: () => _open(
                context,
                'Wi‑Fi i zegar',
                () => SettingsView(
                  session: session,
                  runAction: runAction,
                  ensureAdmin: ensureAdmin,
                ),
              ),
            ),
          ],
        ),
        const SectionHeader(title: 'Ekran'),
        _SettingsGroup(
          children: [
            const InfoRow(
              icon: Icons.contrast_rounded,
              label: 'Motyw aplikacji',
              value: 'Zgodny z telefonem',
            ),
            InfoRow(
              icon: Icons.brightness_6_rounded,
              label: 'Ekran sterownika',
              value:
                  '${display.integer('appliedBrightness', display.integer('brightness', 100))}%',
            ),
            InfoRow(
              icon: Icons.monitor_rounded,
              label: 'Obsługiwane',
              value: '${refresh.supportedRatesLabel} Hz',
            ),
            InfoRow(
              label: 'Maksymalne',
              value: refresh.maximumRefreshRate == null
                  ? 'brak danych'
                  : '${formatRefreshRate(refresh.maximumRefreshRate!)} Hz',
            ),
            InfoRow(
              label: 'Żądane',
              value: requested == null
                  ? 'nie ustawiono'
                  : '${requested.refreshRateLabel} Hz',
            ),
            InfoRow(
              label: 'Aktywne',
              value: '${formatRefreshRate(refresh.activeRefreshRate)} Hz',
            ),
            InfoRow(
              label: 'Tryb',
              value:
                  active?.label ??
                  'wartość diagnostyczna ${formatRefreshRate(refresh.activeRefreshRate)} Hz',
            ),
            if (refresh.error != null)
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 6, 16, 12),
                child: Text(
                  refresh.error!,
                  style: TextStyle(color: Theme.of(context).colorScheme.error),
                ),
              ),
          ],
        ),
        const SectionHeader(title: 'System'),
        _SettingsGroup(
          children: [
            _SettingsLink(
              icon: Icons.system_update_alt_rounded,
              title: 'Zasilanie i OTA',
              subtitle: 'Firmware, pamięć, tryb ECO i aktualizacja',
              onTap: () => _open(
                context,
                'Zasilanie i OTA',
                () => SystemView(
                  session: session,
                  runAction: runAction,
                  ensureAdmin: ensureAdmin,
                ),
              ),
            ),
            _SettingsLink(
              icon: Icons.receipt_long_rounded,
              title: 'Logi',
              subtitle: 'Zdarzenia zwykłe i krytyczne',
              onTap: () => _open(
                context,
                'Logi',
                () => LogsView(
                  session: session,
                  runAction: runAction,
                  ensureAdmin: ensureAdmin,
                ),
              ),
            ),
            _SettingsLink(
              icon: Icons.monitor_heart_rounded,
              title: 'Diagnostyka sprzętu',
              subtitle: 'I²C, OneWire, czujniki i stan magistral',
              onTap: () => _open(
                context,
                'Diagnostyka',
                () =>
                    DiagnosticsView(session: session, ensureAdmin: ensureAdmin),
              ),
            ),
            _SettingsLink(
              icon: Icons.admin_panel_settings_rounded,
              title: 'Administracja',
              subtitle: 'Restart, reset fabryczny i ekran CYD',
              onTap: () => _open(
                context,
                'Administracja',
                () => SettingsView(
                  session: session,
                  runAction: runAction,
                  ensureAdmin: ensureAdmin,
                ),
              ),
            ),
          ],
        ),
      ],
    );
  }
}

class _SettingsGroup extends StatelessWidget {
  const _SettingsGroup({required this.children});

  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    return Card(
      clipBehavior: Clip.antiAlias,
      child: Column(
        children: [
          for (var index = 0; index < children.length; index++) ...[
            children[index],
            if (index != children.length - 1) const Divider(height: 1),
          ],
        ],
      ),
    );
  }
}

class _SettingsLink extends StatelessWidget {
  const _SettingsLink({
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.onTap,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return ListTile(
      contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 6),
      leading: Icon(icon),
      title: Text(title, style: const TextStyle(fontWeight: FontWeight.w700)),
      subtitle: Text(subtitle),
      trailing: const Icon(Icons.chevron_right_rounded),
      onTap: onTap,
    );
  }
}
