import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:crypto/crypto.dart';

import 'firmware_package.dart';

class FirmwareReleaseAsset {
  const FirmwareReleaseAsset({
    required this.name,
    required this.downloadUri,
    required this.size,
    required this.sha256,
  });

  final String name;
  final Uri downloadUri;
  final int size;
  final String sha256;
}

class FirmwareRelease {
  const FirmwareRelease({
    required this.version,
    required this.tagName,
    required this.title,
    required this.notes,
    required this.publishedAt,
    required this.releasePageUri,
    required this.target,
    required this.asset,
  });

  final String version;
  final String tagName;
  final String title;
  final String notes;
  final DateTime? publishedAt;
  final Uri releasePageUri;
  final FirmwareTarget target;
  final FirmwareReleaseAsset asset;

  String get formattedSize {
    final mebibytes = asset.size / (1024 * 1024);
    return '${mebibytes.toStringAsFixed(mebibytes >= 10 ? 0 : 1)} MiB';
  }
}

class FirmwareReleaseException implements Exception {
  const FirmwareReleaseException({
    required this.code,
    required this.message,
    this.cause,
  });

  final String code;
  final String message;
  final Object? cause;

  @override
  String toString() => message;
}

abstract interface class FirmwareReleaseRepository {
  Future<FirmwareRelease?> fetchLatestFirmwareRelease(FirmwareTarget target);

  Future<Uint8List> downloadFirmwarePackage({
    required FirmwareRelease release,
    required void Function(double progress) onProgress,
    required FirmwareDownloadCancellationToken cancellationToken,
  });

  void dispose();
}

class FirmwareDownloadCancellationToken {
  final Set<void Function()> _listeners = <void Function()>{};
  bool _isCanceled = false;

  bool get isCanceled => _isCanceled;

  void addListener(void Function() listener) {
    if (_isCanceled) {
      listener();
      return;
    }
    _listeners.add(listener);
  }

  void removeListener(void Function() listener) {
    _listeners.remove(listener);
  }

  void cancel() {
    if (_isCanceled) return;
    _isCanceled = true;
    final listeners = List<void Function()>.of(_listeners);
    _listeners.clear();
    for (final listener in listeners) {
      listener();
    }
  }
}

class GitHubFirmwareReleaseService implements FirmwareReleaseRepository {
  GitHubFirmwareReleaseService({
    HttpClient? httpClient,
    this.owner = 'Baartek57548',
    this.repository = 'AkwariumCYD',
    Uri? releasesUri,
    Duration? requestTimeoutOverride,
    Duration? downloadDeadlineOverride,
    bool Function(Uri uri)? trustedDownloadUriOverride,
  }) : _httpClient = httpClient ?? _createHttpClient(),
       _ownsHttpClient = httpClient == null,
       _customReleasesUri = releasesUri,
       _requestTimeout = requestTimeoutOverride ?? requestTimeout,
       _downloadDeadline = downloadDeadlineOverride ?? downloadDeadline,
       _trustedDownloadUriOverride = trustedDownloadUriOverride {
    if (_requestTimeout <= Duration.zero) {
      throw ArgumentError.value(
        _requestTimeout,
        'requestTimeoutOverride',
        'Czas operacji HTTP musi być dodatni.',
      );
    }
    if (_downloadDeadline <= Duration.zero) {
      throw ArgumentError.value(
        _downloadDeadline,
        'downloadDeadlineOverride',
        'Limit czasu pobierania musi być dodatni.',
      );
    }
    if (trustedDownloadUriOverride != null && releasesUri == null) {
      throw ArgumentError(
        'Niestandardowa polityka hostów jest dozwolona wyłącznie razem '
        'z niestandardowym endpointem wydań.',
      );
    }
  }

  static const int maximumApiResponseBytes = 1024 * 1024;
  static const int maximumPackageBytes =
      FirmwarePackageParser.defaultMaximumImageBytes +
      FirmwarePackageParser.headerBytes;
  static const Duration requestTimeout = Duration(seconds: 20);
  static const Duration downloadDeadline = Duration(minutes: 2);
  static final RegExp _firmwareTagPattern = RegExp(
    r'^firmware-v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$',
  );
  static final RegExp _sha256Pattern = RegExp(r'^sha256:([0-9a-fA-F]{64})$');

