import 'dart:convert';
import 'dart:io';

import 'package:aquacyd_home/src/aquahub/app_update.dart';
import 'package:crypto/crypto.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

void main() {
  late Directory temporaryDirectory;

  setUp(() async {
    temporaryDirectory = await Directory.systemTemp.createTemp(
      'aquacyd-home-update-test-',
    );
  });

  tearDown(() async {
    if (await temporaryDirectory.exists()) {
      await temporaryDirectory.delete(recursive: true);
    }
  });

  test('wykrywa wydanie Home i weryfikuje pobrany APK', () async {
    final apkBytes = List<int>.generate(
      1024 * 1024,
      (index) => index % 251,
      growable: false,
    );
    final digest = sha256.convert(apkBytes).toString();
    final platform = _FakeAppUpdatePlatform(temporaryDirectory);
    final service = GitHubAppUpdateService(
      platform: platform,
      releasesUri: Uri.parse(
        'https://api.github.com/repos/Baartek57548/AkwariumCYD/releases?per_page=20',
      ),
      client: _releaseClient(apkBytes: apkBytes, digest: digest),
    );
    addTearDown(service.close);

    final installed = await service.installedVersion();
    final release = await service.findUpdate(installed);

    expect(release, isNotNull);
    expect(release!.version, '1.1.2');
    expect(release.buildNumber, 4);
    expect(release.bytes, apkBytes.length);

    var progress = 0.0;
    final path = await service.download(
      release,
      onProgress: (value) => progress = value,
    );
    expect(await File(path).readAsBytes(), apkBytes);
    expect(progress, 1);
    expect(await service.install(path), AppInstallerResult.launched);
    expect(platform.lastInstalledPath, path);
  });

  test('odrzuca APK z sumą inną niż manifest i usuwa plik', () async {
    final apkBytes = List<int>.filled(1024 * 1024, 0x5A);
    final platform = _FakeAppUpdatePlatform(temporaryDirectory);
    final service = GitHubAppUpdateService(
      platform: platform,
      client: _releaseClient(
        apkBytes: apkBytes,
        digest: List<String>.filled(64, '0').join(),
      ),
    );
    addTearDown(service.close);

    final release = await service.findUpdate(await service.installedVersion());
    expect(release, isNotNull);
    await expectLater(
      service.download(release!, onProgress: (_) {}),
      throwsA(
        isA<AppUpdateException>().having(
          (error) => error.message,
          'message',
          contains('SHA-256'),
        ),
      ),
    );
    expect(await File(platform.pathFor(release.apkName)).exists(), isFalse);
  });

  test('kontroler ponawia instalację po zgodzie Androida', () async {
    final service = _PermissionAppUpdateService();
    final controller = AppUpdateController(service: service);
    addTearDown(controller.dispose);

    await controller.initialize();
    expect(controller.phase, AppUpdatePhase.available);

    await controller.downloadAndInstall();
    expect(controller.phase, AppUpdatePhase.permissionRequired);
    expect(service.installCalls, 1);

    await controller.onAppResumed();
    expect(controller.phase, AppUpdatePhase.upToDate);
    expect(service.installCalls, 2);
  });

  test('odrzuca przekierowanie poza zaufane domeny GitHub', () async {
    final service = GitHubAppUpdateService(
      platform: _FakeAppUpdatePlatform(temporaryDirectory),
      client: MockClient(
        (_) async => http.Response(
          '',
          HttpStatus.found,
          headers: const <String, String>{
            'location': 'https://updates.invalid/malicious.json',
          },
        ),
      ),
    );
    addTearDown(service.close);

    await expectLater(
      service.findUpdate(await service.installedVersion()),
      throwsA(
        isA<AppUpdateException>().having(
          (error) => error.message,
          'message',
          contains('zaufaną domenę GitHub'),
        ),
      ),
    );
  });
}

