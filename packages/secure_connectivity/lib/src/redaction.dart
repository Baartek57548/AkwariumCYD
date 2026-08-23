abstract final class LogRedactor {
  static final List<RegExp> _secretPatterns = <RegExp>[
    RegExp(r'(authorization\s*[:=]\s*bearer\s+)[^\s,;]+', caseSensitive: false),
    RegExp(
      r'((?:access_?token|refresh_?token|password|secret|api_?key)\s*[:=]\s*)[^\s,;]+',
      caseSensitive: false,
    ),
    RegExp(r'([?&](?:token|key|secret)=)[^&#\s]+', caseSensitive: false),
  ];

  static String redact(Object? value) {
    var text = value?.toString() ?? '';
    for (final pattern in _secretPatterns) {
      text = text.replaceAllMapped(
        pattern,
        (match) => '${match.group(1)}<redacted>',
      );
    }
    return text.length <= 2048 ? text : '${text.substring(0, 2048)}<truncated>';
  }

  static Map<String, Object?> redactMap(Map<String, Object?> values) =>
      <String, Object?>{
        for (final entry in values.entries)
          entry.key: _isSecretKey(entry.key)
              ? '<redacted>'
              : entry.value is Map<String, Object?>
              ? redactMap(entry.value! as Map<String, Object?>)
              : redact(entry.value),
      };

  static bool _isSecretKey(String key) => RegExp(
    r'token|password|secret|authorization|cookie|api.?key',
    caseSensitive: false,
  ).hasMatch(key);
}
