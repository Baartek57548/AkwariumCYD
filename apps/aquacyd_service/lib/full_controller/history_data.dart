import 'dart:typed_data';

import 'data_access.dart';

class HistorySample {
  const HistorySample({
    required this.epoch,
    this.temperature,
    this.ph,
    this.ldr,
    this.heapBytes,
    this.heaterOn = false,
  });

  factory HistorySample.fromStatus(Object? value) {
    final sample = jsonMap(value);
    return HistorySample(
      epoch: sample.integer('epoch'),
      temperature: sample.nullableNumber('value'),
      heaterOn: sample.flag('heaterOn'),
    );
  }

  final int epoch;
  final double? temperature;
  final double? ph;
  final int? ldr;
  final int? heapBytes;
  final bool heaterOn;
}

class HistoryLoadResult {
  const HistoryLoadResult({
    required this.samples,
    required this.requestedRange,
    required this.usedArchive,
    this.warning,
  });

  final List<HistorySample> samples;
  final Duration requestedRange;
  final bool usedArchive;
  final String? warning;

  Duration get availableRange {
    if (samples.length < 2) return Duration.zero;
    return Duration(seconds: samples.last.epoch - samples.first.epoch);
  }
}

class HistoryArchiveCodec {
  static const int magic = 0x31485141;
  static const int version = 1;
  static const int headerSize = 32;
  static const int recordSize = 18;

  static List<HistorySample> decode(Uint8List bytes) {
    if (bytes.length < headerSize) {
      throw const FormatException('Archiwum historii jest zbyt krótkie.');
    }
    final data = ByteData.sublistView(bytes);
    final actualMagic = data.getUint32(0, Endian.little);
    final actualVersion = data.getUint16(4, Endian.little);
    final actualHeaderSize = data.getUint16(6, Endian.little);
    final actualRecordSize = data.getUint16(8, Endian.little);
    if (actualMagic != magic ||
        actualVersion != version ||
        actualHeaderSize != headerSize ||
        actualRecordSize != recordSize) {
      throw const FormatException('Nieobsługiwany format archiwum historii.');
    }
    final payloadSize = bytes.length - headerSize;
    if (payloadSize % recordSize != 0) {
      throw const FormatException('Archiwum historii zawiera niepełny rekord.');
    }

    final samples = <HistorySample>[];
    for (
      var offset = headerSize;
      offset + recordSize <= bytes.length;
      offset += recordSize
    ) {
      final epoch = data.getUint32(offset, Endian.little);
      final flags = data.getUint8(offset + 14);
      if (epoch == 0) continue;
      samples.add(
        HistorySample(
          epoch: epoch,
          temperature: flags & 0x01 != 0
              ? data.getInt16(offset + 4, Endian.little) / 100
              : null,
          ph: flags & 0x02 != 0
              ? data.getInt16(offset + 6, Endian.little) / 1000
              : null,
          ldr: flags & 0x04 != 0
              ? data.getInt16(offset + 8, Endian.little)
              : null,
          heapBytes: data.getUint32(offset + 10, Endian.little),
          heaterOn: flags & 0x08 != 0,
        ),
      );
    }
    return List.unmodifiable(samples);
  }
}
