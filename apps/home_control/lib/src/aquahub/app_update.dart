import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:crypto/crypto.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:http/http.dart' as http;

import '../home_control/strings.dart';

const homeControlVersionLabel = '2.2.0+8';

const _githubReleasesUri =
    'https://api.github.com/repos/Baartek57548/AkwariumCYD/releases?per_page=20';
const _platformChannelName = 'pl.aquacyd.aquacyd_home/app_update';
const _maximumMetadataBytes = 512 * 1024;
const _maximumApkBytes = 250 * 1024 * 1024;

final class InstalledAppVersion {
  const InstalledAppVersion({required this.version, required this.buildNumber});

  final String version;
  final int buildNumber;

  String get label => '$version+$buildNumber';
}

final class AppUpdateRelease {
  const AppUpdateRelease({
    required this.version,
    required this.buildNumber,
    required this.apkName,
    required this.apkUri,
    required this.bytes,
    required this.sha256Digest,
    required this.notes,
  });

  final String version;
  final int buildNumber;
  final String apkName;
  final Uri apkUri;
  final int bytes;
  final String sha256Digest;
  final String notes;

  String get label => '$version+$buildNumber';
}

enum AppInstallerResult { launched, permissionRequired }

abstract interface class AppUpdatePlatform {
  Future<InstalledAppVersion> installedVersion();

  Future<String> prepareApkPath(String fileName);

  Future<AppInstallerResult> installApk(String path);
}

final class MethodChannelAppUpdatePlatform implements AppUpdatePlatform {
  const MethodChannelAppUpdatePlatform();

  static const MethodChannel _channel = MethodChannel(_platformChannelName);

  @override
  Future<InstalledAppVersion> installedVersion() async {
    try {
      final value = await _channel.invokeMapMethod<String, Object?>(
        'getInstalledInfo',
      );
      final version = value?['version'];
      final buildNumber = value?['buildNumber'];
      if (version is! String ||
          version.trim().isEmpty ||
          buildNumber is! int ||
          buildNumber < 1) {
        throw const AppUpdateException(
          'System zwrócił nieprawidłową wersję aplikacji.',
        );
      }
      return InstalledAppVersion(
        version: version.trim(),
        buildNumber: buildNumber,
      );
    } on PlatformException catch (error) {
      throw AppUpdateException(
        error.message ?? 'Nie można odczytać wersji aplikacji.',
      );
    }
  }

  @override
  Future<String> prepareApkPath(String fileName) async {
    try {
      final path = await _channel.invokeMethod<String>(
        'prepareApkPath',
        <String, Object>{'fileName': fileName},
      );
      if (path == null || path.trim().isEmpty) {
        throw const AppUpdateException(
          'System nie przygotował bezpiecznego miejsca dla aktualizacji.',
        );
      }
      return path;
    } on PlatformException catch (error) {
      throw AppUpdateException(
        error.message ?? 'Nie można przygotować pliku aktualizacji.',
      );
    }
  }

  @override
  Future<AppInstallerResult> installApk(String path) async {
    try {
      final value = await _channel.invokeMapMethod<String, Object?>(
        'installApk',
        <String, Object>{'path': path},
      );
      return switch (value?['status']) {
        'launched' => AppInstallerResult.launched,
        'permissionRequired' => AppInstallerResult.permissionRequired,
        _ => throw const AppUpdateException(
          'System nie zwrócił stanu instalatora.',
        ),
      };
    } on PlatformException catch (error) {
      throw AppUpdateException(
        error.message ?? 'Nie udało się uruchomić instalatora Androida.',
      );
    }
  }
}

abstract interface class AppUpdateService {
  bool get supported;

  Future<InstalledAppVersion> installedVersion();

  Future<AppUpdateRelease?> findUpdate(InstalledAppVersion installed);

  Future<String> download(
    AppUpdateRelease release, {
    required ValueChanged<double> onProgress,
  });

  Future<AppInstallerResult> install(String apkPath);

  void close();
}

final class UnsupportedAppUpdateService implements AppUpdateService {
  const UnsupportedAppUpdateService();

  @override
  bool get supported => false;

  @override
  Future<InstalledAppVersion> installedVersion() =>
      throw const AppUpdateException(
        'Samodzielna aktualizacja APK jest dostępna wyłącznie na Androidzie.',
      );

  @override
  Future<AppUpdateRelease?> findUpdate(InstalledAppVersion installed) async =>
      null;