  final String owner;
  final String repository;
  final HttpClient _httpClient;
  final bool _ownsHttpClient;
  final Uri? _customReleasesUri;
  final Duration _requestTimeout;
  final Duration _downloadDeadline;
  final bool Function(Uri uri)? _trustedDownloadUriOverride;
  final Set<HttpClient> _activeDownloadClients = <HttpClient>{};
  bool _disposed = false;

  static HttpClient _createHttpClient() {
    return HttpClient()
      ..connectionTimeout = const Duration(seconds: 10)
      ..idleTimeout = const Duration(seconds: 15)
      ..maxConnectionsPerHost = 2;
  }

  Uri get releasesUri =>
      _customReleasesUri ??
      Uri.https('api.github.com', '/repos/$owner/$repository/releases', const {
        'per_page': '100',
      });

  @override
  Future<FirmwareRelease?> fetchLatestFirmwareRelease(
    FirmwareTarget target,
  ) async {
    if (_disposed) {
      throw const FirmwareReleaseException(
        code: 'service_disposed',
        message: 'Usługa aktualizacji firmware została zamknięta.',
      );
    }
    try {
      final request = await _httpClient.getUrl(releasesUri);
      request
        ..followRedirects = false
        ..headers.set(HttpHeaders.acceptHeader, 'application/vnd.github+json')
        ..headers.set(HttpHeaders.userAgentHeader, 'AquaCYD-Firmware-Updater')
        ..headers.set('X-GitHub-Api-Version', '2022-11-28');
      final response = await request.close().timeout(_requestTimeout);
      if (response.statusCode != HttpStatus.ok) {
        await response.drain<void>();
        throw FirmwareReleaseException(
          code: 'github_http_${response.statusCode}',
          message: response.statusCode == HttpStatus.forbidden
              ? 'GitHub chwilowo ograniczył sprawdzanie firmware.'
              : 'Nie udało się sprawdzić wydań firmware na GitHubie.',
        );
      }
      final bytes = await _readLimited(
        response,
        maximumBytes: maximumApiResponseBytes,
        inactivityTimeout: _requestTimeout,
      );
      return parseReleaseList(
        jsonDecode(utf8.decode(bytes)),
        target: target,
        owner: owner,
        repository: repository,
      );
    } on FirmwareReleaseException {
      rethrow;
    } on TimeoutException catch (error) {
      throw FirmwareReleaseException(
        code: 'check_timeout',
        message: 'Przekroczono czas sprawdzania firmware.',
        cause: error,
      );
    } on SocketException catch (error) {
      throw FirmwareReleaseException(
        code: 'check_offline',
        message: 'Brak dostępu do internetu podczas sprawdzania firmware.',
        cause: error,
      );
    } on FormatException catch (error) {
      throw FirmwareReleaseException(
        code: 'invalid_github_response',
        message: 'GitHub zwrócił nieprawidłowe dane wydania firmware.',
        cause: error,
      );
    } on HttpException catch (error) {
      throw FirmwareReleaseException(
        code: 'check_http_error',
        message: 'Nie udało się połączyć z GitHubem.',
        cause: error,
      );
    }
  }

  static FirmwareRelease? parseReleaseList(
    Object? decoded, {
    required FirmwareTarget target,
    String owner = 'Baartek57548',
    String repository = 'AkwariumCYD',
  }) {
    if (decoded is! List<Object?>) {
      throw const FirmwareReleaseException(
        code: 'invalid_release_list',
        message: 'GitHub zwrócił nieprawidłową listę wydań firmware.',
      );
    }

    FirmwareRelease? latest;
    for (final entry in decoded) {
      final release = _tryParseRelease(
        entry,
        target: target,
        owner: owner,
        repository: repository,
      );
      if (release == null) continue;
      if (latest == null ||
          FirmwarePackageParser.compareSemanticVersions(
                release.version,
                latest.version,
              ) >
              0) {
        latest = release;
      }
    }
    return latest;
  }

