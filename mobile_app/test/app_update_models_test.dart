import 'package:cyd_aquarium_mobile/app_update/app_update_models.dart';
import 'package:cyd_aquarium_mobile/app_update/github_update_service.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('SemanticVersion', () {
    test('compares every segment numerically', () {
      expect(
        SemanticVersion.tryParse('3.10.0')!,
        greaterThan(SemanticVersion.tryParse('3.9.9')!),
      );
      expect(
        SemanticVersion.tryParse('4.0.0')!,
        greaterThan(SemanticVersion.tryParse('3.99.99')!),
      );
      expect(SemanticVersion.tryParse('3.5.1'), const SemanticVersion(3, 5, 1));
    });

    test('accepts only canonical stable mobile tags', () {
      expect(
        SemanticVersion.tryParseMobileTag('mobile-v3.6.0'),
        const SemanticVersion(3, 6, 0),
      );
      expect(SemanticVersion.tryParseMobileTag('mobile-v3.6'), isNull);
      expect(SemanticVersion.tryParseMobileTag('v3.6.0'), isNull);
      expect(SemanticVersion.tryParseMobileTag('mobile-v03.6.0'), isNull);
      expect(SemanticVersion.tryParseMobileTag('mobile-v3.6.0-beta'), isNull);
    });
  });

  group('GitHub release parser', () {
    test('selects highest valid mobile release regardless of API order', () {
      final parsed = GitHubUpdateService.parseReleaseList([
        _releaseJson('3.6.0'),
        _releaseJson('3.5.2'),
        _releaseJson('3.10.0'),
        _releaseJson('4.0.0', tag: 'v4.0.0'),
      ]);

      expect(parsed?.version, const SemanticVersion(3, 10, 0));
      expect(parsed?.asset.name, 'AquaCYD-Control-3.10.0-current.apk');
      expect(parsed?.asset.sha256, 'a' * 64);
    });

    test('ignores draft, prerelease and malformed assets', () {
      final wrongDigest = _releaseJson('3.9.0');
      final wrongDigestAsset =
          (wrongDigest['assets'] as List<Object?>).single
              as Map<String, dynamic>;
      wrongDigestAsset['digest'] = 'sha256:bad';

      final parsed = GitHubUpdateService.parseReleaseList([
        _releaseJson('4.0.0', draft: true),
        _releaseJson('3.8.0', prerelease: true),
        wrongDigest,
        _releaseJson('3.7.0'),
      ]);

      expect(parsed?.version, const SemanticVersion(3, 7, 0));
    });

    test('requires one exact current APK from a trusted HTTPS URL', () {
      final wrongName = _releaseJson('3.6.0');
      ((wrongName['assets'] as List<Object?>).single
              as Map<String, dynamic>)['name'] =
          'AquaCYD-Full-3.6.0.apk';
      final insecureUrl = _releaseJson('3.6.1');
      ((insecureUrl['assets'] as List<Object?>).single
              as Map<String, dynamic>)['browser_download_url'] =
          'http://github.com/Baartek57548/AkwariumCYD/file.apk';

      expect(
        GitHubUpdateService.parseReleaseList([wrongName, insecureUrl]),
        isNull,
      );
    });

    test('rejects a non-list GitHub response', () {
      expect(
        () => GitHubUpdateService.parseReleaseList({'message': 'error'}),
        throwsA(isA<AppUpdateException>()),
      );
    });
  });
}

Map<String, dynamic> _releaseJson(
  String version, {
  String? tag,
  bool draft = false,
  bool prerelease = false,
}) {
  final assetName = 'AquaCYD-Control-$version-current.apk';
  return <String, dynamic>{
    'tag_name': tag ?? 'mobile-v$version',
    'name': 'AquaCYD Mobile $version',
    'body': 'Bezpieczne aktualizacje.',
    'draft': draft,
    'prerelease': prerelease,
    'published_at': '2026-07-24T10:00:00Z',
    'html_url':
        'https://github.com/Baartek57548/AkwariumCYD/releases/tag/mobile-v$version',
    'assets': <Object?>[
      <String, dynamic>{
        'name': assetName,
        'size': 57 * 1024 * 1024,
        'digest': 'sha256:${'a' * 64}',
        'content_type': 'application/vnd.android.package-archive',
        'browser_download_url':
            'https://github.com/Baartek57548/AkwariumCYD/releases/download/mobile-v$version/$assetName',
      },
    ],
  };
}