  @override
  Future<String> download(
    AppUpdateRelease release, {
    required ValueChanged<double> onProgress,
  }) => throw const AppUpdateException(
    'Samodzielna aktualizacja APK jest dostępna wyłącznie na Androidzie.',
  );

  @override
  Future<AppInstallerResult> install(String apkPath) =>
      throw const AppUpdateException(
        'Samodzielna aktualizacja APK jest dostępna wyłącznie na Androidzie.',
      );

  @override
  void close() {}
}

AppUpdateService createDefaultAppUpdateService() {
  if (kIsWeb || !Platform.isAndroid) {
    return const UnsupportedAppUpdateService();
  }
  return GitHubAppUpdateService(
    platform: const MethodChannelAppUpdatePlatform(),
  );
}

final class GitHubAppUpdateService implements AppUpdateService {
  GitHubAppUpdateService({
    required this.platform,
    http.Client? client,
    Uri? releasesUri,
  }) : _client = client ?? http.Client(),
       _ownsClient = client == null,
       releasesUri = releasesUri ?? Uri.parse(_githubReleasesUri);

  final AppUpdatePlatform platform;
  final Uri releasesUri;
  final http.Client _client;
  final bool _ownsClient;

  @override
  bool get supported => true;

  @override
  Future<InstalledAppVersion> installedVersion() => platform.installedVersion();

  @override
  Future<AppUpdateRelease?> findUpdate(InstalledAppVersion installed) async {
    final releasesJson = await _getJson(releasesUri);
    if (releasesJson is! List<Object?>) {
      throw const AppUpdateException(
        'Kanał aktualizacji zwrócił nieprawidłową listę wydań.',
      );
    }

    _GitHubRelease? latest;
    for (final item in releasesJson) {
      if (item is! Map<String, Object?>) continue;
      final candidate = _GitHubRelease.tryParse(item);
      if (candidate == null) continue;
      if (latest == null || candidate.version.compareTo(latest.version) > 0) {
        latest = candidate;
      }
    }
    if (latest == null) return null;

    final installedSemantic = _SemanticVersion.tryParse(installed.version);
    if (installedSemantic != null &&
        latest.version.compareTo(installedSemantic) < 0) {
      return null;
    }

    final manifestJson = await _getJson(latest.manifestUri);
    if (manifestJson is! Map<String, Object?>) {
      throw const AppUpdateException(
        'Manifest aktualizacji ma nieprawidłowy format.',
      );
    }
    final release = _parseManifest(latest, manifestJson);
    if (release.buildNumber <= installed.buildNumber) return null;
    if (installedSemantic != null &&
        latest.version.compareTo(installedSemantic) < 0) {
      return null;
    }
    return release;
  }

  @override
  Future<String> download(
    AppUpdateRelease release, {
    required ValueChanged<double> onProgress,
  }) async {
    final path = await platform.prepareApkPath(release.apkName);
    final file = File(path);
    IOSink? output;
    try {
      if (await file.exists()) await file.delete();
      await file.parent.create(recursive: true);

      final response = await _sendTrustedGet(
        release.apkUri,
        timeout: const Duration(seconds: 20),
      );
      if (response.statusCode != HttpStatus.ok) {
        throw AppUpdateException(
          'Serwer APK odpowiedział kodem ${response.statusCode}.',
        );
      }
      if (response.contentLength != null &&
          response.contentLength != release.bytes) {
        throw const AppUpdateException(
          'Rozmiar APK różni się od podpisanego manifestu wydania.',
        );
      }

      output = file.openWrite(mode: FileMode.writeOnly);
      final digestSink = _DigestSink();
      final hashInput = sha256.startChunkedConversion(digestSink);
      var received = 0;
      await for (final chunk in response.stream.timeout(
        const Duration(seconds: 30),
      )) {
        received += chunk.length;
        if (received > release.bytes || received > _maximumApkBytes) {
          throw const AppUpdateException(
            'Pobrany APK przekroczył dozwolony rozmiar.',
          );
        }
        output.add(chunk);
        hashInput.add(chunk);
        onProgress(received / release.bytes);
      }
      hashInput.close();
      await output.flush();
      await output.close();
      output = null;

      if (received != release.bytes) {
        throw const AppUpdateException('Pobrany APK jest niekompletny.');
      }
      final digest = digestSink.value?.toString().toLowerCase();
      if (digest == null || digest != release.sha256Digest) {
        throw const AppUpdateException(
          'Suma SHA-256 APK nie zgadza się z manifestem wydania.',
        );
      }
      onProgress(1);
      return file.path;
    } on AppUpdateException {
      await _deleteIfPresent(file);
      rethrow;
    } on TimeoutException {
      await _deleteIfPresent(file);
      throw const AppUpdateException(
        'Przekroczono czas pobierania aktualizacji.',
      );
    } on Object {
      await _deleteIfPresent(file);
      throw const AppUpdateException(
        'Nie udało się bezpiecznie pobrać aktualizacji.',
      );
    } finally {
      await output?.close();
    }
  }