  static FirmwareRelease? _tryParseRelease(
    Object? value, {
    required FirmwareTarget target,
    required String owner,
    required String repository,
  }) {
    if (value is! Map<String, dynamic>) return null;
    if (value['draft'] != false || value['prerelease'] != false) return null;

    final tagName = value['tag_name'];
    if (tagName is! String) return null;
    final tagMatch = _firmwareTagPattern.firstMatch(tagName.trim());
    if (tagMatch == null) return null;
    final version = tagName.substring('firmware-v'.length);
    try {
      FirmwarePackageParser.compareSemanticVersions(version, version);
    } on FirmwarePackageException {
      return null;
    }

    final expectedAssetName = 'AquaCYD-Firmware-$version-${target.code}.aqfw';
    final rawAssets = value['assets'];
    if (rawAssets is! List<Object?>) return null;
    final matchingAssets = rawAssets
        .whereType<Map<String, dynamic>>()
        .where((asset) => asset['name'] == expectedAssetName)
        .toList(growable: false);
    if (matchingAssets.length != 1) return null;
    final asset = _tryParseAsset(
      matchingAssets.single,
      expectedName: expectedAssetName,
      owner: owner,
      repository: repository,
    );
    if (asset == null) return null;

    final releasePageUri = _trustedReleasePageUri(
      value['html_url'],
      owner: owner,
      repository: repository,
    );
    if (releasePageUri == null) return null;

    final rawName = value['name'];
    final rawNotes = value['body'];
    final notes = rawNotes is String ? rawNotes.trim() : '';
    return FirmwareRelease(
      version: version,
      tagName: tagName,
      title: rawName is String && rawName.trim().isNotEmpty
          ? rawName.trim()
          : 'AquaCYD Firmware $version',
      notes: notes.length <= 4000 ? notes : '${notes.substring(0, 4000)}…',
      publishedAt: value['published_at'] is String
          ? DateTime.tryParse(value['published_at'] as String)?.toUtc()
          : null,
      releasePageUri: releasePageUri,
      target: target,
      asset: asset,
    );
  }

  static FirmwareReleaseAsset? _tryParseAsset(
    Map<String, dynamic> value, {
    required String expectedName,
    required String owner,
    required String repository,
  }) {
    final rawSize = value['size'];
    final rawDigest = value['digest'];
    final rawUrl = value['browser_download_url'];
    if (rawSize is! int ||
        rawSize <= FirmwarePackageParser.headerBytes ||
        rawSize > maximumPackageBytes ||
        rawDigest is! String ||
        rawUrl is! String) {
      return null;
    }
    final digestMatch = _sha256Pattern.firstMatch(rawDigest);
    if (digestMatch == null) return null;
    final uri = Uri.tryParse(rawUrl);
    if (uri == null ||
        !_isTrustedDownloadUri(uri, owner: owner, repository: repository)) {
      return null;
    }
    return FirmwareReleaseAsset(
      name: expectedName,
      downloadUri: uri,
      size: rawSize,
      sha256: digestMatch.group(1)!.toLowerCase(),
    );
  }

  static Uri? _trustedReleasePageUri(
    Object? value, {
    required String owner,
    required String repository,
  }) {
    if (value is! String) return null;
    final uri = Uri.tryParse(value);
    if (uri == null ||
        uri.scheme != 'https' ||
        uri.userInfo.isNotEmpty ||
        uri.hasPort ||
        uri.host.toLowerCase() != 'github.com') {
      return null;
    }
    final prefix = '/$owner/$repository/releases/';
    return uri.path.startsWith(prefix) ? uri : null;
  }

