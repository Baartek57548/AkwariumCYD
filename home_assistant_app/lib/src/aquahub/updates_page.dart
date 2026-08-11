import 'package:flutter/material.dart';

import '../design/app_theme.dart';
import 'controller.dart';
import 'domain.dart';

final class HubUpdatesPage extends StatelessWidget {
  const HubUpdatesPage({required this.controller, super.key});

  final HubController controller;

  @override
  Widget build(BuildContext context) {
    final status = controller.updateStatus;
    return ListView(
      padding: const EdgeInsets.fromLTRB(20, 16, 20, 36),
      children: <Widget>[
        Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 1040),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: <Widget>[
                Text(
                  'Centrum aktualizacji',
                  style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                    fontWeight: FontWeight.w800,
                  ),
                ),
                const SizedBox(height: 6),
                Text(
                  'Bezpieczne wydania AquaHub i podgląd wersji wszystkich urządzeń.',
                  style: TextStyle(
                    color: Theme.of(context).colorScheme.onSurfaceVariant,
                  ),
                ),
                const SizedBox(height: 18),
                _HubFirmwareCard(controller: controller, status: status),
                const SizedBox(height: 18),
                _AppReleaseCard(),
                const SizedBox(height: 18),
                _DeviceFirmwareCard(controller: controller),
              ],
            ),
          ),
        ),
      ],
    );
  }
}

final class _HubFirmwareCard extends StatelessWidget {
  const _HubFirmwareCard({required this.controller, required this.status});

  final HubController controller;
  final HubUpdateStatus? status;

