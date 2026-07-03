import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../controller_api.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../data_access.dart';
import '../widgets.dart';

class SystemView extends StatefulWidget {
  const SystemView({
    super.key,
    required this.session,
    required this.runAction,
    required this.ensureAdmin,
  });

  final ControllerSession session;
  final RunControllerAction runAction;
  final Future<bool> Function() ensureAdmin;

  @override
  State<SystemView> createState() => _SystemViewState();
}

class _SystemViewState extends State<SystemView> {
  PlatformFile? firmware;
  double uploadProgress = 0;
  bool uploading = false;
  String? message;

  Future<void> _pickFirmware() async {
    final result = await FilePicker.pickFiles(
      type: FileType.custom,
      allowedExtensions: const ['bin'],
      withData: true,
    );
    final file = result?.files.singleOrNull;
    if (file == null) return;
    if (file.bytes == null || file.bytes!.isEmpty) {
      setState(() => message = 'Nie udało się odczytać zawartości firmware.');
      return;
    }
    if (file.size > ControllerApi.maximumFirmwareBytes) {
      setState(() => message = 'Firmware przekracza limit 8 MB.');
      return;
    }
    setState(() {
      firmware = file;
      uploadProgress = 0;
      message = 'Wybrano ${file.name} (${formatBytes(file.size)}).';
    });
  }

  Future<void> _upload() async {
    final file = firmware;
    if (file == null || file.bytes == null) return;
    final bytes = file.bytes!;
    final fileName = file.name;
    if (!await widget.ensureAdmin()) return;
    if (!mounted) return;
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Aktualizacja firmware OTA'),
        content: Text(
          'Wgrać $fileName? Nie wyłączaj sterownika podczas aktualizacji. Po zapisie ESP32 uruchomi się ponownie.',
        ),
        actions: [
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
    if (confirmed != true) return;
    setState(() {
      uploading = true;
      uploadProgress = 0;
      message = 'Wysyłanie firmware…';
    });
    try {
      final result = await widget.session.uploadFirmware(
        bytes,
        fileName,
        onProgress: (sent, total) {
          if (mounted) {
            setState(() => uploadProgress = total == 0 ? 0 : sent / total);
          }
        },
      );
      if (mounted) setState(() => message = result.message);
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => message = error.message);
    } finally {
      if (mounted) setState(() => uploading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final status = widget.session.status;
    final system = status.section('system');
    final eco = status.section('eco');
    final battery = status.section('battery');
    final firmwareData = status.section('firmware');
    final blockers = eco
        .list('blockers')
        .map((item) => item.toString())
        .toList();
    return ControllerPageBody(
      children: [
        const SectionHeader(
          title: 'Zasilanie i tryb ECO',
          description:
              'Stan energetyczny, gotowość RTC i blokady bezpiecznego uśpienia.',
        ),
        ResponsiveGrid(
          children: [
            MetricTile(
              icon: Icons.battery_5_bar_rounded,
              label: 'Bateria RTC',
              value: battery.nullableNumber('voltage') == null
                  ? '--'
                  : '${battery.number('voltage').toStringAsFixed(2)} V',
              detail: battery['percent'] == null
                  ? 'Brak telemetrii'
                  : '${battery.integer('percent')}%',
            ),
            MetricTile(
              icon: Icons.power_settings_new_rounded,
              label: 'Profil zasilania',
              value: system.text('powerMode', 'normal').toUpperCase(),
              detail: 'Czas pracy ${formatUptime(system.integer('uptime'))}',
            ),
            MetricTile(
              icon: Icons.bedtime_rounded,
              label: 'ECO',
              value: eco.flag('safe_active') ? 'AKTYWNY' : 'OCZEKUJE',
              detail: eco.flag('deep_ready')
                  ? 'Deep sleep gotowy'
                  : 'Deep sleep zablokowany',
            ),
            MetricTile(
              icon: Icons.alarm_rounded,
              label: 'Następne wybudzenie',
              value: eco.integer('wake_after_sec') > 0
                  ? formatUptime(eco.integer('wake_after_sec'))
                  : '--',
              detail: eco.flag('rtc_ready') ? 'RTC gotowy' : 'RTC niedostępny',
            ),
          ],
        ),
        const SizedBox(height: 12),
        StatusBanner(
          icon: blockers.isEmpty ? Icons.verified_rounded : Icons.block_rounded,
          title: blockers.isEmpty
              ? 'Brak blokad ECO'
              : '${blockers.length} blokad ECO',
          message: blockers.isEmpty
              ? 'Sterownik może przejść w bezpieczny tryb oszczędzania energii.'
              : blockers.join(' · '),
          isError: blockers.isNotEmpty,
        ),
        const SectionHeader(
          title: 'Aktualizacja OTA',
          description:
              'Wysyłanie do endpointu /update z tą samą kontrolą PIN i limitem pliku co WWW.',
        ),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(18),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Row(
                  children: [
                    const Icon(Icons.system_update_alt_rounded, size: 34),
                    const SizedBox(width: 12),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            'Firmware ${firmwareData.text('version', 'dev')}',
                            style: const TextStyle(
                              fontWeight: FontWeight.w800,
                              fontSize: 17,
                            ),
                          ),
                          Text(
                            'Build ${firmwareData.text('buildDate', '—')} ${firmwareData.text('buildTime')}',
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                OutlinedButton.icon(
                  onPressed: uploading ? null : _pickFirmware,
                  icon: const Icon(Icons.folder_open_rounded),
                  label: Text(
                    firmware == null
                        ? 'Wybierz plik firmware.bin'
                        : firmware!.name,
                  ),
                ),
                if (uploading || uploadProgress > 0) ...[
                  const SizedBox(height: 12),
                  LinearProgressIndicator(value: uploadProgress),
                  const SizedBox(height: 6),
                  Text('${(uploadProgress * 100).toStringAsFixed(0)}%'),
                ],
                const SizedBox(height: 12),
                FilledButton.icon(
                  onPressed: firmware == null || uploading ? null : _upload,
                  icon: uploading
                      ? const SizedBox.square(
                          dimension: 18,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.cloud_upload_rounded),
                  label: Text(uploading ? 'Wysyłanie OTA…' : 'Wgraj firmware'),
                ),
              ],
            ),
          ),
        ),
        const SectionHeader(title: 'Informacje pamięci'),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                InfoRow(
                  label: 'Wolny heap',
                  value: formatBytes(
                    system.integer('freeHeap', status.integer('heap_free')),
                  ),
                ),
                InfoRow(
                  label: 'Największy blok',
                  value: formatBytes(
                    system.integer(
                      'largestHeap',
                      status.integer('heap_largest'),
                    ),
                  ),
                ),
                InfoRow(
                  label: 'Pojemność SD',
                  value: formatBytes(status.integer('sd_total_bytes')),
                ),
                InfoRow(
                  label: 'Zajęte SD',
                  value: formatBytes(status.integer('sd_used_bytes')),
                ),
                InfoRow(
                  label: 'Wolne SD',
                  value: formatBytes(status.integer('sd_free_bytes')),
                ),
              ],
            ),
          ),
        ),
        if (message != null)
          Padding(
            padding: const EdgeInsets.only(top: 10),
            child: Text(message!),
          ),
      ],
    );
  }
}