  @override
  Future<AppInstallerResult> install(String apkPath) =>
      platform.installApk(apkPath);

  @override
  void close() {
    if (_ownsClient) _client.close();
  }

  Future<Object?> _getJson(Uri uri) async {
    if (uri.scheme != 'https') {
      throw const AppUpdateException(
        'Kanał aktualizacji musi używać połączenia HTTPS.',
      );
    }
    try {
      final response = await _sendTrustedGet(
        uri,
        timeout: const Duration(seconds: 15),
      );
      if (response.statusCode != HttpStatus.ok) {
        throw AppUpdateException(
          'Kanał aktualizacji odpowiedział kodem ${response.statusCode}.',
        );
      }
      final bytes = await _readLimited(
        response.stream.timeout(const Duration(seconds: 15)),
        _maximumMetadataBytes,
      );
      return jsonDecode(utf8.decode(bytes, allowMalformed: false));
    } on AppUpdateException {
      rethrow;
    } on FormatException {
      throw const AppUpdateException(
        'Kanał aktualizacji zwrócił uszkodzone metadane.',
      );
    } on TimeoutException {
      throw const AppUpdateException(
        'Przekroczono czas sprawdzania aktualizacji.',
      );
    } on Object {
      throw const AppUpdateException(
        'Nie udało się połączyć z kanałem aktualizacji.',
      );
    }
  }

  Future<http.StreamedResponse> _sendTrustedGet(
    Uri initialUri, {
    required Duration timeout,
  }) async {
    var uri = initialUri;
    for (var redirect = 0; redirect <= 5; redirect++) {
      if (!_isTrustedDownloadUri(uri)) {
        throw const AppUpdateException(
          'Kanał aktualizacji przekierował poza zaufaną domenę GitHub.',
        );
      }
      final request = http.Request('GET', uri)
        ..headers.addAll(_headers)
        ..followRedirects = false;
      final response = await _client.send(request).timeout(timeout);
      if (!_redirectStatusCodes.contains(response.statusCode)) {
        return response;
      }
      final location = response.headers['location'];
      await response.stream.drain<void>();
      if (location == null || location.trim().isEmpty) {
        throw const AppUpdateException(
          'Kanał aktualizacji zwrócił przekierowanie bez adresu.',
        );
      }
      uri = uri.resolve(location.trim());
    }
    throw const AppUpdateException(
      'Kanał aktualizacji przekroczył limit bezpiecznych przekierowań.',
    );
  }

  AppUpdateRelease _parseManifest(
    _GitHubRelease githubRelease,
    Map<String, Object?> manifest,
  ) {
    final version = githubRelease.version.toString();
    final expectedApkName = 'Home-Control-$version.apk';
    if (manifest['schemaVersion'] != 1 ||
        manifest['kind'] != 'home' ||
        manifest['tag'] != 'home-v$version' ||
        manifest['version'] != version) {
      throw const AppUpdateException(
        'Manifest nie należy do aplikacji Home Control.',
      );
    }
    final buildNumber = manifest['buildNumber'];
    final assets = manifest['assets'];
    if (buildNumber is! int || buildNumber < 1 || assets is! List<Object?>) {
      throw const AppUpdateException(
        'Manifest nie zawiera poprawnego numeru kompilacji.',
      );
    }

    Map<String, Object?>? apkEntry;
    for (final item in assets) {
      if (item is Map<String, Object?> && item['name'] == expectedApkName) {
        apkEntry = item;
        break;
      }
    }
    final bytes = apkEntry?['bytes'];
    final digest = apkEntry?['sha256'];
    if (bytes is! int ||
        bytes < 1024 * 1024 ||
        bytes > _maximumApkBytes ||
        digest is! String ||
        !RegExp(r'^[0-9a-fA-F]{64}$').hasMatch(digest)) {
      throw const AppUpdateException(
        'Manifest zawiera nieprawidłowe metadane APK.',
      );
    }
    if (githubRelease.apkName != expectedApkName ||
        githubRelease.apkBytes != bytes) {
      throw const AppUpdateException(
        'Plik APK na GitHubie różni się od manifestu wydania.',
      );
    }
    return AppUpdateRelease(
      version: version,
      buildNumber: buildNumber,
      apkName: expectedApkName,
      apkUri: githubRelease.apkUri,
      bytes: bytes,
      sha256Digest: digest.toLowerCase(),
      notes: githubRelease.notes,
    );
  }
}

