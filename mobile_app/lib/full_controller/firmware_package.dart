import 'dart:convert';
import 'dart:typed_data';

import 'package:crypto/crypto.dart';

enum FirmwareTarget {
  ili9341(1, 'ili9341', 'ILI9341'),
  st7789(2, 'st7789', 'ST7789');

  const FirmwareTarget(this.id, this.code, this.label);

  final int id;
  final String code;
  final String label;

  static FirmwareTarget? fromId(int id) {
    for (final target in values) {
      if (target.id == id) return target;
    }
    return null;
  }

  static FirmwareTarget? fromCode(String value) {
    final normalized = value.trim().toLowerCase().replaceAll(
      RegExp(r'[^a-z0-9]'),
      '',
    );
    for (final target in values) {
      if (normalized == target.code) return target;
    }
    return null;
  }
}

class FirmwarePackageException implements Exception {
  const FirmwarePackageException({required this.code, required this.message});

  final String code;
  final String message;

  @override
  String toString() => message;
}

class FirmwarePackageValidationContext {
  const FirmwarePackageValidationContext({
    required this.expectedTarget,
    required this.currentVersion,
    required this.minimumSecurityVersion,
    required this.bootloaderVersion,
    required this.maximumImageBytes,
  });

  final FirmwareTarget expectedTarget;
  final String currentVersion;
  final int minimumSecurityVersion;
  final int bootloaderVersion;
  final int maximumImageBytes;
}

class FirmwarePackage {
  const FirmwarePackage({
    required this.bytes,
    required this.target,
    required this.imageBytes,
    required this.securityVersion,
    required this.imageSha256,
    required this.firmwareVersion,
    required this.productId,
    required this.keyId,
    required this.commit,
    required this.minimumBootloaderVersion,
  });

  final Uint8List bytes;
  final FirmwareTarget target;
  final int imageBytes;
  final int securityVersion;
  final String imageSha256;
  final String firmwareVersion;
  final String productId;
  final String keyId;
  final String commit;
  final int minimumBootloaderVersion;

  int get packageBytes => bytes.length;

  String get shortDigest =>
      '${imageSha256.substring(0, 12)}…${imageSha256.substring(56)}';
}

abstract final class FirmwarePackageParser {
  static const int signedMetadataBytes = 128;
  static const int signatureBytes = 384;
  static const int headerBytes = signedMetadataBytes + signatureBytes;
  static const int formatVersion = 1;
  static const int algorithmRsa3072PssSha256 = 1;
  static const String productId = 'aquacyd-cyd';
  static const String trustedKeyId = '9470c281de5f898f';
  static const int defaultMaximumImageBytes = 8 * 1024 * 1024;

  static final Uint8List _magic = Uint8List.fromList(ascii.encode('AQCYDOTA'));
  static final RegExp _semanticVersionPattern = RegExp(
    r'^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$',
  );
  static final RegExp _canonicalTextPattern = RegExp(r'^[A-Za-z0-9_-]+$');
  static final RegExp _canonicalVersionTextPattern = RegExp(
    r'^[A-Za-z0-9_.-]+$',
  );
  static final RegExp _keyIdPattern = RegExp(r'^[0-9a-f]{16}$');
  static final RegExp _commitPattern = RegExp(r'^[0-9a-f]{7,19}$');

