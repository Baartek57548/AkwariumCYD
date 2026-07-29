import 'dart:io';

import 'package:cyd_aquarium_mobile/alarm_center/alarm_models.dart';
import 'package:cyd_aquarium_mobile/alarm_center/alarm_notifications.dart';
import 'package:cyd_aquarium_mobile/app_update/app_update_background.dart';
import 'package:cyd_aquarium_mobile/app_update/app_update_models.dart';
import 'package:cyd_aquarium_mobile/app_update/app_update_preferences.dart';
import 'package:cyd_aquarium_mobile/app_update/github_update_service.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    SharedPreferences.setMockInitialValues(<String, Object>{});
  });

  test(
    'notifies once for the same newer release and never downloads without consent',
    () async {
      final preferences = AppUpdatePreferences(
        await SharedPreferences.getInstance(),
      );
      await preferences.recordInstalledVersion(const SemanticVersion(5, 1, 1));
      final repository = _FakeUpdateRepository(_release);
      final notifications = _RecordingNotificationSink();
      final connectivity = _RecordingConnectivity(isWifiValue: true);
      final runner = AppUpdateBackgroundRunner(
        preferencesLoader: () async => preferences,
        repository: repository,
        notifications: notifications,
        connectivity: connectivity,
        supportDirectory: () async =>
            throw StateError('Download directory must not be requested.'),
      );

      final first = await runner.run();
      final second = await runner.run();

      expect(first.succeeded, isTrue);
      expect(first.notified, isTrue);
      expect(first.downloaded, isFalse);
      expect(second.succeeded, isTrue);
      expect(second.reason, 'deduplicated');
      expect(second.notified, isFalse);
      expect(repository.downloadCalls, 0);
      expect(connectivity.checks, 0);
      expect(notifications.updates, <String>['mobile-v6.0.0']);
    },
  );

  test('predownloads only after explicit opt-in and only on Wi-Fi', () async {
    final temporaryDirectory = await Directory.systemTemp.createTemp(
      'aquacyd-update-test-',
    );
    addTearDown(() async {
      if (await temporaryDirectory.exists()) {
        await temporaryDirectory.delete(recursive: true);
      }
    });
    final preferences = AppUpdatePreferences(
      await SharedPreferences.getInstance(),
    );
    await preferences.recordInstalledVersion(const SemanticVersion(5, 1, 1));
    await preferences.saveBackgroundOptions(
      const AppUpdateBackgroundOptions(
        systemNotificationsEnabled: false,
        downloadOnWifiEnabled: true,
      ),
    );
    final repository = _FakeUpdateRepository(_release);
    final connectivity = _RecordingConnectivity(isWifiValue: false);
    final runner = AppUpdateBackgroundRunner(
      preferencesLoader: () async => preferences,
      repository: repository,
      notifications: _RecordingNotificationSink(),
      connectivity: connectivity,
      supportDirectory: () async => temporaryDirectory,
    );

    final cellularResult = await runner.run();
    connectivity.isWifiValue = true;
    final wifiResult = await runner.run();

    expect(cellularResult.downloaded, isFalse);
    expect(wifiResult.downloaded, isTrue);
    expect(repository.downloadCalls, 1);
    expect(connectivity.checks, 2);
    expect(preferences.downloadedVersion, const SemanticVersion(6, 0, 0));
    expect(await File(preferences.downloadedPath!).exists(), isTrue);
  });
}

final _asset = ReleaseAsset(
  name: 'AquaCYD-Control-6.0.0-current.apk',
  downloadUri: Uri.parse(
    'https://github.com/example/aquacyd/releases/download/'
    'mobile-v6.0.0/app.apk',
  ),
  size: 4,
  sha256: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
  contentType: 'application/vnd.android.package-archive',
);

final _release = AppRelease(
  version: const SemanticVersion(6, 0, 0),
  tagName: 'mobile-v6.0.0',
  title: 'AquaCYD Mobile 6.0.0',
  notes: 'Test release',
  publishedAt: DateTime.utc(2026, 7, 29),
  releasePageUri: Uri.parse(
    'https://github.com/example/aquacyd/releases/tag/mobile-v6.0.0',
  ),
  asset: _asset,
);

final class _FakeUpdateRepository implements AppUpdateRepository {
  _FakeUpdateRepository(this.release);

  final AppRelease? release;
  int downloadCalls = 0;

  @override
  Future<AppRelease?> fetchLatestMobileRelease() async => release;

  @override
  Future<String> downloadApk({
    required AppRelease release,
    required String updateDirectory,
    required void Function(double progress) onProgress,
    required bool Function() isCanceled,
  }) async {
    downloadCalls += 1;
    final directory = Directory(updateDirectory);
    await directory.create(recursive: true);
    final file = File(
      '${directory.path}${Platform.pathSeparator}aquacyd-${release.version}.apk',
    );
    await file.writeAsBytes(<int>[1, 2, 3, 4], flush: true);
    onProgress(1);
    return file.path;
  }

  @override
  void dispose() {}
}

final class _RecordingConnectivity implements AppUpdateBackgroundConnectivity {
  _RecordingConnectivity({required this.isWifiValue});

  bool isWifiValue;
  int checks = 0;

  @override
  Future<bool> get isWifi async {
    checks += 1;
    return isWifiValue;
  }
}

final class _RecordingNotificationSink implements AlarmNotificationSink {
  final List<String> updates = <String>[];

  @override
  Future<void> cancelAlarm(String alarmKey) async {}

  @override
  Future<void> initialize() async {}

  @override
  Future<bool> requestPermission() async => true;

  @override
  Future<void> showAlarm(AlarmRecord alarm) async {}

  @override
  Future<void> showAppUpdate({
    required String tagName,
    required String version,
    required bool downloaded,
  }) async {
    updates.add(tagName);
  }

  @override
  Future<void> showResolved(AlarmRecord alarm) async {}

  @override
  Future<void> showServiceReminder({
    required String id,
    required String title,
    required String body,
  }) async {}

  @override
  Future<void> showTestNotification() async {}
}
