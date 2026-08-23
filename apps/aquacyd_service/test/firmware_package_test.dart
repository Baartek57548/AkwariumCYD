import 'dart:typed_data';

import 'package:cyd_aquarium_mobile/full_controller/firmware_package.dart';
import 'package:flutter_test/flutter_test.dart';

import 'support/firmware_package_fixture.dart';

void main() {
  const compatibleIli9341 = FirmwarePackageValidationContext(
    expectedTarget: FirmwareTarget.ili9341,
    currentVersion: '5.0.0',
    minimumSecurityVersion: 1,
    bootloaderVersion: 1,
    maximumImageBytes: 1966080,
  );

  test('parses and validates a complete compatible .aqfw package', () {
    final bytes = buildFirmwarePackageFixture();

    final package = FirmwarePackageParser.parse(
      bytes,
      fileName: 'cydAquarium-ili9341.aqfw',
      context: compatibleIli9341,
    );

    expect(package.target, FirmwareTarget.ili9341);
    expect(package.firmwareVersion, '5.1.0');
    expect(package.securityVersion, 2);
    expect(package.productId, FirmwarePackageParser.productId);
    expect(package.keyId, FirmwarePackageParser.trustedKeyId);
    expect(package.imageBytes, 4096);
    expect(package.packageBytes, 4608);
    expect(package.imageSha256, hasLength(64));
  });

  test('rejects a file that is not an .aqfw package', () {
    final bytes = buildFirmwarePackageFixture();

    expect(
      () => FirmwarePackageParser.parse(bytes, fileName: 'firmware.bin'),
      throwsFirmwareCode('invalid_firmware_extension'),
    );
  });

  test('rejects invalid magic and unsupported header format', () {
    final invalidMagic = Uint8List.fromList(buildFirmwarePackageFixture());
    invalidMagic[0] ^= 0xff;
    expect(
      () => FirmwarePackageParser.parse(invalidMagic),
      throwsFirmwareCode('invalid_magic'),
    );

    final invalidFormat = Uint8List.fromList(buildFirmwarePackageFixture());
    ByteData.sublistView(invalidFormat).setUint16(10, 511, Endian.little);
    expect(
      () => FirmwarePackageParser.parse(invalidFormat),
      throwsFirmwareCode('unsupported_format'),
    );
  });

  test('rejects a package with mismatched size or SHA-256', () {
    final truncated = buildFirmwarePackageFixture().sublist(0, 4607);
    expect(
      () => FirmwarePackageParser.parse(Uint8List.fromList(truncated)),
      throwsFirmwareCode('package_size_mismatch'),
    );

    final tampered = Uint8List.fromList(buildFirmwarePackageFixture());
    tampered[tampered.length - 1] ^= 1;
    expect(
      () => FirmwarePackageParser.parse(tampered),
      throwsFirmwareCode('firmware_digest_mismatch'),
    );
  });

  test('rejects wrong product, signing key and semantic version', () {
    final wrongProduct = buildFirmwarePackageFixture(productId: 'other-cyd');
    expect(
      () => FirmwarePackageParser.parse(wrongProduct),
      throwsFirmwareCode('invalid_product'),
    );

    final wrongKey = buildFirmwarePackageFixture(keyId: '0123456789abcdef');
    expect(
      () => FirmwarePackageParser.parse(wrongKey),
      throwsFirmwareCode('invalid_key_id'),
    );

    final invalidVersion = buildFirmwarePackageFixture(version: '05.1.0');
    expect(
      () => FirmwarePackageParser.parse(invalidVersion),
      throwsFirmwareCode('invalid_version'),
    );

    final overflowingVersion = buildFirmwarePackageFixture(
      version: '4294967296.1.1',
    );
    expect(
      () => FirmwarePackageParser.parse(overflowingVersion),
      throwsFirmwareCode('invalid_version'),
    );
  });

  test('blocks wrong hardware target, downgrade and security rollback', () {
    final wrongTarget = buildFirmwarePackageFixture(
      target: FirmwareTarget.st7789,
    );
    expect(
      () =>
          FirmwarePackageParser.parse(wrongTarget, context: compatibleIli9341),
      throwsFirmwareCode('wrong_target'),
    );

    final downgrade = buildFirmwarePackageFixture(version: '4.9.9');
    expect(
      () => FirmwarePackageParser.parse(downgrade, context: compatibleIli9341),
      throwsFirmwareCode('downgrade_blocked'),
    );

    final oldSecurity = buildFirmwarePackageFixture(securityVersion: 0);
    expect(
      () =>
          FirmwarePackageParser.parse(oldSecurity, context: compatibleIli9341),
      throwsFirmwareCode('security_rollback_blocked'),
    );
  });

  test('blocks a package requiring a newer bootloader', () {
    final bytes = buildFirmwarePackageFixture(minimumBootloaderVersion: 2);

    expect(
      () => FirmwarePackageParser.parse(bytes, context: compatibleIli9341),
      throwsFirmwareCode('bootloader_too_old'),
    );
  });

  test('rejects a missing RSA signature', () {
    final bytes = buildFirmwarePackageFixture();
    bytes.fillRange(
      FirmwarePackageParser.signedMetadataBytes,
      FirmwarePackageParser.headerBytes,
      0,
    );

    expect(
      () => FirmwarePackageParser.parse(bytes),
      throwsFirmwareCode('invalid_signature'),
    );
  });
}

Matcher throwsFirmwareCode(String code) {
  return throwsA(
    isA<FirmwarePackageException>().having((error) => error.code, 'code', code),
  );
}
