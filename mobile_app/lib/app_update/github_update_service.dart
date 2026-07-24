import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'app_update_models.dart';

abstract interface class AppUpdateRepository {
  Future<AppRelease?> fetchLatestMobileRelease();

  Future<String> downloadApk({
    required AppRelease release,
    required String updateDirectory,
    required void Function(double progress) onProgress,
    required bool Function() isCanceled,
  });

  void dispose();
}

class GitHubUpdateService implements AppUpdateRepository {
  GitHubUpdateService({
    HttpClient? httpClient,
    this.owner = 'Baartek57548',
    this.repository = 'AkwariumCYD',
    Uri? releasesUri,
  }) : _httpClient = httpClient ?? _createHttpClient(),
       _ownsHttpClient = httpClient == null,
       _customReleasesUri = releasesUri;

  static const int maximumApiResponseBytes = 1024 * 1024;
  static const int maximumApkBytes = 200 * 1024 * 1024;
  static const Duration requestTimeout = Duration(seconds: 20);

  final String owner;
  final String repository;
  final HttpClient _httpClient;
  final bool _ownsHttpClient;
  final Uri? _customReleasesUri;

  static HttpClient _createHttpClient() {
    return HttpClient()
      ..connectionTimeout = const Duration(seconds: 10)
      ..idleTimeout = const Duration(seconds: 15)
      ..maxConnectionsPerHost = 2;
  }

  Uri get releasesUri =>
      _customReleasesUri ??
      Uri.https('api.github.com', '/repos/$owner/$repository/releases', const {
        'per_page': '30',
      });

  @override
  Future<AppRelease?> fetchLatestMobileRelease() async {
    try {
      final request = await _httpClient.getUrl(releasesUri);
      request
        ..followRedirects = false
        ..headers.set(HttpHeaders.acceptHeader, 'application/vnd.github+json')
        ..headers.set(HttpHeaders.userAgentHeader, 'AquaCYD-Control-Updater')
        ..headers.set('X-GitHub-Api-Version', '2022-11-28');
      final response = await request.close().timeout(requestTimeout);
      if (response.statusCode != HttpStatus.ok) {
        await response.drain<void>();
        throw AppUpdateException(
          'GITHUB_HTTP_${response.statusCode}',
          response.statusCode == HttpStatus.forbidden
              ? 'GitHub chwilowo ograniczył sprawdzanie aktualizacji. Spróbuj ponownie później.'
              : 'Nie udało się sprawdzić aktualizacji na GitHubie.',
        );
      }
      final bytes = await _readLimited(
        response,
        maximumBytes: maximumApiResponseBytes,
      );
      final decoded = jsonDecode(utf8.decode(bytes));
      return parseReleaseList(decoded);
    } on AppUpdateException {
      rethrow;
    } on TimeoutException catch (error) {
      throw AppUpdateException(
        'CHECK_TIMEOUT',
        'Przekroczono czas sprawdzania aktualizacji.',
        error,
      );
    } on SocketException catch (error) {
      throw AppUpdateException(
        'CHECK_OFFLINE',
        'Brak połączenia z internetem. Aktualizację sprawdzimy później.',
        error,
      );
    } on FormatException catch (error) {
      throw AppUpdateException(
        'INVALID_GITHUB_RESPONSE',
        'GitHub zwrócił nieprawidłowe dane wydania.',
        error,
      );
    } on HttpException catch (error) {
      throw AppUpdateException(
        'CHECK_HTTP_ERROR',
        'Nie udało się połączyć z GitHubem.',
        error,
      );
    }
  }

  static AppRelease? parseReleaseList(Object? decoded) {
    if (decoded is! List<Object?>) {
      throw const AppUpdateException(
        'INVALID_RELEASE_LIST',
        'GitHub zwrócił nieprawidłową listę wydań.',
      );
    }

    AppRelease? latest;
    for (final entry in decoded) {
      final release = _tryParseRelease(entry);
      if (release == null) continue;
      if (latest == null || release.version > latest.version) {
        latest = release;
      }
    }
    return latest;
  }

