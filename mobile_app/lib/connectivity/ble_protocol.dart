import 'dart:convert';
import 'dart:typed_data';

class BleFrameAssembler {
  static const int headerLength = 4;
  static const int maximumPartCount = 64;

  final Map<int, _PendingBleMessage> _pending = {};

  String? add(List<int> rawFrame) {
    if (rawFrame.length < headerLength) {
      throw const FormatException('Ramka BLE jest krótsza niż nagłówek.');
    }
    final frame = Uint8List.fromList(rawFrame);
    final messageId = frame[0] | (frame[1] << 8);
    final partIndex = frame[2];
    final partCount = frame[3];
    if (partCount == 0 || partCount > maximumPartCount) {
      throw const FormatException('Nieprawidłowa liczba części ramki BLE.');
    }
    if (partIndex >= partCount) {
      throw const FormatException(
        'Indeks części ramki BLE jest poza zakresem.',
      );
    }

    _pending.removeWhere(
      (_, message) =>
          DateTime.now().difference(message.createdAt) >
          const Duration(seconds: 10),
    );
    final pending = _pending.putIfAbsent(
      messageId,
      () => _PendingBleMessage(partCount),
    );
    if (pending.partCount != partCount) {
      _pending.remove(messageId);
      throw const FormatException('Niespójna liczba części wiadomości BLE.');
    }
    pending.parts[partIndex] = frame.sublist(headerLength);
    if (pending.parts.any((part) => part == null)) {
      return null;
    }

    final builder = BytesBuilder(copy: false);
    for (final part in pending.parts) {
      builder.add(part!);
    }
    _pending.remove(messageId);
    return utf8.decode(builder.takeBytes(), allowMalformed: false);
  }

  void clear() => _pending.clear();
}

class _PendingBleMessage {
  _PendingBleMessage(this.partCount)
    : createdAt = DateTime.now(),
      parts = List<Uint8List?>.filled(partCount, null);

  final int partCount;
  final DateTime createdAt;
  final List<Uint8List?> parts;
}
