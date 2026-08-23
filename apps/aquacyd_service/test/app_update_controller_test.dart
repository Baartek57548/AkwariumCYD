import 'package:cyd_aquarium_mobile/app_update/app_update_controller.dart';
import 'package:cyd_aquarium_mobile/app_update/app_update_models.dart';
import 'package:cyd_aquarium_mobile/app_update/app_update_platform.dart';
import 'package:cyd_aquarium_mobile/app_update/github_update_service.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    SharedPreferences.setMockInitialValues({});
  });

  test('initialization exposes a newer compatible release', () async {
    final repository = _FakeRepository(_release('3.6.0'));
    final platform = _FakePlatform();
    final controller = AppUpdateController(
      service: repository,
      platform: platform,
      clock: () => DateTime.utc(2026, 7, 24, 12),
    );
    addTearDown(controller.dispose);

    await controller.start();

    expect(controller.state.phase, AppUpdatePhase.available);
    expect(controller.state.release?.version, const SemanticVersion(3, 6, 0));
    expect(repository.checkCount, 1);
  });

  test(
    'skipped release stays hidden automatically but manual check shows it',
    () async {
      final repository = _FakeRepository(_release('3.6.0'));
      final controller = AppUpdateController(
        service: repository,
        platform: _FakePlatform(),
        clock: () => DateTime.utc(2026, 7, 24, 12),
      );
      addTearDown(controller.dispose);

      await controller.start();
      await controller.skipCurrentVersion();
      await controller.checkForUpdates(manual: true);

      expect(controller.state.phase, AppUpdatePhase.available);
      expect(controller.state.isManual, isTrue);
      expect(repository.checkCount, 2);
    },
  );

  test('downloads and opens installer after validation', () async {
    final repository = _FakeRepository(_release('3.6.0'));
    final platform = _FakePlatform();
    final controller = AppUpdateController(
      service: repository,
      platform: platform,
    );
    addTearDown(controller.dispose);

    await controller.start();
    await controller.downloadAndInstall();

    expect(repository.downloadCount, 1);
    expect(platform.installCount, 1);
    expect(controller.state.phase, AppUpdatePhase.installerOpened);
    expect(platform.lastSha256, 'b' * 64);
    expect(platform.lastVersionName, '3.6.0');
  });

  test('coalesces concurrent checks and downloads', () async {
    final repository = _FakeRepository(_release('3.6.0'));
    final platform = _FakePlatform();
    final controller = AppUpdateController(
      service: repository,
      platform: platform,
      clock: () => DateTime.utc(2026, 7, 24, 12),
    );
    addTearDown(controller.dispose);

    await Future.wait([controller.start(), controller.onAppResumed()]);
    await Future.wait([
      controller.downloadAndInstall(),
      controller.downloadAndInstall(),
    ]);

    expect(repository.checkCount, 1);
    expect(repository.downloadCount, 1);
    expect(platform.installCount, 1);
  });

  test(
    'opens unknown-source settings and resumes installation after consent',
    () async {
      final repository = _FakeRepository(_release('3.6.0'));
      final platform = _FakePlatform()..allowPackageInstalls = false;
      final controller = AppUpdateController(
        service: repository,
        platform: platform,
      );
      addTearDown(controller.dispose);

      await controller.start();
      await controller.downloadAndInstall();

      expect(controller.state.phase, AppUpdatePhase.awaitingInstallPermission);
      expect(platform.settingsOpenCount, 1);

      platform.allowPackageInstalls = true;
      await controller.onAppResumed();

      expect(platform.installCount, 2);
      expect(controller.state.phase, AppUpdatePhase.installerOpened);
    },
  );

  test('automatic network failure never blocks app startup', () async {
    final repository = _FakeRepository(null)
      ..checkError = const AppUpdateException(
        'CHECK_OFFLINE',
        'Brak internetu.',
      );
    final controller = AppUpdateController(
      service: repository,
      platform: _FakePlatform(),
    );
    addTearDown(controller.dispose);

    await controller.start();

    expect(controller.state.phase, AppUpdatePhase.idle);
    expect(controller.state.message, isNull);
  });
}

class _FakeRepository implements AppUpdateRepository {
  _FakeRepository(this.latest);

  AppRelease? latest;
  AppUpdateException? checkError;
  int checkCount = 0;
  int downloadCount = 0;

  @override
  Future<AppRelease?> fetchLatestMobileRelease() async {
    checkCount++;
    final error = checkError;
    if (error != null) throw error;
    return latest;
  }

  @override
  Future<String> downloadApk({
    required AppRelease release,
    required String updateDirectory,
    required void Function(double progress) onProgress,
    required bool Function() isCanceled,
  }) async {
    downloadCount++;
    if (isCanceled()) throw const AppUpdateCanceledException();
    onProgress(0.5);
    onProgress(1);
    return '$updateDirectory/aquacyd-${release.version}.apk';
  }

  @override
  void dispose() {}
}

class _FakePlatform implements AppUpdatePlatform {
  bool allowPackageInstalls = true;
  int installCount = 0;
  int settingsOpenCount = 0;
  String? lastSha256;
  String? lastVersionName;

  @override
  Future<InstalledAppInfo> getInstallState() async {
    return InstalledAppInfo(
      packageName: AppUpdateController.productionPackageName,
      versionName: '3.5.1',
      versionCode: 11,
      sdkInt: 36,
      canRequestPackageInstalls: allowPackageInstalls,
    );
  }

  @override
  Future<String> getUpdateDirectory() async => '/private/updates';

  @override
  Future<void> installApk({
    required String path,
    required String expectedSha256,
    required String expectedVersionName,
  }) async {
    installCount++;
    lastSha256 = expectedSha256;
    lastVersionName = expectedVersionName;
    if (!allowPackageInstalls) {
      throw const AppUpdateException(
        'INSTALL_PERMISSION_REQUIRED',
        'Potrzebna zgoda.',
      );
    }
  }

  @override
  Future<void> openUnknownSourcesSettings() async {
    settingsOpenCount++;
  }
}

AppRelease _release(String version) {
  final parsed = SemanticVersion.tryParse(version)!;
  return AppRelease(
    version: parsed,
    tagName: 'mobile-v$version',
    title: 'AquaCYD Mobile $version',
    notes: 'Bezpieczne aktualizacje.',
    publishedAt: DateTime.utc(2026, 7, 24),
    releasePageUri: Uri.parse(
      'https://github.com/Baartek57548/AkwariumCYD/releases/tag/mobile-v$version',
    ),
    asset: ReleaseAsset(
      name: 'AquaCYD-Control-$version-current.apk',
      downloadUri: Uri.parse(
        'https://github.com/Baartek57548/AkwariumCYD/releases/download/mobile-v$version/AquaCYD-Control-$version-current.apk',
      ),
      size: 57 * 1024 * 1024,
      sha256: 'b' * 64,
      contentType: 'application/vnd.android.package-archive',
    ),
  );
}
