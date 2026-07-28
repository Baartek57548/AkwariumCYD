import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../controller_api.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../data_access.dart';
import '../firmware_package.dart';
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
  FirmwarePackage? firmwarePackage;
  double uploadProgress = 0;
  bool uploading = false;
  bool installingRelease = false;
  String? message;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted &&
          widget.session.firmwareReleaseStatus.phase ==
              FirmwareReleasePhase.idle) {
        unawaited(widget.session.checkForFirmwareUpdates());
      }
    });
  }

  Future<void> _pickFirmware() async {
    final result = await FilePicker.pickFiles(
      type: FileType.custom,
      allowedExtensions: const ['aqfw'],
      withData: true,
    );
    if (!mounted) return;
    final file = result?.files.singleOrNull;
    if (file == null) return;
    if (file.bytes == null || file.bytes!.isEmpty) {
      setState(() {
        firmware = null;
        firmwarePackage = null;
        message = 'Nie udało się odczytać zawartości pakietu firmware.';
      });
      return;
    }
    if (file.size > ControllerApi.maximumFirmwareBytes) {
      setState(() {
        firmware = null;
        firmwarePackage = null;
        message = 'Pakiet firmware przekracza bezpieczny limit rozmiaru.';
      });
      return;
    }
    try {
      final parsed = widget.session.inspectFirmwarePackage(
        file.bytes!,
        file.name,
      );
      widget.session.clearFirmwareUpdateStatus();
      setState(() {
        firmware = file;
        firmwarePackage = parsed;
        uploadProgress = 0;
        message =
            'Pakiet jest zgodny: ${parsed.firmwareVersion}, '
            '${parsed.target.label}, ${formatBytes(parsed.imageBytes)}.';
      });
    } on ControllerApiException catch (error) {
      setState(() {
        firmware = null;
        firmwarePackage = null;
        uploadProgress = 0;
        message = error.message;
      });
    } on Object {
      setState(() {
        firmware = null;
        firmwarePackage = null;
        uploadProgress = 0;
        message = 'Nie udało się bezpiecznie sprawdzić pakietu firmware.';
      });
    }
  }

  Future<void> _upload() async {
    final file = firmware;
    final package = firmwarePackage;
    if (file == null || file.bytes == null || package == null) return;
    final bytes = file.bytes!;
    final fileName = file.name;
    if (!await widget.ensureAdmin()) return;
    if (!mounted) return;
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Aktualizacja firmware OTA'),
        content: Text(
          'Zainstalować firmware ${package.firmwareVersion} dla '
          '${package.target.label}?\n\n'
          'Pakiet: ${formatBytes(package.packageBytes)}\n'
          'SHA-256: ${package.shortDigest}\n\n'
          'Nie wyłączaj sterownika podczas aktualizacji. Sterownik zweryfikuje '
          'podpis RSA przed zapisem i dopiero potem uruchomi się ponownie.',
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
    } on Object {
      if (mounted) {
        setState(
          () => message = 'Nieoczekiwany błąd podczas aktualizacji firmware.',
        );
      }
    } finally {
      if (mounted) setState(() => uploading = false);
    }
  }

  Future<void> _installAvailableRelease() async {
    final release = widget.session.firmwareReleaseStatus.release;
    if (release == null || installingRelease) return;
    final notes = release.notes.trim();
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: Text('Firmware ${release.version} jest dostępny'),
        content: SingleChildScrollView(
          child: Text(
            'Wariant: ${release.target.label}\n'
            'Rozmiar: ${release.formattedSize}\n\n'
            '${notes.isEmpty ? "Wydanie nie zawiera dodatkowych informacji." : notes}\n\n'
            'Pakiet zostanie pobrany z oficjalnego wydania GitHub, '
            'sprawdzony pod kątem rozmiaru, SHA-256 i zgodności, a następnie '
            'wysłany do sterownika. Podpis RSA zweryfikuje sterownik. '
            'Nie wyłączaj zasilania podczas instalacji.',
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Nie teraz'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('Pobierz i zainstaluj'),
          ),
        ],
      ),
    );
    if (confirmed != true || !mounted) return;
    setState(() {
      installingRelease = true;
      message = null;
    });
    try {
      final package = await widget.session.downloadAvailableFirmware();
      if (!mounted) return;
      if (!await widget.ensureAdmin() || !mounted) return;
      final result = await widget.session.uploadFirmware(
        package.bytes,
        release.asset.name,
      );
      if (mounted) setState(() => message = result.message);
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => message = error.message);
    } on Object {
      if (mounted) {
        setState(
          () => message = 'Nie udało się przygotować aktualizacji firmware.',
        );
      }
    } finally {
      if (mounted) setState(() => installingRelease = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final status = widget.session.status;
    final system = status.section('system');
    final eco = status.section('eco');
    final battery = status.section('battery');
    final firmwareData = status.section('firmware');
    final updateStatus = widget.session.firmwareUpdateStatus;
    final releaseStatus = widget.session.firmwareReleaseStatus;
    final availableRelease = releaseStatus.release;
    final releaseDownloading =
        releaseStatus.phase == FirmwareReleasePhase.downloading;
    final releaseCanceling =
        releaseStatus.phase == FirmwareReleasePhase.canceling;
    final releaseTransferring = releaseDownloading || releaseCanceling;
    final releaseActionable =
        availableRelease != null &&
        (releaseStatus.phase == FirmwareReleasePhase.available ||
            releaseStatus.phase == FirmwareReleasePhase.readyToInstall ||
            releaseStatus.phase == FirmwareReleasePhase.failed);
    final otaActive =
        uploading ||
        installingRelease ||
        updateStatus.isActive ||
        releaseStatus.isBusy;
    final progress = releaseTransferring
        ? releaseStatus.progress
        : updateStatus.isActive
        ? updateStatus.progress
        : uploadProgress;
    final otaMessage =
        releaseStatus.message ??
        (updateStatus.message.isNotEmpty ? updateStatus.message : message);
    final hasStoredData = widget.session.hasStatusData;
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
              value: hasStoredData
                  ? system.text('powerMode', '—').toUpperCase()
                  : '—',
              detail: hasStoredData
                  ? 'Czas pracy ${formatUptime(system.integer('uptime'))}'
                  : 'Brak zapisanego stanu',
            ),
            MetricTile(
              icon: Icons.bedtime_rounded,
              label: 'ECO',
              value: !hasStoredData
                  ? '—'
                  : eco.flag('safe_active')
                  ? 'AKTYWNY'
                  : 'OCZEKUJE',
              detail: !hasStoredData
                  ? 'Brak zapisanego stanu'
                  : eco.flag('deep_ready')
                  ? 'Deep sleep gotowy'
                  : 'Deep sleep zablokowany',
            ),
            MetricTile(
              icon: Icons.alarm_rounded,
              label: 'Następne wybudzenie',
              value: !hasStoredData
                  ? '—'
                  : eco.integer('wake_after_sec') > 0
                  ? formatUptime(eco.integer('wake_after_sec'))
                  : '--',
              detail: !hasStoredData
                  ? 'Brak zapisanego stanu'
                  : eco.flag('rtc_ready')
                  ? 'RTC gotowy'
                  : 'RTC niedostępny',
            ),
          ],
        ),
        const SizedBox(height: 12),
        StatusBanner(
          icon: !hasStoredData
              ? Icons.history_toggle_off_rounded
              : blockers.isEmpty
              ? Icons.verified_rounded
              : Icons.block_rounded,
          title: !hasStoredData
              ? 'Brak zapisanych danych ECO'
              : blockers.isEmpty
              ? 'Brak blokad ECO'
              : '${blockers.length} blokad ECO',
          message: !hasStoredData
              ? 'Połącz sterownik, aby sprawdzić gotowość zasilania, RTC '
                    'i bezpiecznego uśpienia.'
              : blockers.isEmpty
              ? 'Sterownik może przejść w bezpieczny tryb oszczędzania energii.'
              : blockers.join(' · '),
          isError: hasStoredData && blockers.isNotEmpty,
        ),
        const SectionHeader(
          title: 'Aktualizacja OTA',
          description:
              'Tylko podpisane pakiety AquaCYD dopasowane do tego sterownika.',
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
                            'Firmware ${firmwareData.text('version', 'nieznane')}',
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
                StatusBanner(
                  icon: widget.session.supportsFirmwareUpload
                      ? Icons.verified_user_rounded
                      : Icons.lock_rounded,
                  title: widget.session.supportsFirmwareUpload
                      ? 'Bezpieczne OTA gotowe'
                      : 'Aktualizacja zablokowana',
                  message:
                      widget.session.firmwareUpdateBlockReason ??
                      'Aplikacja sprawdzi format, wariant sprzętu, wersję, '
                          'identyfikator klucza, rozmiar oraz SHA-256 przed '
                          'wysłaniem. Podpis RSA zweryfikuje sterownik przed '
                          'zapisem obrazu.',
                  isError: !widget.session.supportsFirmwareUpload,
                ),
                const SizedBox(height: 12),
                if (releaseStatus.phase == FirmwareReleasePhase.checking) ...[
                  const LinearProgressIndicator(),
                  const SizedBox(height: 8),
                  const Text('Sprawdzanie najnowszego wydania firmware…'),
                  const SizedBox(height: 12),
                ],
                if (availableRelease != null) ...[
                  DecoratedBox(
                    decoration: BoxDecoration(
                      color: Theme.of(context).colorScheme.primaryContainer,
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: Padding(
                      padding: const EdgeInsets.all(16),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Row(
                            children: [
                              const Icon(Icons.new_releases_rounded),
                              const SizedBox(width: 10),
                              Expanded(
                                child: Text(
                                  'Dostępny firmware '
                                  '${availableRelease.version}',
                                  style: Theme.of(context).textTheme.titleMedium
                                      ?.copyWith(fontWeight: FontWeight.w900),
                                ),
                              ),
                            ],
                          ),
                          const SizedBox(height: 8),
                          Text(
                            '${availableRelease.target.label} · '
                            '${availableRelease.formattedSize}',
                          ),
                          if (availableRelease.notes.trim().isNotEmpty) ...[
                            const SizedBox(height: 8),
                            Text(
                              availableRelease.notes.trim(),
                              maxLines: 3,
                              overflow: TextOverflow.ellipsis,
                            ),
                          ],
                          const SizedBox(height: 14),
                          FilledButton.icon(
                            onPressed: releaseActionable && !otaActive
                                ? _installAvailableRelease
                                : null,
                            icon: releaseTransferring
                                ? const SizedBox.square(
                                    dimension: 18,
                                    child: CircularProgressIndicator(
                                      strokeWidth: 2,
                                    ),
                                  )
                                : const Icon(
                                    Icons.download_for_offline_rounded,
                                  ),
                            label: Text(
                              releaseStatus.phase ==
                                      FirmwareReleasePhase.readyToInstall
                                  ? 'Zainstaluj pobrany pakiet'
                                  : releaseCanceling
                                  ? 'Anulowanie…'
                                  : releaseStatus.phase ==
                                        FirmwareReleasePhase.failed
                                  ? 'Spróbuj ponownie'
                                  : 'Pobierz i zainstaluj',
                            ),
                          ),
                          if (releaseTransferring)
                            TextButton(
                              onPressed: releaseDownloading
                                  ? widget.session.cancelFirmwareDownload
                                  : null,
                              child: Text(
                                releaseCanceling
                                    ? 'Anulowanie pobierania…'
                                    : 'Anuluj pobieranie',
                              ),
                            ),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(height: 12),
                ] else if (releaseStatus.phase ==
                    FirmwareReleasePhase.upToDate) ...[
                  const StatusBanner(
                    icon: Icons.check_circle_outline_rounded,
                    title: 'Firmware jest aktualny',
                    message:
                        'Na GitHubie nie ma nowszej zgodnej wersji dla tego sterownika.',
                    isError: false,
                  ),
                  const SizedBox(height: 12),
                ],
                OutlinedButton.icon(
                  onPressed:
                      otaActive ||
                          !widget.session.supportsFirmwareUpload ||
                          releaseStatus.phase == FirmwareReleasePhase.checking
                      ? null
                      : () => widget.session.checkForFirmwareUpdates(
                          manual: true,
                        ),
                  icon: const Icon(Icons.refresh_rounded),
                  label: const Text('Sprawdź aktualizacje firmware'),
                ),
                const SizedBox(height: 8),
                ExpansionTile(
                  tilePadding: EdgeInsets.zero,
                  childrenPadding: const EdgeInsets.only(bottom: 4),
                  title: const Text('Instalacja z pliku .aqfw'),
                  subtitle: const Text(
                    'Opcja serwisowa dla pakietu pobranego wcześniej.',
                  ),
                  children: [
                    OutlinedButton.icon(
                      onPressed:
                          otaActive || !widget.session.supportsFirmwareUpload
                          ? null
                          : _pickFirmware,
                      icon: const Icon(Icons.folder_open_rounded),
                      label: Text(
                        !widget.session.supportsFirmwareUpload
                            ? 'Bezpieczne OTA jest niedostępne'
                            : firmware == null
                            ? 'Wybierz podpisany pakiet .aqfw'
                            : firmware!.name,
                      ),
                    ),
                    if (firmwarePackage != null) ...[
                      const SizedBox(height: 12),
                      DecoratedBox(
                        decoration: BoxDecoration(
                          color: Theme.of(
                            context,
                          ).colorScheme.surfaceContainerHighest,
                          borderRadius: BorderRadius.circular(12),
                        ),
                        child: Padding(
                          padding: const EdgeInsets.all(14),
                          child: Column(
                            children: [
                              InfoRow(
                                label: 'Wersja docelowa',
                                value: firmwarePackage!.firmwareVersion,
                              ),
                              InfoRow(
                                label: 'Wariant sprzętu',
                                value: firmwarePackage!.target.label,
                              ),
                              InfoRow(
                                label: 'Wersja zabezpieczeń',
                                value: '${firmwarePackage!.securityVersion}',
                              ),
                              InfoRow(
                                label: 'Podpis wydawniczy',
                                value: firmwarePackage!.keyId,
                              ),
                              InfoRow(
                                label: 'SHA-256',
                                value: firmwarePackage!.shortDigest,
                              ),
                            ],
                          ),
                        ),
                      ),
                    ],
                    const SizedBox(height: 12),
                    FilledButton.icon(
                      onPressed:
                          firmwarePackage == null ||
                              otaActive ||
                              !widget.session.supportsFirmwareUpload
                          ? null
                          : _upload,
                      icon: otaActive
                          ? const SizedBox.square(
                              dimension: 18,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            )
                          : const Icon(Icons.cloud_upload_rounded),
                      label: const Text('Sprawdź i zainstaluj plik'),
                    ),
                  ],
                ),
                if (otaActive || progress > 0) ...[
                  const SizedBox(height: 12),
                  LinearProgressIndicator(
                    value:
                        updateStatus.phase == FirmwareUpdatePhase.validating ||
                            (otaActive && progress <= 0)
                        ? null
                        : progress,
                  ),
                  const SizedBox(height: 6),
                  Text(
                    updateStatus.phase == FirmwareUpdatePhase.validating
                        ? 'Weryfikowanie pakietu…'
                        : releaseCanceling
                        ? 'Anulowanie pobierania…'
                        : releaseDownloading
                        ? 'Pobieranie ${(progress * 100).toStringAsFixed(0)}%'
                        : '${(progress * 100).toStringAsFixed(0)}%',
                  ),
                ],
                if (otaMessage != null && otaMessage.isNotEmpty) ...[
                  const SizedBox(height: 12),
                  StatusBanner(
                    icon:
                        updateStatus.isError ||
                            releaseStatus.phase == FirmwareReleasePhase.failed
                        ? Icons.error_outline_rounded
                        : updateStatus.phase ==
                                  FirmwareUpdatePhase.awaitingRestart ||
                              releaseStatus.phase ==
                                  FirmwareReleasePhase.awaitingRestart
                        ? Icons.restart_alt_rounded
                        : Icons.info_outline_rounded,
                    title:
                        updateStatus.isError ||
                            releaseStatus.phase == FirmwareReleasePhase.failed
                        ? 'Aktualizacja nie powiodła się'
                        : updateStatus.phase ==
                                  FirmwareUpdatePhase.awaitingRestart ||
                              releaseStatus.phase ==
                                  FirmwareReleasePhase.awaitingRestart
                        ? 'Pakiet przyjęty'
                        : 'Stan aktualizacji',
                    message: otaMessage,
                    isError:
                        updateStatus.isError ||
                        releaseStatus.phase == FirmwareReleasePhase.failed,
                  ),
                ],
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
                  value: hasStoredData
                      ? formatBytes(
                          system.integer(
                            'freeHeap',
                            status.integer('heap_free'),
                          ),
                        )
                      : '—',
                ),
                InfoRow(
                  label: 'Największy blok',
                  value: hasStoredData
                      ? formatBytes(
                          system.integer(
                            'largestHeap',
                            status.integer('heap_largest'),
                          ),
                        )
                      : '—',
                ),
                InfoRow(
                  label: 'Pojemność SD',
                  value: hasStoredData
                      ? formatBytes(status.integer('sd_total_bytes'))
                      : '—',
                ),
                InfoRow(
                  label: 'Zajęte SD',
                  value: hasStoredData
                      ? formatBytes(status.integer('sd_used_bytes'))
                      : '—',
                ),
                InfoRow(
                  label: 'Wolne SD',
                  value: hasStoredData
                      ? formatBytes(status.integer('sd_free_bytes'))
                      : '—',
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }
}