  static AppRelease? _tryParseRelease(Object? value) {
    if (value is! Map<String, dynamic>) return null;
    if (value['draft'] != false || value['prerelease'] != false) return null;

    final tagName = value['tag_name'];
    if (tagName is! String) return null;
    final version = SemanticVersion.tryParseMobileTag(tagName);
    if (version == null) return null;

    final expectedAssetName =
        'AquaCYD-Control-${version.toString()}-current.apk';
    final rawAssets = value['assets'];
    if (rawAssets is! List<Object?>) return null;
    final matchingAssets = rawAssets
        .whereType<Map<String, dynamic>>()
        .where((asset) => asset['name'] == expectedAssetName)
        .toList(growable: false);
    if (matchingAssets.length != 1) return null;

    final asset = _tryParseAsset(matchingAssets.single, expectedAssetName);
    if (asset == null) return null;

    final releasePageUri = _trustedGitHubPageUri(value['html_url']);
    if (releasePageUri == null) return null;

    final rawName = value['name'];
    final rawNotes = value['body'];
    final rawPublishedAt = value['published_at'];
    return AppRelease(
      version: version,
      tagName: tagName,
      title: rawName is String && rawName.trim().isNotEmpty
          ? rawName.trim()
          : 'AquaCYD Mobile $version',
      notes: rawNotes is String ? rawNotes.trim() : '',
      publishedAt: rawPublishedAt is String
          ? DateTime.tryParse(rawPublishedAt)?.toUtc()
          : null,
      releasePageUri: releasePageUri,
      asset: asset,
    );
  }

  static ReleaseAsset? _tryParseAsset(
    Map<String, dynamic> value,
    String expectedName,
  ) {
    final rawSize = value['size'];
    final rawDigest = value['digest'];
    final rawUrl = value['browser_download_url'];
    if (rawSize is! int ||
        rawSize <= 0 ||
        rawSize > maximumApkBytes ||
        rawDigest is! String ||
        rawUrl is! String) {
      return null;
    }

    final digestMatch = RegExp(
      r'^sha256:([0-9a-fA-F]{64})$',
    ).firstMatch(rawDigest);
    if (digestMatch == null) return null;

    final uri = Uri.tryParse(rawUrl);
    if (uri == null || !_isTrustedDownloadUri(uri)) return null;

    final rawContentType = value['content_type'];
    return ReleaseAsset(
      name: expectedName,
      downloadUri: uri,
      size: rawSize,
      sha256: digestMatch.group(1)!.toLowerCase(),
      contentType: rawContentType is String ? rawContentType : '',
    );
  }

  static Uri? _trustedGitHubPageUri(Object? value) {
    if (value is! String) return null;
    final uri = Uri.tryParse(value);
    if (uri == null ||
        uri.scheme != 'https' ||
        uri.host.toLowerCase() != 'github.com') {
      return null;
    }
    return uri;
  }

  @override
  Future<String> downloadApk({
    required AppRelease release,
    required String updateDirectory,
    required void Function(double progress) onProgress,
    required bool Function() isCanceled,
  }) async {
    final directory = Directory(updateDirectory);
    await directory.create(recursive: true);
    final baseName = 'aquacyd-${release.version}';
    final partialFile = File(
      '${directory.path}${Platform.pathSeparator}$baseName.part',
    );
    final completedFile = File(
      '${directory.path}${Platform.pathSeparator}$baseName.apk',
    );

    await _deleteIfExists(partialFile);
    await _deleteIfExists(completedFile);

    IOSink? output;
    try {
      final response = await _openTrustedDownload(release.asset.downloadUri);
      if (response.statusCode != HttpStatus.ok) {
        await response.drain<void>();
        throw AppUpdateException(
          'DOWNLOAD_HTTP_${response.statusCode}',
          'GitHub nie udostępnił pliku aktualizacji.',
        );
      }
      if (response.contentLength > release.asset.size) {
        await response.drain<void>();
        throw const AppUpdateException(
          'DOWNLOAD_TOO_LARGE',
          'Pobrany plik jest większy niż plik opublikowany na GitHubie.',
        );
      }

      output = partialFile.openWrite(mode: FileMode.writeOnly);
      var received = 0;
      await for (final chunk in response.timeout(requestTimeout)) {
        if (isCanceled()) throw const AppUpdateCanceledException();
        received += chunk.length;
        if (received > release.asset.size || received > maximumApkBytes) {
          throw const AppUpdateException(
            'DOWNLOAD_TOO_LARGE',
            'Pobierany plik przekroczył bezpieczny limit rozmiaru.',
          );
        }
        output.add(chunk);
        onProgress(received / release.asset.size);
      }
      await output.flush();
      await output.close();
      output = null;

      if (isCanceled()) throw const AppUpdateCanceledException();
      if (received != release.asset.size) {
        throw AppUpdateException(
          'DOWNLOAD_SIZE_MISMATCH',
          'Pobrano niepełny plik aktualizacji ($received z ${release.asset.size} bajtów).',
        );
      }
      await partialFile.rename(completedFile.path);
      onProgress(1);
      return completedFile.path;
    } on AppUpdateException {
      rethrow;
    } on TimeoutException catch (error) {
      throw AppUpdateException(
        'DOWNLOAD_TIMEOUT',
        'Pobieranie aktualizacji trwało zbyt długo.',
        error,
      );
    } on SocketException catch (error) {
      throw AppUpdateException(
        'DOWNLOAD_OFFLINE',
        'Utracono połączenie podczas pobierania aktualizacji.',
        error,
      );
    } on FileSystemException catch (error) {
      throw AppUpdateException(
        'DOWNLOAD_STORAGE_ERROR',
        'Nie można zapisać aktualizacji. Sprawdź wolne miejsce w pamięci.',
        error,
      );
    } finally {
      await output?.close();
      if (await partialFile.exists()) {
        await partialFile.delete();
      }
    }
  }

