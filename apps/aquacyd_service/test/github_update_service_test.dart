import 'dart:convert';
import 'dart:io';

import 'package:cyd_aquarium_mobile/app_update/app_update_models.dart';
import 'package:cyd_aquarium_mobile/app_update/github_update_service.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('sends versioned GitHub headers and parses a valid response', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    late String? accept;
    late String? userAgent;
    late String? apiVersion;
    server.listen((request) {
      accept = request.headers.value(HttpHeaders.acceptHeader);
      userAgent = request.headers.value(HttpHeaders.userAgentHeader);
      apiVersion = request.headers.value('X-GitHub-Api-Version');
      request.response
        ..statusCode = HttpStatus.ok
        ..headers.contentType = ContentType.json
        ..write(jsonEncode([_releaseJson()]))
        ..close();
    });
    final service = GitHubUpdateService(
      releasesUri: Uri.parse(
        'http://${server.address.host}:${server.port}/releases',
      ),
    );
    addTearDown(service.dispose);

    final release = await service.fetchLatestMobileRelease();

    expect(release?.version, const SemanticVersion(3, 6, 0));
    expect(accept, 'application/vnd.github+json');
    expect(userAgent, 'AquaCYD-Control-Updater');
    expect(apiVersion, '2022-11-28');
  });

  test('maps GitHub rate limiting to a safe user error', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    server.listen((request) {
      request.response
        ..statusCode = HttpStatus.forbidden
        ..close();
    });
    final service = GitHubUpdateService(
      releasesUri: Uri.parse(
        'http://${server.address.host}:${server.port}/releases',
      ),
    );
    addTearDown(service.dispose);

    await expectLater(
      service.fetchLatestMobileRelease(),
      throwsA(
        isA<AppUpdateException>().having(
          (error) => error.code,
          'code',
          'GITHUB_HTTP_403',
        ),
      ),
    );
  });
}

Map<String, dynamic> _releaseJson() {
  const version = '3.6.0';
  const name = 'AquaCYD-Control-3.6.0-current.apk';
  return <String, dynamic>{
    'tag_name': 'mobile-v$version',
    'name': 'AquaCYD Mobile $version',
    'body': 'Aktualizacja.',
    'draft': false,
    'prerelease': false,
    'published_at': '2026-07-24T10:00:00Z',
    'html_url':
        'https://github.com/Baartek57548/AkwariumCYD/releases/tag/mobile-v$version',
    'assets': <Object?>[
      <String, dynamic>{
        'name': name,
        'size': 1000000,
        'digest': 'sha256:${'c' * 64}',
        'content_type': 'application/vnd.android.package-archive',
        'browser_download_url':
            'https://github.com/Baartek57548/AkwariumCYD/releases/download/mobile-v$version/$name',
      },
    ],
  };
}
