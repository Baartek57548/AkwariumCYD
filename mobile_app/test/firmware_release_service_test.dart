import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:cyd_aquarium_mobile/full_controller/firmware_package.dart';
import 'package:cyd_aquarium_mobile/full_controller/firmware_release_service.dart';
import 'package:crypto/crypto.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('selects the highest canonical firmware release for the target', () {
    final decoded = <Object?>[
      _release('5.1.0'),
      _release('5.3.0', draft: true),
      _release('5.2.0'),
      _release('9.0.0', tag: 'mobile-v9.0.0'),
    ];

    final release = GitHubFirmwareReleaseService.parseReleaseList(
      decoded,
      target: FirmwareTarget.ili9341,
    );

    expect(release, isNotNull);
    expect(release!.version, '5.2.0');
    expect(release.target, FirmwareTarget.ili9341);
    expect(release.asset.name, 'AquaCYD-Firmware-5.2.0-ili9341.aqfw');
  });

  test('selects only the asset matching the controller display', () {
    final decoded = <Object?>[_release('5.2.0')];

    final ili = GitHubFirmwareReleaseService.parseReleaseList(
      decoded,
      target: FirmwareTarget.ili9341,
    );
    final st = GitHubFirmwareReleaseService.parseReleaseList(
      decoded,
      target: FirmwareTarget.st7789,
    );

    expect(ili?.asset.name, endsWith('-ili9341.aqfw'));
    expect(st?.asset.name, endsWith('-st7789.aqfw'));
  });

  test('rejects duplicate, untrusted and digest-less assets', () {
    final duplicate = _release('5.2.0');
    final assets = duplicate['assets']! as List<Object?>;
    assets.add(Map<String, dynamic>.from(assets.first! as Map));
    expect(
      GitHubFirmwareReleaseService.parseReleaseList(<Object?>[
        duplicate,
      ], target: FirmwareTarget.ili9341),
      isNull,
    );

    final untrusted = _release('5.2.0');
    final untrustedAsset =
        (untrusted['assets']! as List<Object?>).first as Map<String, dynamic>;
    untrustedAsset['browser_download_url'] =
        'https://example.com/firmware.aqfw';
    expect(
      GitHubFirmwareReleaseService.parseReleaseList(<Object?>[
        untrusted,
      ], target: FirmwareTarget.ili9341),
      isNull,
    );

    final missingDigest = _release('5.2.0');
    final digestAsset =
        (missingDigest['assets']! as List<Object?>).first
            as Map<String, dynamic>;
    digestAsset.remove('digest');
    expect(
      GitHubFirmwareReleaseService.parseReleaseList(<Object?>[
        missingDigest,
      ], target: FirmwareTarget.ili9341),
      isNull,
    );
  });

  test('fetch uses a bounded GitHub-compatible release response', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    String? accept;
    String? userAgent;
    final subscription = server.listen((request) async {
      accept = request.headers.value(HttpHeaders.acceptHeader);
      userAgent = request.headers.value(HttpHeaders.userAgentHeader);
      request.response.headers.contentType = ContentType.json;
      request.response.write(jsonEncode(<Object?>[_release('5.2.0')]));
      await request.response.close();
    });
    addTearDown(subscription.cancel);
    final service = GitHubFirmwareReleaseService(
      releasesUri: Uri.parse(
        'http://${server.address.address}:${server.port}/releases',
      ),
    );
    addTearDown(service.dispose);

    final release = await service.fetchLatestFirmwareRelease(
      FirmwareTarget.ili9341,
    );

    expect(release?.version, '5.2.0');
    expect(accept, 'application/vnd.github+json');
    expect(userAgent, 'AquaCYD-Firmware-Updater');
  });

  test('non-list GitHub payload is rejected', () {
    expect(
      () => GitHubFirmwareReleaseService.parseReleaseList(<String, Object?>{
        'message': 'invalid',
      }, target: FirmwareTarget.ili9341),
      throwsA(
        isA<FirmwareReleaseException>().having(
          (error) => error.code,
          'code',
          'invalid_release_list',
        ),
      ),
    );
  });

  test('cancellation force-closes the transfer and stops progress', () async {
    final fixture = await _SlowFirmwareServer.start();
    addTearDown(fixture.dispose);
    final service = GitHubFirmwareReleaseService(
      releasesUri: fixture.releasesUri,
      requestTimeoutOverride: const Duration(seconds: 2),
      downloadDeadlineOverride: const Duration(seconds: 5),
      trustedDownloadUriOverride: fixture.isTrusted,
    );
    addTearDown(service.dispose);
    final token = FirmwareDownloadCancellationToken();
    final progress = <double>[];

    final operation = service.downloadFirmwarePackage(
      release: fixture.release,
      onProgress: progress.add,
      cancellationToken: token,
    );
    await fixture.requestStarted.future.timeout(const Duration(seconds: 2));
    token.cancel();

    await expectLater(
      operation,
      throwsA(
        isA<FirmwareReleaseException>().having(
          (error) => error.code,
          'code',
          'download_canceled',
        ),
      ),
    ).timeout(const Duration(seconds: 2));
    final callbacksAfterCancellation = progress.length;
    fixture.finishResponse();
    await Future<void>.delayed(const Duration(milliseconds: 100));

    expect(progress, isNotEmpty);
    expect(progress.length, callbacksAfterCancellation);
  });

  test(
    'download deadline closes the transfer and suppresses late data',
    () async {
      final fixture = await _SlowFirmwareServer.start();
      addTearDown(fixture.dispose);
      final service = GitHubFirmwareReleaseService(
        releasesUri: fixture.releasesUri,
        requestTimeoutOverride: const Duration(seconds: 2),
        downloadDeadlineOverride: const Duration(milliseconds: 120),
        trustedDownloadUriOverride: fixture.isTrusted,
      );
      addTearDown(service.dispose);
      final progress = <double>[];

      final operation = service.downloadFirmwarePackage(
        release: fixture.release,
        onProgress: progress.add,
        cancellationToken: FirmwareDownloadCancellationToken(),
      );
      await fixture.requestStarted.future.timeout(const Duration(seconds: 2));

      await expectLater(
        operation,
        throwsA(
          isA<FirmwareReleaseException>().having(
            (error) => error.code,
            'code',
            'download_timeout',
          ),
        ),
      ).timeout(const Duration(seconds: 2));
      final callbacksAfterTimeout = progress.length;
      fixture.finishResponse();
      await Future<void>.delayed(const Duration(milliseconds: 100));

      expect(progress, isNotEmpty);
      expect(progress.length, callbacksAfterTimeout);
    },
  );
}

