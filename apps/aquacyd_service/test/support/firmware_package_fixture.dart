import 'dart:convert';
import 'dart:typed_data';

import 'package:crypto/crypto.dart';
import 'package:cyd_aquarium_mobile/full_controller/firmware_package.dart';

Uint8List buildFirmwarePackageFixture({
  FirmwareTarget target = FirmwareTarget.ili9341,
  String version = '5.1.0',
  int securityVersion = 2,
  String productId = FirmwarePackageParser.productId,
  String keyId = FirmwarePackageParser.trustedKeyId,
  String commit = '0123456789abcdef012',
  int minimumBootloaderVersion = 1,
  Uint8List? image,
}) {
  final payload =
      image ??
      Uint8List.fromList(
        List<int>.generate(4096, (index) => (index * 17 + 3) & 0xff),
      );
  final package = Uint8List(FirmwarePackageParser.headerBytes + payload.length);
  package.setRange(0, 8, ascii.encode('AQCYDOTA'));
  final header = ByteData.sublistView(
    package,
    0,
    FirmwarePackageParser.headerBytes,
  );
  header.setUint16(8, FirmwarePackageParser.formatVersion, Endian.little);
  header.setUint16(10, FirmwarePackageParser.headerBytes, Endian.little);
  header.setUint8(12, FirmwarePackageParser.algorithmRsa3072PssSha256);
  header.setUint8(13, target.id);
  header.setUint16(14, 0, Endian.little);
  header.setUint32(16, payload.length, Endian.little);
  header.setUint32(20, securityVersion, Endian.little);
  package.setRange(24, 56, sha256.convert(payload).bytes);
  _writeFixed(package, 56, 16, version);
  _writeFixed(package, 72, 16, productId);
  final encodedKeyId = ascii.encode(keyId);
  if (encodedKeyId.length != 16) {
    throw ArgumentError.value(keyId, 'keyId', 'Wymagane jest 16 znaków ASCII.');
  }
  package.setRange(88, 104, encodedKeyId);
  _writeFixed(package, 104, 20, commit);
  header.setUint16(124, minimumBootloaderVersion, Endian.little);
  header.setUint16(126, 0, Endian.little);
  for (
    var index = FirmwarePackageParser.signedMetadataBytes;
    index < FirmwarePackageParser.headerBytes;
    index++
  ) {
    package[index] = ((index * 29) & 0xff) | 1;
  }
  package.setRange(FirmwarePackageParser.headerBytes, package.length, payload);
  return package;
}

void writeFirmwareFixtureField(
  Uint8List package,
  int offset,
  int length,
  String value,
) {
  _writeFixed(package, offset, length, value);
}

void _writeFixed(Uint8List destination, int offset, int length, String value) {
  final encoded = ascii.encode(value);
  if (encoded.isEmpty || encoded.length >= length) {
    throw ArgumentError.value(
      value,
      'value',
      'Pole musi zajmować od 1 do ${length - 1} bajtów ASCII.',
    );
  }
  destination.fillRange(offset, offset + length, 0);
  destination.setRange(offset, offset + encoded.length, encoded);
}