enum AppUpdatePhase {
  idle,
  checking,
  upToDate,
  available,
  downloading,
  launchingInstaller,
  permissionRequired,
  failed,
  unsupported,
}

final class AppUpdateController extends ChangeNotifier {
  AppUpdateController({required this.service});

  final AppUpdateService service;

  AppUpdatePhase phase = AppUpdatePhase.idle;
  InstalledAppVersion? installed;
  AppUpdateRelease? release;
  double progress = 0;
  String? errorMessage;
  String? _downloadedApkPath;
  bool _disposed = false;

  bool get busy =>
      phase == AppUpdatePhase.checking ||
      phase == AppUpdatePhase.downloading ||
      phase == AppUpdatePhase.launchingInstaller;

  Future<void> initialize() => check();

  Future<void> check() async {
    if (busy) return;
    if (!service.supported) {
      phase = AppUpdatePhase.unsupported;
      _notify();
      return;
    }
    phase = AppUpdatePhase.checking;
    errorMessage = null;
    _notify();
    try {
      final current = await service.installedVersion();
      final update = await service.findUpdate(current);
      if (_disposed) return;
      installed = current;
      release = update;
      phase = update == null
          ? AppUpdatePhase.upToDate
          : AppUpdatePhase.available;
      _notify();
    } on AppUpdateException catch (error) {
      _fail(error.message);
    } on Object {
      _fail('Nie udało się sprawdzić aktualizacji aplikacji.');
    }
  }

  Future<void> downloadAndInstall() async {
    final update = release;
    if (update == null || busy) return;
    phase = AppUpdatePhase.downloading;
    progress = 0;
    errorMessage = null;
    _notify();
    try {
      _downloadedApkPath = await service.download(
        update,
        onProgress: (value) {
          if (_disposed) return;
          progress = value.clamp(0, 1);
          _notify();
        },
      );
      if (_disposed) return;
      await _launchInstaller();
    } on AppUpdateException catch (error) {
      _fail(error.message);
    } on Object {
      _fail('Nie udało się przygotować aktualizacji aplikacji.');
    }
  }

  Future<void> retryInstaller() async {
    if (_downloadedApkPath == null || busy) return;
    await _launchInstaller();
  }

  Future<void> onAppResumed() async {
    if (phase == AppUpdatePhase.permissionRequired && !busy) {
      await retryInstaller();
      return;
    }
    await check();
  }

  Future<void> _launchInstaller() async {
    final path = _downloadedApkPath;
    if (path == null) return;
    phase = AppUpdatePhase.launchingInstaller;
    errorMessage = null;
    _notify();
    try {
      final result = await service.install(path);
      if (_disposed) return;
      phase = switch (result) {
        AppInstallerResult.launched => AppUpdatePhase.upToDate,
        AppInstallerResult.permissionRequired =>
          AppUpdatePhase.permissionRequired,
      };
      _notify();
    } on AppUpdateException catch (error) {
      _fail(error.message);
    } on Object {
      _fail('Nie udało się uruchomić instalatora Androida.');
    }
  }

  void _fail(String message) {
    if (_disposed) return;
    phase = AppUpdatePhase.failed;
    errorMessage = message;
    _notify();
  }

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    service.close();
    super.dispose();
  }
}

final class AppUpdateScope extends InheritedNotifier<AppUpdateController> {
  const AppUpdateScope({
    required AppUpdateController controller,
    required super.child,
    super.key,
  }) : super(notifier: controller);

  static AppUpdateController? maybeOf(BuildContext context) =>
      context.dependOnInheritedWidgetOfExactType<AppUpdateScope>()?.notifier;
}