MockClient _releaseClient({
  required List<int> apkBytes,
  required String digest,
}) {
  final apkName = 'AquaCYD-Home-1.1.2.apk';
  return MockClient((request) async {
    if (request.url.host == 'api.github.com') {
      return http.Response(
        jsonEncode(<Object?>[
          <String, Object?>{
            'tag_name': 'home-v1.1.2',
            'draft': false,
            'prerelease': false,
            'body': 'Bezpieczna automatyczna aktualizacja aplikacji.',
            'assets': <Object?>[
              <String, Object?>{
                'name': apkName,
                'size': apkBytes.length,
                'browser_download_url':
                    'https://github.com/Baartek57548/AkwariumCYD/releases/download/home-v1.1.2/$apkName',
              },
              <String, Object?>{
                'name': 'release-manifest.json',
                'size': 512,
                'browser_download_url':
                    'https://github.com/Baartek57548/AkwariumCYD/releases/download/home-v1.1.2/release-manifest.json',
              },
            ],
          },
        ]),
        HttpStatus.ok,
        headers: const <String, String>{'content-type': 'application/json'},
      );
    }
    if (request.url.path.endsWith('/release-manifest.json')) {
      return http.Response(
        jsonEncode(<String, Object?>{
          'schemaVersion': 1,
          'tag': 'home-v1.1.2',
          'kind': 'home',
          'version': '1.1.2',
          'buildNumber': 4,
          'assets': <Object?>[
            <String, Object?>{
              'name': apkName,
              'bytes': apkBytes.length,
              'sha256': digest,
            },
          ],
        }),
        HttpStatus.ok,
        headers: const <String, String>{'content-type': 'application/json'},
      );
    }
    if (request.url.path.endsWith('/$apkName')) {
      return http.Response.bytes(
        apkBytes,
        HttpStatus.ok,
        headers: <String, String>{
          'content-type': 'application/vnd.android.package-archive',
          'content-length': '${apkBytes.length}',
        },
      );
    }
    return http.Response('not found', HttpStatus.notFound);
  });
}

final class _FakeAppUpdatePlatform implements AppUpdatePlatform {
  _FakeAppUpdatePlatform(this.directory);

  final Directory directory;
  String? lastInstalledPath;

  String pathFor(String fileName) =>
      '${directory.path}${Platform.pathSeparator}$fileName';

  @override
  Future<InstalledAppVersion> installedVersion() async =>
      const InstalledAppVersion(version: '1.1.1', buildNumber: 3);

  @override
  Future<String> prepareApkPath(String fileName) async => pathFor(fileName);

  @override
  Future<AppInstallerResult> installApk(String path) async {
    lastInstalledPath = path;
    return AppInstallerResult.launched;
  }
}

final class _PermissionAppUpdateService implements AppUpdateService {
  int installCalls = 0;

  @override
  bool get supported => true;

  @override
  Future<InstalledAppVersion> installedVersion() async =>
      const InstalledAppVersion(version: '1.1.1', buildNumber: 3);

  @override
  Future<AppUpdateRelease?> findUpdate(InstalledAppVersion installed) async =>
      AppUpdateRelease(
        version: '1.1.2',
        buildNumber: 4,
        apkName: 'AquaCYD-Home-1.1.2.apk',
        apkUri: Uri.parse('https://github.com/example/update.apk'),
        bytes: 1024 * 1024,
        sha256Digest: List<String>.filled(64, '0').join(),
        notes: 'Aktualizacja testowa',
      );

  @override
  Future<String> download(
    AppUpdateRelease release, {
    required ValueChanged<double> onProgress,
  }) async {
    onProgress(1);
    return 'verified.apk';
  }

  @override
  Future<AppInstallerResult> install(String apkPath) async {
    installCalls += 1;
    return installCalls == 1
        ? AppInstallerResult.permissionRequired
        : AppInstallerResult.launched;
  }

  @override
  void close() {}
}