  @override
  Future<Uint8List> downloadFirmwarePackage({
    required FirmwareRelease release,
    required void Function(double progress) onProgress,
    required FirmwareDownloadCancellationToken cancellationToken,
  }) async {
    if (_disposed) {
      throw const FirmwareReleaseException(
        code: 'service_disposed',
        message: 'Usługa aktualizacji firmware została zamknięta.',
      );
    }
    if (cancellationToken.isCanceled) {
      throw const FirmwareReleaseException(
        code: 'download_canceled',
        message: 'Pobieranie firmware zostało anulowane.',
      );
    }

    final client = _createHttpClient();
    _activeDownloadClients.add(client);
    var callbacksEnabled = true;
    void stopTransfer() {
      callbacksEnabled = false;
      client.close(force: true);
    }

    void reportProgress(double progress) {
      if (!callbacksEnabled || cancellationToken.isCanceled || _disposed) {
        return;
      }
      onProgress(progress);
    }

    cancellationToken.addListener(stopTransfer);
    try {
      return await _downloadFirmwarePackage(
        client: client,
        release: release,
        onProgress: reportProgress,
        cancellationToken: cancellationToken,
      ).timeout(
        _downloadDeadline,
        onTimeout: () {
          stopTransfer();
          throw const FirmwareReleaseException(
            code: 'download_timeout',
            message: 'Pobieranie firmware trwało zbyt długo.',
          );
        },
      );
    } on FirmwareReleaseException {
      rethrow;
    } on TimeoutException catch (error) {
      if (cancellationToken.isCanceled) {
        throw const FirmwareReleaseException(
          code: 'download_canceled',
          message: 'Pobieranie firmware zostało anulowane.',
        );
      }
      throw FirmwareReleaseException(
        code: 'download_timeout',
        message: 'Pobieranie firmware trwało zbyt długo.',
        cause: error,
      );
    } on SocketException catch (error) {
      if (cancellationToken.isCanceled || _disposed) {
        throw const FirmwareReleaseException(
          code: 'download_canceled',
          message: 'Pobieranie firmware zostało anulowane.',
        );
      }
      throw FirmwareReleaseException(
        code: 'download_offline',
        message: 'Utracono internet podczas pobierania firmware.',
        cause: error,
      );
    } on HttpException catch (error) {
      if (cancellationToken.isCanceled || _disposed) {
        throw const FirmwareReleaseException(
          code: 'download_canceled',
          message: 'Pobieranie firmware zostało anulowane.',
        );
      }
      throw FirmwareReleaseException(
        code: 'download_http_error',
        message: 'Nie udało się pobrać firmware z GitHuba.',
        cause: error,
      );
    } on IOException catch (error) {
      if (cancellationToken.isCanceled || _disposed) {
        throw const FirmwareReleaseException(
          code: 'download_canceled',
          message: 'Pobieranie firmware zostało anulowane.',
        );
      }
      throw FirmwareReleaseException(
        code: 'download_io_error',
        message: 'Nie udało się odczytać pakietu firmware.',
        cause: error,
      );
    } finally {
      callbacksEnabled = false;
      cancellationToken.removeListener(stopTransfer);
      _activeDownloadClients.remove(client);
      client.close(force: true);
    }
  }

  Future<Uint8List> _downloadFirmwarePackage({
    required HttpClient client,
    required FirmwareRelease release,
    required void Function(double progress) onProgress,
    required FirmwareDownloadCancellationToken cancellationToken,
  }) async {
    _throwIfCanceled(cancellationToken);
    final response = await _openTrustedDownload(
      client,
      release.asset.downloadUri,
      cancellationToken,
    );
    _throwIfCanceled(cancellationToken);
    if (response.statusCode != HttpStatus.ok) {
      await response.drain<void>().timeout(_requestTimeout);
      throw FirmwareReleaseException(
        code: 'download_http_${response.statusCode}',
        message: 'GitHub nie udostępnił pakietu firmware.',
      );
    }
    if (response.contentLength >= 0 &&
        response.contentLength != release.asset.size) {
      await response.drain<void>().timeout(_requestTimeout);
      throw const FirmwareReleaseException(
        code: 'download_content_length_mismatch',
        message: 'Rozmiar pliku na serwerze nie zgadza się z wydaniem.',
      );
    }

    final builder = BytesBuilder(copy: false);
    var received = 0;
    onProgress(0);
    await for (final chunk in response.timeout(_requestTimeout)) {
      _throwIfCanceled(cancellationToken);
      received += chunk.length;
      if (received > release.asset.size || received > maximumPackageBytes) {
        throw const FirmwareReleaseException(
          code: 'download_too_large',
          message: 'Pakiet przekroczył bezpieczny limit rozmiaru.',
        );
      }
      builder.add(chunk);
      onProgress(received / release.asset.size);
    }
    _throwIfCanceled(cancellationToken);
    if (received != release.asset.size) {
      throw FirmwareReleaseException(
        code: 'download_size_mismatch',
        message:
            'Pobrano niepełny pakiet firmware '
            '($received z ${release.asset.size} B).',
      );
    }

    final bytes = builder.takeBytes();
    final actualDigest = sha256.convert(bytes).toString();
    if (actualDigest != release.asset.sha256) {
      throw const FirmwareReleaseException(
        code: 'download_digest_mismatch',
        message: 'SHA-256 pobranego pakietu nie zgadza się z wydaniem.',
      );
    }
    _throwIfCanceled(cancellationToken);
    onProgress(1);
    return bytes;
  }