final class AppUpdateDialog extends StatelessWidget {
  const AppUpdateDialog({
    required this.controller,
    this.authorizeInstall,
    super.key,
  });

  final AppUpdateController controller;
  final Future<bool> Function()? authorizeInstall;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    return AnimatedBuilder(
      animation: controller,
      builder: (context, _) {
        final release = controller.release;
        final phase = controller.phase;
        if (phase == AppUpdatePhase.upToDate && release != null) {
          WidgetsBinding.instance.addPostFrameCallback((_) {
            if (context.mounted && Navigator.of(context).canPop()) {
              Navigator.of(context).pop();
            }
          });
        }
        return AlertDialog(
          icon: Icon(
            phase == AppUpdatePhase.failed
                ? Icons.error_outline_rounded
                : Icons.system_update_alt_rounded,
          ),
          title: Text(
            release == null
                ? strings.t('appUpdate')
                : strings.withValue('availableVersion', release.version),
          ),
          content: SizedBox(
            width: 440,
            child: ConstrainedBox(
              constraints: BoxConstraints(
                maxHeight: MediaQuery.sizeOf(context).height * 0.55,
              ),
              child: SingleChildScrollView(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: <Widget>[
                    Text(_dialogMessage(controller, strings)),
                    if (phase == AppUpdatePhase.downloading) ...<Widget>[
                      const SizedBox(height: 18),
                      LinearProgressIndicator(value: controller.progress),
                      const SizedBox(height: 8),
                      Text(
                        '${(controller.progress * 100).round()}%',
                        textAlign: TextAlign.center,
                      ),
                    ],
                    if (phase == AppUpdatePhase.failed &&
                        controller.errorMessage != null) ...<Widget>[
                      const SizedBox(height: 12),
                      Text(
                        controller.errorMessage!,
                        style: TextStyle(
                          color: Theme.of(context).colorScheme.error,
                        ),
                      ),
                    ],
                  ],
                ),
              ),
            ),
          ),
          actions: _dialogActions(
            context,
            controller,
            strings,
            authorizeInstall,
          ),
        );
      },
    );
  }
}

List<Widget> _dialogActions(
  BuildContext context,
  AppUpdateController controller,
  HomeControlStrings strings,
  Future<bool> Function()? authorizeInstall,
) {
  final phase = controller.phase;
  if (phase == AppUpdatePhase.downloading ||
      phase == AppUpdatePhase.launchingInstaller) {
    return const <Widget>[];
  }
  return <Widget>[
    TextButton(
      onPressed: () => Navigator.of(context).pop(),
      child: Text(strings.t('later')),
    ),
    if (phase == AppUpdatePhase.available || phase == AppUpdatePhase.failed)
      FilledButton.icon(
        onPressed: () =>
            _runAuthorized(authorizeInstall, controller.downloadAndInstall),
        icon: const Icon(Icons.download_rounded),
        label: Text(strings.t('downloadAndInstall')),
      ),
    if (phase == AppUpdatePhase.permissionRequired)
      FilledButton.icon(
        onPressed: () =>
            _runAuthorized(authorizeInstall, controller.retryInstaller),
        icon: const Icon(Icons.security_rounded),
        label: Text(strings.t('continueInstallation')),
      ),
  ];
}

Future<void> _runAuthorized(
  Future<bool> Function()? authorize,
  Future<void> Function() action,
) async {
  if (authorize != null && !await authorize()) return;
  await action();
}

String _dialogMessage(
  AppUpdateController controller,
  HomeControlStrings strings,
) {
  final release = controller.release;
  return switch (controller.phase) {
    AppUpdatePhase.available =>
      release?.notes.isNotEmpty == true
          ? release!.notes
          : strings.t('otaAvailableMessage'),
    AppUpdatePhase.downloading => strings.t('otaDownloadingMessage'),
    AppUpdatePhase.launchingInstaller => strings.t('otaVerifyingMessage'),
    AppUpdatePhase.permissionRequired => strings.t('otaPermissionMessage'),
    AppUpdatePhase.failed => strings.t('otaFailedMessage'),
    _ => strings.t('otaPreparingMessage'),
  };
}

final class AppUpdateException implements Exception {
  const AppUpdateException(this.message);

  final String message;

  @override
  String toString() => message;
}

final class _GitHubRelease {
  const _GitHubRelease({
    required this.version,
    required this.manifestUri,
    required this.apkName,
    required this.apkUri,
    required this.apkBytes,
    required this.notes,
  });