  Future<HttpClientResponse> _openTrustedDownload(Uri initialUri) async {
    var uri = initialUri;
    for (var redirectCount = 0; redirectCount <= 5; redirectCount++) {
      if (!_isTrustedDownloadUri(uri)) {
        throw const AppUpdateException(
          'UNTRUSTED_DOWNLOAD_HOST',
          'GitHub przekierował pobieranie do niezaufanego serwera.',
        );
      }

      final request = await _httpClient.getUrl(uri);
      request
        ..followRedirects = false
        ..headers.set(HttpHeaders.acceptHeader, 'application/octet-stream')
        ..headers.set(HttpHeaders.userAgentHeader, 'AquaCYD-Control-Updater');
      final response = await request.close().timeout(requestTimeout);
      if (!_isRedirect(response.statusCode)) return response;

      final location = response.headers.value(HttpHeaders.locationHeader);
      await response.drain<void>();
      if (location == null || redirectCount == 5) {
        throw const AppUpdateException(
          'INVALID_DOWNLOAD_REDIRECT',
          'Nie udało się bezpiecznie pobrać pliku z GitHuba.',
        );
      }
      uri = uri.resolve(location);
    }
    throw const AppUpdateException(
      'TOO_MANY_DOWNLOAD_REDIRECTS',
      'GitHub wykonał zbyt wiele przekierowań pobierania.',
    );
  }

  static bool _isRedirect(int statusCode) {
    return statusCode == HttpStatus.movedPermanently ||
        statusCode == HttpStatus.found ||
        statusCode == HttpStatus.seeOther ||
        statusCode == HttpStatus.temporaryRedirect ||
        statusCode == HttpStatus.permanentRedirect;
  }

  static bool _isTrustedDownloadUri(Uri uri) {
    if (uri.scheme != 'https' || uri.userInfo.isNotEmpty || uri.hasPort) {
      return false;
    }
    final host = uri.host.toLowerCase();
    return host == 'github.com' ||
        host == 'objects.githubusercontent.com' ||
        host == 'release-assets.githubusercontent.com' ||
        host.endsWith('.githubusercontent.com');
  }

  static Future<Uint8List> _readLimited(
    Stream<List<int>> stream, {
    required int maximumBytes,
  }) async {
    final builder = BytesBuilder(copy: false);
    var length = 0;
    await for (final chunk in stream.timeout(requestTimeout)) {
      length += chunk.length;
      if (length > maximumBytes) {
        throw const AppUpdateException(
          'API_RESPONSE_TOO_LARGE',
          'Odpowiedź GitHuba przekroczyła bezpieczny limit.',
        );
      }
      builder.add(chunk);
    }
    return builder.takeBytes();
  }

  static Future<void> _deleteIfExists(File file) async {
    if (await file.exists()) await file.delete();
  }

  @override
  void dispose() {
    if (_ownsHttpClient) _httpClient.close(force: true);
  }
}