final class _SlowFirmwareServer {
  _SlowFirmwareServer._(
    this.server,
    this.bytes,
    this.requestStarted,
    this._finishGate,
  ) : release = FirmwareRelease(
        version: '5.1.0',
        tagName: 'firmware-v5.1.0',
        title: 'AquaCYD Firmware 5.1.0',
        notes: 'Test wolnego transferu.',
        publishedAt: DateTime.utc(2026, 7, 29),
        releasePageUri: Uri.parse(
          'https://github.com/Baartek57548/AkwariumCYD/'
          'releases/tag/firmware-v5.1.0',
        ),
        target: FirmwareTarget.ili9341,
        asset: FirmwareReleaseAsset(
          name: 'AquaCYD-Firmware-5.1.0-ili9341.aqfw',
          downloadUri: Uri.parse(
            'http://${server.address.address}:${server.port}/firmware.aqfw',
          ),
          size: bytes.length,
          sha256: sha256.convert(bytes).toString(),
        ),
      );

  final HttpServer server;
  final Uint8List bytes;
  final Completer<void> requestStarted;
  final Completer<void> _finishGate;
  final FirmwareRelease release;
  StreamSubscription<HttpRequest>? _subscription;

  Uri get releasesUri =>
      Uri.parse('http://${server.address.address}:${server.port}/releases');

  static Future<_SlowFirmwareServer> start() async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final fixture = _SlowFirmwareServer._(
      server,
      Uint8List.fromList(List<int>.generate(4096, (index) => index & 0xff)),
      Completer<void>(),
      Completer<void>(),
    );
    fixture._subscription = server.listen(fixture._handle);
    return fixture;
  }

  bool isTrusted(Uri uri) {
    return uri.host == server.address.address && uri.port == server.port;
  }

  Future<void> _handle(HttpRequest request) async {
    try {
      request.response.contentLength = bytes.length;
      request.response.add(Uint8List.sublistView(bytes, 0, 512));
      await request.response.flush();
      if (!requestStarted.isCompleted) requestStarted.complete();
      await _finishGate.future;
      request.response.add(Uint8List.sublistView(bytes, 512));
      await request.response.close();
    } on Object {
      if (!requestStarted.isCompleted) requestStarted.complete();
    }
  }

  void finishResponse() {
    if (!_finishGate.isCompleted) _finishGate.complete();
  }

  Future<void> dispose() async {
    finishResponse();
    await _subscription?.cancel();
    await server.close(force: true);
  }
}

Map<String, dynamic> _release(
  String version, {
  String? tag,
  bool draft = false,
  bool prerelease = false,
}) {
  List<Map<String, dynamic>> assets() {
    return FirmwareTarget.values
        .map((target) {
          final name = 'AquaCYD-Firmware-$version-${target.code}.aqfw';
          return <String, dynamic>{
            'name': name,
            'size': 1800000,
            'digest': 'sha256:${List.filled(64, 'a').join()}',
            'browser_download_url':
                'https://github.com/Baartek57548/AkwariumCYD/'
                'releases/download/firmware-v$version/$name',
          };
        })
        .toList(growable: true);
  }

  return <String, dynamic>{
    'tag_name': tag ?? 'firmware-v$version',
    'name': 'AquaCYD Firmware $version',
    'body': 'Bezpieczna aktualizacja sterownika.',
    'draft': draft,
    'prerelease': prerelease,
    'published_at': '2026-07-28T12:00:00Z',
    'html_url':
        'https://github.com/Baartek57548/AkwariumCYD/'
        'releases/tag/firmware-v$version',
    'assets': assets(),
  };
}