  final _SemanticVersion version;
  final Uri manifestUri;
  final String apkName;
  final Uri apkUri;
  final int apkBytes;
  final String notes;

  static _GitHubRelease? tryParse(Map<String, Object?> json) {
    if (json['draft'] == true || json['prerelease'] == true) return null;
    final tag = json['tag_name'];
    final assets = json['assets'];
    if (tag is! String ||
        !tag.startsWith('home-v') ||
        assets is! List<Object?>) {
      return null;
    }
    final version = _SemanticVersion.tryParse(tag.substring('home-v'.length));
    if (version == null) return null;
    final apkName = 'Home-Control-$version.apk';
    Uri? manifestUri;
    Uri? apkUri;
    int? apkBytes;
    for (final asset in assets) {
      if (asset is! Map<String, Object?>) continue;
      final name = asset['name'];
      final url = asset['browser_download_url'];
      final size = asset['size'];
      if (name is! String || url is! String) continue;
      final uri = Uri.tryParse(url);
      if (uri == null || !_isTrustedDownloadUri(uri)) continue;
      if (name == 'release-manifest.json') manifestUri = uri;
      if (name == apkName && size is int) {
        apkUri = uri;
        apkBytes = size;
      }
    }
    if (manifestUri == null || apkUri == null || apkBytes == null) return null;
    final body = json['body'];
    final notes = body is String ? body.trim() : '';
    return _GitHubRelease(
      version: version,
      manifestUri: manifestUri,
      apkName: apkName,
      apkUri: apkUri,
      apkBytes: apkBytes,
      notes: notes.length <= 4000 ? notes : notes.substring(0, 4000),
    );
  }
}

final class _SemanticVersion implements Comparable<_SemanticVersion> {
  const _SemanticVersion(this.major, this.minor, this.patch);

  final int major;
  final int minor;
  final int patch;

  static _SemanticVersion? tryParse(String value) {
    final match = RegExp(
      r'^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$',
    ).firstMatch(value);
    if (match == null) return null;
    return _SemanticVersion(
      int.parse(match.group(1)!),
      int.parse(match.group(2)!),
      int.parse(match.group(3)!),
    );
  }

  @override
  int compareTo(_SemanticVersion other) {
    final majorResult = major.compareTo(other.major);
    if (majorResult != 0) return majorResult;
    final minorResult = minor.compareTo(other.minor);
    if (minorResult != 0) return minorResult;
    return patch.compareTo(other.patch);
  }

  @override
  String toString() => '$major.$minor.$patch';
}

final class _DigestSink implements Sink<Digest> {
  Digest? value;

  @override
  void add(Digest data) {
    if (value != null) {
      throw StateError('SHA-256 converter returned more than one digest.');
    }
    value = data;
  }

  @override
  void close() {}
}

const Map<String, String> _headers = <String, String>{
  'accept': 'application/vnd.github+json',
  'x-github-api-version': '2022-11-28',
  'user-agent': 'Home-Control-OTA',
};

const Set<int> _redirectStatusCodes = <int>{
  HttpStatus.movedPermanently,
  HttpStatus.found,
  HttpStatus.seeOther,
  HttpStatus.temporaryRedirect,
  HttpStatus.permanentRedirect,
};

bool _isTrustedDownloadUri(Uri uri) {
  if (uri.scheme != 'https' || uri.userInfo.isNotEmpty) return false;
  final host = uri.host.toLowerCase();
  return host == 'github.com' ||
      host == 'api.github.com' ||
      host == 'objects.githubusercontent.com' ||
      host.endsWith('.githubusercontent.com');
}

Future<Uint8List> _readLimited(Stream<List<int>> stream, int limit) async {
  final bytes = BytesBuilder(copy: false);
  var length = 0;
  await for (final chunk in stream) {
    length += chunk.length;
    if (length > limit) {
      throw const AppUpdateException(
        'Metadane aktualizacji przekroczyły dozwolony rozmiar.',
      );
    }
    bytes.add(chunk);
  }
  return bytes.takeBytes();
}

Future<void> _deleteIfPresent(File file) async {
  try {
    if (await file.exists()) await file.delete();
  } on Object {
    // Usunięcie niekompletnego pliku jest sprzątaniem best-effort. Plik leży
    // wyłącznie w prywatnym cache aplikacji i nie może zostać zainstalowany.
  }
}