  static FirmwarePackage parse(
    Uint8List bytes, {
    String? fileName,
    FirmwarePackageValidationContext? context,
    int maximumImageBytes = defaultMaximumImageBytes,
  }) {
    if (fileName != null && !fileName.toLowerCase().endsWith('.aqfw')) {
      throw const FirmwarePackageException(
        code: 'invalid_firmware_extension',
        message: 'Wybierz podpisany pakiet firmware z rozszerzeniem .aqfw.',
      );
    }
    if (maximumImageBytes <= 0 ||
        maximumImageBytes > defaultMaximumImageBytes) {
      throw const FirmwarePackageException(
        code: 'invalid_firmware_limit',
        message: 'Limit obrazu firmware jest nieprawidłowy.',
      );
    }
    if (bytes.length < headerBytes) {
      throw const FirmwarePackageException(
        code: 'header_too_short',
        message: 'Pakiet jest krótszy niż wymagany nagłówek 512 B.',
      );
    }

    for (var index = 0; index < _magic.length; index++) {
      if (bytes[index] != _magic[index]) {
        throw const FirmwarePackageException(
          code: 'invalid_magic',
          message: 'Plik nie jest pakietem firmware AquaCYD.',
        );
      }
    }

    final header = ByteData.sublistView(bytes, 0, headerBytes);
    final format = header.getUint16(8, Endian.little);
    final declaredHeaderBytes = header.getUint16(10, Endian.little);
    if (format != formatVersion || declaredHeaderBytes != headerBytes) {
      throw const FirmwarePackageException(
        code: 'unsupported_format',
        message: 'Ta wersja formatu pakietu firmware nie jest obsługiwana.',
      );
    }
    if (header.getUint8(12) != algorithmRsa3072PssSha256) {
      throw const FirmwarePackageException(
        code: 'unsupported_algorithm',
        message: 'Pakiet używa nieobsługiwanego algorytmu podpisu.',
      );
    }
    if (header.getUint16(14, Endian.little) != 0 ||
        header.getUint16(126, Endian.little) != 0) {
      throw const FirmwarePackageException(
        code: 'invalid_flags',
        message: 'Nagłówek pakietu zawiera nieobsługiwane flagi.',
      );
    }

    final target = FirmwareTarget.fromId(header.getUint8(13));
    if (target == null) {
      throw const FirmwarePackageException(
        code: 'invalid_target',
        message: 'Pakiet wskazuje nieznany wariant sprzętowy.',
      );
    }

    final imageBytes = header.getUint32(16, Endian.little);
    final effectiveMaximum = context?.maximumImageBytes ?? maximumImageBytes;
    if (imageBytes <= 0 ||
        imageBytes > maximumImageBytes ||
        imageBytes > effectiveMaximum) {
      throw FirmwarePackageException(
        code: 'invalid_image_size',
        message:
            'Obraz firmware ma nieprawidłowy rozmiar '
            '($imageBytes B, limit $effectiveMaximum B).',
      );
    }
    if (bytes.length != headerBytes + imageBytes) {
      throw FirmwarePackageException(
        code: 'package_size_mismatch',
        message:
            'Rozmiar pakietu nie zgadza się z podpisanym nagłówkiem '
            '(oczekiwano ${headerBytes + imageBytes} B, jest ${bytes.length} B).',
      );
    }

    final declaredDigest = Uint8List.sublistView(bytes, 24, 56);
    if (_allZero(declaredDigest)) {
      throw const FirmwarePackageException(
        code: 'invalid_digest',
        message: 'Nagłówek nie zawiera poprawnego skrótu SHA-256.',
      );
    }
    final actualDigest = sha256
        .convert(Uint8List.sublistView(bytes, headerBytes))
        .bytes;
    if (!_constantTimeEqual(declaredDigest, actualDigest)) {
      throw const FirmwarePackageException(
        code: 'firmware_digest_mismatch',
        message: 'SHA-256 obrazu nie zgadza się z podpisanym nagłówkiem.',
      );
    }

    final firmwareVersion = _readCanonicalText(
      bytes,
      56,
      16,
      code: 'invalid_version',
      label: 'wersja firmware',
      allowed: _canonicalVersionTextPattern,
    );
    if (_semanticVersionParts(firmwareVersion) == null) {
      throw const FirmwarePackageException(
        code: 'invalid_version',
        message: 'Wersja firmware nie jest poprawnym SemVer X.Y.Z.',
      );
    }

    final parsedProductId = _readCanonicalText(
      bytes,
      72,
      16,
      code: 'invalid_product',
      label: 'identyfikator produktu',
      allowed: _canonicalTextPattern,
    );
    if (parsedProductId != productId) {
      throw FirmwarePackageException(
        code: 'invalid_product',
        message: 'Pakiet jest przeznaczony dla produktu „$parsedProductId”.',
      );
    }

    final keyId = _readExactAscii(bytes, 88, 16);
    if (!_keyIdPattern.hasMatch(keyId) || keyId != trustedKeyId) {
      throw const FirmwarePackageException(
        code: 'invalid_key_id',
        message: 'Pakiet nie pochodzi z zaufanego klucza wydawniczego AquaCYD.',
      );
    }

    final commit = _readCanonicalText(
      bytes,
      104,
      20,
      code: 'invalid_commit',
      label: 'identyfikator rewizji',
      allowed: _canonicalTextPattern,
    );
    if (!_commitPattern.hasMatch(commit)) {
      throw const FirmwarePackageException(
        code: 'invalid_commit',
        message: 'Pakiet zawiera nieprawidłowy identyfikator rewizji.',
      );
    }

    final signature = Uint8List.sublistView(
      bytes,
      signedMetadataBytes,
      headerBytes,
    );
    if (_allZero(signature)) {
      throw const FirmwarePackageException(
        code: 'invalid_signature',
        message: 'Pakiet nie zawiera podpisu RSA-3072.',
      );
    }

    final securityVersion = header.getUint32(20, Endian.little);
    final minimumBootloaderVersion = header.getUint16(124, Endian.little);
    if (context != null) {
      _validateCompatibility(
        target: target,
        firmwareVersion: firmwareVersion,
        securityVersion: securityVersion,
        minimumBootloaderVersion: minimumBootloaderVersion,
        context: context,
      );
    }

    return FirmwarePackage(
      bytes: bytes,
      target: target,
      imageBytes: imageBytes,
      securityVersion: securityVersion,
      imageSha256: _hex(actualDigest),
      firmwareVersion: firmwareVersion,
      productId: parsedProductId,
      keyId: keyId,
      commit: commit,
      minimumBootloaderVersion: minimumBootloaderVersion,
    );
  }

