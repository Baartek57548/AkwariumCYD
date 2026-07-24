import 'package:flutter/foundation.dart';

@immutable
class SemanticVersion implements Comparable<SemanticVersion> {
  const SemanticVersion(this.major, this.minor, this.patch);

  static final RegExp _versionPattern = RegExp(
    r'^(0|[1-9]\d{0,8})\.(0|[1-9]\d{0,8})\.(0|[1-9]\d{0,8})$',
  );
  static final RegExp _mobileTagPattern = RegExp(
    r'^mobile-v(0|[1-9]\d{0,8})\.(0|[1-9]\d{0,8})\.(0|[1-9]\d{0,8})$',
  );

  final int major;
  final int minor;
  final int patch;

  static SemanticVersion? tryParse(String value) {
    final match = _versionPattern.firstMatch(value.trim());
    return match == null ? null : _fromMatch(match);
  }

  static SemanticVersion? tryParseMobileTag(String value) {
    final match = _mobileTagPattern.firstMatch(value.trim());
    return match == null ? null : _fromMatch(match);
  }

  static SemanticVersion _fromMatch(RegExpMatch match) {
    return SemanticVersion(
      int.parse(match.group(1)!),
      int.parse(match.group(2)!),
      int.parse(match.group(3)!),
    );
  }

  @override
  int compareTo(SemanticVersion other) {
    final majorComparison = major.compareTo(other.major);
    if (majorComparison != 0) return majorComparison;
    final minorComparison = minor.compareTo(other.minor);
    if (minorComparison != 0) return minorComparison;
    return patch.compareTo(other.patch);
  }

  bool operator >(SemanticVersion other) => compareTo(other) > 0;

  bool operator >=(SemanticVersion other) => compareTo(other) >= 0;

  bool operator <(SemanticVersion other) => compareTo(other) < 0;

  bool operator <=(SemanticVersion other) => compareTo(other) <= 0;

  @override
  bool operator ==(Object other) {
    return other is SemanticVersion &&
        major == other.major &&
        minor == other.minor &&
        patch == other.patch;
  }

  @override
  int get hashCode => Object.hash(major, minor, patch);

  @override
  String toString() => '$major.$minor.$patch';
}

@immutable
class InstalledAppInfo {
  const InstalledAppInfo({
    required this.packageName,
    required this.versionName,
    required this.versionCode,
    required this.sdkInt,
    required this.canRequestPackageInstalls,
  });

  final String packageName;
  final String versionName;
  final int versionCode;
  final int sdkInt;
  final bool canRequestPackageInstalls;

  SemanticVersion? get semanticVersion => SemanticVersion.tryParse(versionName);
}

@immutable
class ReleaseAsset {
  const ReleaseAsset({
    required this.name,
    required this.downloadUri,
    required this.size,
    required this.sha256,
    required this.contentType,
  });

  final String name;
  final Uri downloadUri;
  final int size;
  final String sha256;
  final String contentType;
}

@immutable
class AppRelease {
  const AppRelease({
    required this.version,
    required this.tagName,
    required this.title,
    required this.notes,
    required this.publishedAt,
    required this.releasePageUri,
    required this.asset,
  });

  final SemanticVersion version;
  final String tagName;
  final String title;
  final String notes;
  final DateTime? publishedAt;
  final Uri releasePageUri;
  final ReleaseAsset asset;

  String get formattedSize {
    final megabytes = asset.size / (1024 * 1024);
    return '${megabytes.toStringAsFixed(megabytes >= 10 ? 0 : 1)} MB';
  }
}

class AppUpdateException implements Exception {
  const AppUpdateException(this.code, this.userMessage, [this.cause]);

  final String code;
  final String userMessage;
  final Object? cause;

  @override
  String toString() => 'AppUpdateException($code): $userMessage';
}

class AppUpdateCanceledException extends AppUpdateException {
  const AppUpdateCanceledException()
    : super('DOWNLOAD_CANCELED', 'Pobieranie aktualizacji zostało anulowane.');
}