  Future<HttpClientResponse> _openTrustedDownload(
    HttpClient client,
    Uri initialUri,
    FirmwareDownloadCancellationToken cancellationToken,
  ) async {
    var uri = initialUri;
    for (var redirectCount = 0; redirectCount <= 5; redirectCount++) {
      _throwIfCanceled(cancellationToken);
      if (!_isTrustedDownload(uri)) {
        throw const FirmwareReleaseException(
          code: 'untrusted_download_host',
          message: 'Pobieranie przekierowano do niezaufanego serwera.',
        );
      }
      final request = await client.getUrl(uri);
      request
        ..followRedirects = false
        ..headers.set(HttpHeaders.acceptHeader, 'application/octet-stream')
        ..headers.set(HttpHeaders.userAgentHeader, 'AquaCYD-Firmware-Updater');
      final response = await request.close().timeout(_requestTimeout);
      _throwIfCanceled(cancellationToken);
      if (!_isRedirect(response.statusCode)) return response;

      final location = response.headers.value(HttpHeaders.locationHeader);
      await response.drain<void>().timeout(_requestTimeout);
      _throwIfCanceled(cancellationToken);
      if (location == null || redirectCount == 5) {
        throw const FirmwareReleaseException(
          code: 'invalid_download_redirect',
          message: 'Nie udało się bezpiecznie pobrać pakietu z GitHuba.',
        );
      }
      uri = uri.resolve(location);
    }
    throw const FirmwareReleaseException(
      code: 'too_many_download_redirects',
      message: 'GitHub wykonał zbyt wiele przekierowań.',
    );
  }

  bool _isTrustedDownload(Uri uri) {
    final override = _trustedDownloadUriOverride;
    return override?.call(uri) ??
        _isTrustedDownloadUri(uri, owner: owner, repository: repository);
  }

  static void _throwIfCanceled(
    FirmwareDownloadCancellationToken cancellationToken,
  ) {
    if (!cancellationToken.isCanceled) return;
    throw const FirmwareReleaseException(
      code: 'download_canceled',
      message: 'Pobieranie firmware zostało anulowane.',
    );
  }

  static bool _isRedirect(int statusCode) {
    return statusCode == HttpStatus.movedPermanently ||
        statusCode == HttpStatus.found ||
        statusCode == HttpStatus.seeOther ||
        statusCode == HttpStatus.temporaryRedirect ||
        statusCode == HttpStatus.permanentRedirect;
  }

  static bool _isTrustedDownloadUri(
    Uri uri, {
    required String owner,
    required String repository,
  }) {
    if (uri.scheme != 'https' || uri.userInfo.isNotEmpty || uri.hasPort) {
      return false;
    }
    final host = uri.host.toLowerCase();
    if (host == 'github.com') {
      return uri.path.startsWith('/$owner/$repository/releases/download/');
    }
    return host == 'objects.githubusercontent.com' ||
        host == 'release-assets.githubusercontent.com' ||
        host.endsWith('.githubusercontent.com');
  }

  static Future<Uint8List> _readLimited(
    Stream<List<int>> stream, {
    required int maximumBytes,
    required Duration inactivityTimeout,
  }) async {
    final builder = BytesBuilder(copy: false);
    var length = 0;
    await for (final chunk in stream.timeout(inactivityTimeout)) {
      length += chunk.length;
      if (length > maximumBytes) {
        throw const FirmwareReleaseException(
          code: 'api_response_too_large',
          message: 'Odpowiedź GitHuba przekroczyła bezpieczny limit.',
        );
      }
      builder.add(chunk);
    }
    return builder.takeBytes();
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    for (final client in List<HttpClient>.of(_activeDownloadClients)) {
      client.close(force: true);
    }
    _activeDownloadClients.clear();
    if (_ownsHttpClient) _httpClient.close(force: true);
  }
}