  static int compareSemanticVersions(String left, String right) {
    final leftParts = _semanticVersionParts(left);
    final rightParts = _semanticVersionParts(right);
    if (leftParts == null || rightParts == null) {
      throw const FirmwarePackageException(
        code: 'invalid_version',
        message: 'Nie można porównać nieprawidłowych wersji firmware.',
      );
    }
    for (var index = 0; index < 3; index++) {
      final comparison = leftParts[index].compareTo(rightParts[index]);
      if (comparison != 0) return comparison;
    }
    return 0;
  }

  static void _validateCompatibility({
    required FirmwareTarget target,
    required String firmwareVersion,
    required int securityVersion,
    required int minimumBootloaderVersion,
    required FirmwarePackageValidationContext context,
  }) {
    if (target != context.expectedTarget) {
      throw FirmwarePackageException(
        code: 'wrong_target',
        message:
            'Pakiet ${target.label} nie pasuje do sterownika '
            '${context.expectedTarget.label}.',
      );
    }
    if (!_semanticVersionPattern.hasMatch(context.currentVersion)) {
      throw const FirmwarePackageException(
        code: 'current_version_unknown',
        message: 'Sterownik nie podał wersji wymaganej do bezpiecznego OTA.',
      );
    }
    if (compareSemanticVersions(firmwareVersion, context.currentVersion) < 0) {
      throw FirmwarePackageException(
        code: 'downgrade_blocked',
        message:
            'Ze względów bezpieczeństwa nie można cofnąć firmware '
            'z ${context.currentVersion} do $firmwareVersion.',
      );
    }
    if (securityVersion < context.minimumSecurityVersion) {
      throw const FirmwarePackageException(
        code: 'security_rollback_blocked',
        message: 'Pakiet ma zbyt niską wersję zabezpieczeń.',
      );
    }
    if (minimumBootloaderVersion > context.bootloaderVersion) {
      throw FirmwarePackageException(
        code: 'bootloader_too_old',
        message:
            'Pakiet wymaga bootloadera $minimumBootloaderVersion, '
            'a sterownik udostępnia wersję ${context.bootloaderVersion}.',
      );
    }
  }

  static String _readCanonicalText(
    Uint8List bytes,
    int offset,
    int length, {
    required String code,
    required String label,
    required RegExp allowed,
  }) {
    final field = Uint8List.sublistView(bytes, offset, offset + length);
    final terminator = field.indexOf(0);
    if (terminator <= 0) {
      throw FirmwarePackageException(
        code: code,
        message: 'Pole „$label” nie ma kanonicznego zakończenia.',
      );
    }
    for (var index = terminator; index < field.length; index++) {
      if (field[index] != 0) {
        throw FirmwarePackageException(
          code: code,
          message: 'Pole „$label” zawiera nieprawidłowe dopełnienie.',
        );
      }
    }
    final value = _decodeAscii(field, 0, terminator, code, label);
    if (!allowed.hasMatch(value)) {
      throw FirmwarePackageException(
        code: code,
        message: 'Pole „$label” zawiera niedozwolone znaki.',
      );
    }
    return value;
  }

  static String _readExactAscii(Uint8List bytes, int offset, int length) {
    return _decodeAscii(
      Uint8List.sublistView(bytes, offset, offset + length),
      0,
      length,
      'invalid_key_id',
      'identyfikator klucza',
    );
  }

  static String _decodeAscii(
    Uint8List bytes,
    int start,
    int end,
    String code,
    String label,
  ) {
    for (var index = start; index < end; index++) {
      if (bytes[index] > 0x7f) {
        throw FirmwarePackageException(
          code: code,
          message: 'Pole „$label” nie jest tekstem ASCII.',
        );
      }
    }
    return ascii.decode(bytes.sublist(start, end));
  }

  static List<int>? _semanticVersionParts(String value) {
    final match = _semanticVersionPattern.firstMatch(value);
    if (match == null) return null;
    final result = <int>[];
    for (var index = 1; index <= 3; index++) {
      final parsed = int.tryParse(match.group(index)!);
      if (parsed == null || parsed > 0xffffffff) return null;
      result.add(parsed);
    }
    return result;
  }

  static bool _allZero(List<int> bytes) {
    var combined = 0;
    for (final byte in bytes) {
      combined |= byte;
    }
    return combined == 0;
  }

  static bool _constantTimeEqual(List<int> left, List<int> right) {
    if (left.length != right.length) return false;
    var difference = 0;
    for (var index = 0; index < left.length; index++) {
      difference |= left[index] ^ right[index];
    }
    return difference == 0;
  }

  static String _hex(List<int> bytes) {
    final buffer = StringBuffer();
    for (final byte in bytes) {
      buffer.write(byte.toRadixString(16).padLeft(2, '0'));
    }
    return buffer.toString();
  }
}
