import 'package:flutter/material.dart';

import '../../data/home_assistant_socket.dart';
import '../../design/app_theme.dart';
import '../../domain/entity_ids.dart';
import '../../state/aquacyd_controller.dart';
import '../widgets/common.dart';

class SystemPage extends StatelessWidget {
  const SystemPage({required this.controller, super.key});

  final AquaCydController controller;

  @override
  Widget build(BuildContext context) {
    final snapshot = controller.snapshot;
    final config = controller.config;
    final credentials = controller.credentials;
    final missing = AquaEntityIds.all
        .where((id) => !controller.entities.containsKey(id))
        .toList(growable: false);
    return AquaPage(
      children: <Widget>[
        const SectionTitle(
          title: 'System',
          subtitle: 'Home Assistant, łączność oraz diagnostyka bramki',
        ),
        const SizedBox(height: 16),
        _InfoCard(
          title: config?.locationName ?? 'Home Assistant',
          icon: Icons.home_work_outlined,
          rows: <_InfoRow>[
            _InfoRow('Adres', credentials?.baseUri.toString() ?? '—'),
            _InfoRow('Wersja HA', config?.version ?? '—'),
            _InfoRow('Strefa czasowa', config?.timeZone ?? '—'),
            _InfoRow(
              'Transport',
              credentials?.baseUri.scheme == 'https'
                  ? 'HTTPS + WSS'
                  : 'Lokalne HTTP + WS',
            ),
            _InfoRow('WebSocket', _socketLabel(controller.socketStatus)),
          ],
        ),
        const SizedBox(height: 14),
        _InfoCard(
          title: 'Sterownik AquaCYD',
          icon: Icons.memory_rounded,
          rows: <_InfoRow>[
            _InfoRow(
              'Encje dostępne',
              '${snapshot.availableEntityCount} / ${AquaEntityIds.all.length}',
            ),
            _InfoRow(
              'Wi-Fi bramki',
              formatMeasurement(
                snapshot.number(AquaEntityIds.wifiRssi),
                'dBm',
                decimals: 0,
              ),
            ),
            _InfoRow(
              'ESP-NOW',
              formatMeasurement(
                snapshot.number(AquaEntityIds.espNowRssi),
                'dBm',
                decimals: 0,
              ),
            ),
            _InfoRow(
              'Czas pracy',
              formatUptime(snapshot.entity(AquaEntityIds.uptime)?.integer),
            ),
            _InfoRow(
              'Wolna pamięć',
              _formatBytes(snapshot.number(AquaEntityIds.freeHeap)),
            ),
            _InfoRow(
              'Rewizja konfiguracji',
              snapshot.entity(AquaEntityIds.configurationRevision)?.state ??
                  '—',
            ),
            _InfoRow('Ostatnie dane', formatUpdated(snapshot.lastUpdated)),
          ],
        ),
        const SizedBox(height: 14),
        Card(
          child: ExpansionTile(
            tilePadding: const EdgeInsets.symmetric(
              horizontal: 20,
              vertical: 6,
            ),
            childrenPadding: const EdgeInsets.fromLTRB(20, 0, 20, 18),
            leading: Icon(
              missing.isEmpty
                  ? Icons.check_circle_outline_rounded
                  : Icons.info_outline_rounded,
              color: missing.isEmpty ? AquaColors.green : AquaColors.amber,
            ),
            title: Text(
              missing.isEmpty
                  ? 'Komplet encji gotowy'
                  : 'Brakuje ${missing.length} encji',
              style: const TextStyle(fontWeight: FontWeight.w800),
            ),
            subtitle: const Text('Diagnostyka integracji MQTT Discovery'),
            children: <Widget>[
              if (missing.isEmpty)
                const Align(
                  alignment: Alignment.centerLeft,
                  child: Text(
                    'Home Assistant udostępnia wszystkie encje wymagane przez aplikację.',
                  ),
                )
              else
                for (final entityId in missing)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 7),
                    child: Align(
                      alignment: Alignment.centerLeft,
                      child: SelectableText(
                        entityId,
                        style: Theme.of(context).textTheme.bodySmall,
                      ),
                    ),
                  ),
            ],
          ),
        ),
        const SizedBox(height: 26),
        const SectionTitle(title: 'Połączenie aplikacji'),
        const SizedBox(height: 12),
        Wrap(
          spacing: 12,
          runSpacing: 12,
          children: <Widget>[
            FilledButton.tonalIcon(
              onPressed: controller.beginReconfiguration,
              icon: const Icon(Icons.edit_outlined),
              label: const Text('Zmień serwer lub token'),
            ),
            OutlinedButton.icon(
              onPressed: controller.isBusy('logout')
                  ? null
                  : () => _confirmLogout(context),
              icon: const Icon(Icons.logout_rounded),
              label: const Text('Usuń konfigurację'),
            ),
          ],
        ),
        const SizedBox(height: 18),
        Text(
          'Token nie jest wyświetlany ani zapisywany w logach. Usunięcie '
          'konfiguracji kasuje go z bezpiecznego magazynu urządzenia.',
          style: Theme.of(context).textTheme.bodySmall?.copyWith(
            color: Theme.of(context).colorScheme.onSurfaceVariant,
          ),
        ),
      ],
    );
  }

  Future<void> _confirmLogout(BuildContext context) async {
    final confirmed =
        await showDialog<bool>(
          context: context,
          builder: (context) => AlertDialog(
            title: const Text('Usunąć konfigurację?'),
            content: const Text(
              'Adres serwera i token zostaną usunięte z tego urządzenia. '
              'Konfiguracja Home Assistanta i sterownika nie zostanie zmieniona.',
            ),
            actions: <Widget>[
              TextButton(
                onPressed: () => Navigator.pop(context, false),
                child: const Text('Anuluj'),
              ),
              FilledButton(
                onPressed: () => Navigator.pop(context, true),
                child: const Text('Usuń'),
              ),
            ],
          ),
        ) ??
        false;
    if (confirmed) {
      await controller.logout();
    }
  }

  static String _socketLabel(HomeAssistantSocketStatus status) {
    return switch (status) {
      HomeAssistantSocketStatus.connected => 'połączony — dane na żywo',
      HomeAssistantSocketStatus.connecting => 'łączenie',
      HomeAssistantSocketStatus.disconnected => 'rozłączony — działa REST',
      HomeAssistantSocketStatus.unauthorized => 'token odrzucony',
    };
  }

  static String _formatBytes(double? bytes) {
    if (bytes == null || !bytes.isFinite || bytes < 0) {
      return '—';
    }
    if (bytes >= 1024 * 1024) {
      return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
    }
    return '${(bytes / 1024).toStringAsFixed(0)} kB';
  }
}

class _InfoCard extends StatelessWidget {
  const _InfoCard({
    required this.title,
    required this.icon,
    required this.rows,
  });

  final String title;
  final IconData icon;
  final List<_InfoRow> rows;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: <Widget>[
            Row(
              children: <Widget>[
                Container(
                  padding: const EdgeInsets.all(10),
                  decoration: BoxDecoration(
                    color: Theme.of(context).colorScheme.primaryContainer,
                    borderRadius: BorderRadius.circular(13),
                  ),
                  child: Icon(icon),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    title,
                    style: Theme.of(context).textTheme.titleLarge?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 16),
            for (var index = 0; index < rows.length; index++) ...<Widget>[
              if (index > 0) const Divider(height: 18),
              Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: <Widget>[
                  Expanded(
                    child: Text(
                      rows[index].label,
                      style: TextStyle(
                        color: Theme.of(context).colorScheme.onSurfaceVariant,
                      ),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Flexible(
                    child: Text(
                      rows[index].value,
                      textAlign: TextAlign.right,
                      style: const TextStyle(fontWeight: FontWeight.w700),
                    ),
                  ),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _InfoRow {
  const _InfoRow(this.label, this.value);

  final String label;
  final String value;
}
