typedef JsonMap = Map<String, dynamic>;

JsonMap jsonMap(Object? value) {
  if (value is JsonMap) {
    return value;
  }
  if (value is Map) {
    return value.map((key, value) => MapEntry(key.toString(), value));
  }
  return <String, dynamic>{};
}

List<dynamic> jsonList(Object? value) =>
    value is List<dynamic> ? value : const [];

extension JsonMapAccess on JsonMap {
  JsonMap section(String key) => jsonMap(this[key]);

  List<dynamic> list(String key) => jsonList(this[key]);

  String text(String key, [String fallback = '']) {
    final value = this[key];
    return value == null ? fallback : value.toString();
  }

  bool flag(String key, [bool fallback = false]) {
    final value = this[key];
    if (value is bool) return value;
    if (value is num) return value != 0;
    if (value is String) {
      return const {'1', 'true', 'on', 'tak'}.contains(value.toLowerCase());
    }
    return fallback;
  }

  int integer(String key, [int fallback = 0]) {
    final value = this[key];
    if (value is int) return value;
    if (value is num) {
      final parsed = value.toDouble();
      return parsed.isFinite ? parsed.toInt() : fallback;
    }
    return int.tryParse(value?.toString() ?? '') ?? fallback;
  }

  double number(String key, [double fallback = 0]) {
    final value = this[key];
    final parsed = value is num
        ? value.toDouble()
        : double.tryParse(value?.toString() ?? '');
    return parsed != null && parsed.isFinite ? parsed : fallback;
  }

  double? nullableNumber(String key) {
    final value = this[key];
    if (value == null) return null;
    final parsed = value is num
        ? value.toDouble()
        : double.tryParse(value.toString());
    return parsed != null && parsed.isFinite ? parsed : null;
  }
}

String formatClock(int hour, int minute) =>
    '${hour.clamp(0, 23).toString().padLeft(2, '0')}:'
    '${minute.clamp(0, 59).toString().padLeft(2, '0')}';

String formatBytes(int bytes) {
  if (bytes < 1024) return '$bytes B';
  if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KiB';
  if (bytes < 1024 * 1024 * 1024) {
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MiB';
  }
  return '${(bytes / (1024 * 1024 * 1024)).toStringAsFixed(2)} GiB';
}

String formatUptime(int totalSeconds) {
  final days = totalSeconds ~/ 86400;
  final hours = (totalSeconds % 86400) ~/ 3600;
  final minutes = (totalSeconds % 3600) ~/ 60;
  final seconds = totalSeconds % 60;
  final clock =
      '${hours.toString().padLeft(2, '0')}:'
      '${minutes.toString().padLeft(2, '0')}:'
      '${seconds.toString().padLeft(2, '0')}';
  return days > 0 ? '$days d $clock' : clock;
}