  @override
  Widget build(BuildContext context) {
    final value = status;
    final phase = value?.phase;
    final release = value?.release;
    final busy = controller.updating || phase?.busy == true;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(22),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: <Widget>[
            Row(
              children: <Widget>[
                CircleAvatar(
                  backgroundColor: AquaColors.cyan.withValues(alpha: 0.16),
                  child: const Icon(Icons.memory_rounded),
                ),
                const SizedBox(width: 14),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: <Widget>[
                      Text(
                        'AquaHub ESP32-P4',
                        style: Theme.of(context).textTheme.titleLarge?.copyWith(
                          fontWeight: FontWeight.w800,
                        ),
                      ),
                      Text(
                        value == null
                            ? 'Odczytywanie stanu…'
                            : 'Wersja ${value.currentVersion} · security ${value.currentSecurityVersion}',
                        style: TextStyle(
                          color: Theme.of(context).colorScheme.onSurfaceVariant,
                        ),
                      ),
                    ],
                  ),
                ),
                if (value != null) _PhaseChip(phase: value.phase),
              ],
            ),
            const SizedBox(height: 20),
            if (value == null)
              const LinearProgressIndicator()
            else if (!value.supported)
              _Notice(
                icon: Icons.settings_ethernet_outlined,
                title: 'Kanał OTA nie jest skonfigurowany',
                text:
                    'Ustaw bezpieczny AQUAHUB_OTA_BASE_URL w firmware P4. Do tego czasu aplikacja świadomie blokuje instalację.',
              )
            else ...<Widget>[
              if (phase == HubUpdatePhase.failed)
                _Notice(
                  icon: Icons.error_outline_rounded,
                  title: 'Aktualizacja nie powiodła się',
                  text: value.error.isEmpty
                      ? 'Sprawdź źródło wydania i ponów operację.'
                      : value.error,
                  error: true,
                ),
              if (release != null &&
                  (phase == HubUpdatePhase.available ||
                      phase == HubUpdatePhase.downloading ||
                      phase == HubUpdatePhase.verifying ||
                      phase == HubUpdatePhase.rebooting)) ...<Widget>[
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: <Widget>[
                    Chip(label: Text('Wersja ${release.version}')),
                    Chip(label: Text(_fileSize(release.sizeBytes))),
                    Chip(label: Text('security ${release.securityVersion}')),
                    if (release.mandatory)
                      Chip(
                        avatar: const Icon(Icons.security_rounded, size: 18),
                        label: const Text('Wymagana'),
                        backgroundColor: Theme.of(
                          context,
                        ).colorScheme.errorContainer,
                      ),
                  ],
                ),
                if (release.notes.isNotEmpty) ...<Widget>[
                  const SizedBox(height: 12),
                  Text(release.notes),
                ],
              ],
              if (phase == HubUpdatePhase.downloading ||
                  phase == HubUpdatePhase.verifying ||
                  phase == HubUpdatePhase.rebooting) ...<Widget>[
                const SizedBox(height: 18),
                LinearProgressIndicator(
                  value: phase == HubUpdatePhase.rebooting
                      ? null
                      : value.progressPercent / 100,
                ),
                const SizedBox(height: 8),
                Text(
                  phase == HubUpdatePhase.downloading
                      ? '${value.progressPercent}% · ${_fileSize(value.bytesReceived)} z ${_fileSize(value.totalBytes)}'
                      : phase == HubUpdatePhase.verifying
                      ? 'Weryfikacja obrazu i ustawianie partycji A/B…'
                      : 'AquaHub uruchamia nową wersję. Połączenie wróci automatycznie.',
                  textAlign: TextAlign.center,
                ),
              ],
              const SizedBox(height: 20),
              Wrap(
                spacing: 10,
                runSpacing: 10,
                alignment: WrapAlignment.end,
                children: <Widget>[
                  OutlinedButton.icon(
                    onPressed: busy ? null : controller.checkForUpdates,
                    icon: busy && phase == HubUpdatePhase.checking
                        ? const SizedBox.square(
                            dimension: 18,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Icon(Icons.refresh_rounded),
                    label: const Text('Sprawdź wydania'),
                  ),
                  if (phase == HubUpdatePhase.available)
                    FilledButton.icon(
                      onPressed: busy
                          ? null
                          : () =>
                                _confirmInstall(context, controller, release!),
                      icon: const Icon(Icons.system_update_alt_rounded),
                      label: const Text('Zainstaluj'),
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

final class _AppReleaseCard extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return Card(
      child: ListTile(
        contentPadding: const EdgeInsets.all(18),
        leading: const CircleAvatar(child: Icon(Icons.phone_android_rounded)),
        title: const Text(
          'Aplikacja AquaHub 1.1.0',
          style: TextStyle(fontWeight: FontWeight.w700),
        ),
        subtitle: const Text(
          'Aktualizacje Android/iOS są dystrybuowane przez podpisany sklep lub firmowy kanał MDM. Firmware urządzeń aktualizuje centrala, nie telefon.',
        ),
        trailing: const Icon(Icons.verified_user_outlined),
      ),
    );
  }
}

final class _DeviceFirmwareCard extends StatelessWidget {
  const _DeviceFirmwareCard({required this.controller});

  final HubController controller;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: <Widget>[
            Text(
              'Firmware urządzeń',
              style: Theme.of(
                context,
              ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
            ),
            const SizedBox(height: 6),
            Text(
              'Urządzenia ogłaszają wersję przez discovery. Przycisk instalacji pojawi się dopiero po ogłoszeniu encji typu update przez dane urządzenie.',
              style: TextStyle(
                color: Theme.of(context).colorScheme.onSurfaceVariant,
              ),
            ),
            const SizedBox(height: 14),
            if (controller.devices.isEmpty)
              const Text('Brak zarejestrowanych urządzeń.')
            else
              ...controller.devices.map(
                (device) => ListTile(
                  contentPadding: EdgeInsets.zero,
                  leading: Icon(
                    device.online
                        ? Icons.check_circle_outline_rounded
                        : Icons.cloud_off_outlined,
                    color: device.online
                        ? AquaColors.green
                        : Theme.of(context).colorScheme.onSurfaceVariant,
                  ),
                  title: Text(device.name),
                  subtitle: Text(
                    device.model.isEmpty ? device.id : device.model,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                  trailing: Text(
                    device.firmwareVersion.isEmpty
                        ? 'nieznana'
                        : device.firmwareVersion,
                    style: const TextStyle(fontWeight: FontWeight.w700),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

final class _PhaseChip extends StatelessWidget {
  const _PhaseChip({required this.phase});

  final HubUpdatePhase phase;

  @override
  Widget build(BuildContext context) {
    final (label, icon) = switch (phase) {
      HubUpdatePhase.disabled => ('Wyłączone', Icons.block_rounded),
      HubUpdatePhase.idle => ('Gotowe', Icons.check_circle_outline),
      HubUpdatePhase.checking => ('Sprawdzanie', Icons.sync_rounded),
      HubUpdatePhase.available => ('Dostępna', Icons.new_releases_outlined),
      HubUpdatePhase.upToDate => ('Aktualna', Icons.verified_outlined),
      HubUpdatePhase.downloading => ('Pobieranie', Icons.downloading_rounded),
      HubUpdatePhase.verifying => ('Weryfikacja', Icons.security_rounded),
      HubUpdatePhase.rebooting => ('Restart', Icons.restart_alt_rounded),
      HubUpdatePhase.failed => ('Błąd', Icons.error_outline_rounded),
    };
    return Chip(avatar: Icon(icon, size: 18), label: Text(label));
  }
}

final class _Notice extends StatelessWidget {
  const _Notice({
    required this.icon,
    required this.title,
    required this.text,
    this.error = false,
  });

  final IconData icon;
  final String title;
  final String text;
  final bool error;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: error ? scheme.errorContainer : scheme.surfaceContainerHighest,
        borderRadius: BorderRadius.circular(16),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          Icon(icon, color: error ? scheme.onErrorContainer : scheme.primary),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                Text(
                  title,
                  style: const TextStyle(fontWeight: FontWeight.w700),
                ),
                const SizedBox(height: 3),
                Text(text),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

Future<void> _confirmInstall(
  BuildContext context,
  HubController controller,
  HubUpdateRelease release,
) async {
  final confirmed = await showDialog<bool>(
    context: context,
    builder: (context) => AlertDialog(
      icon: const Icon(Icons.system_update_alt_rounded),
      title: Text('Zainstalować ${release.version}?'),
      content: const Text(
        'Panel uruchomi się ponownie. Sterownik CYD zachowa autonomiczne sterowanie akwarium, ale panel i aplikacja będą chwilowo offline. Nie odłączaj zasilania.',
      ),
      actions: <Widget>[
        TextButton(
          onPressed: () => Navigator.pop(context, false),
          child: const Text('Anuluj'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(context, true),
          child: const Text('Rozpocznij OTA'),
        ),
      ],
    ),
  );
  if (confirmed != true || !context.mounted) return;
  final accepted = await controller.installUpdate();
  if (!accepted && context.mounted) {
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(
          controller.errorMessage ?? 'AquaHub odrzucił aktualizację.',
        ),
      ),
    );
  }
}

String _fileSize(int bytes) {
  if (bytes >= 1024 * 1024) {
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MiB';
  }
  if (bytes >= 1024) return '${(bytes / 1024).toStringAsFixed(0)} KiB';
  return '$bytes B';
}
