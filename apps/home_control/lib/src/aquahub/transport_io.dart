import 'dart:convert';
import 'dart:io';

import 'package:crypto/crypto.dart';
import 'package:http/http.dart' as http;
import 'package:http/io_client.dart';

import 'domain.dart';

http.Client createHubHttpClient({
  required bool bootstrap,
  String? expectedFingerprint,
  void Function(String fingerprint)? onCertificate,
}) {
  final expected = expectedFingerprint == null
      ? null
      : normalizeFingerprint(expectedFingerprint);
  final native = HttpClient()..connectionTimeout = const Duration(seconds: 8);
  native.badCertificateCallback = (certificate, host, port) {
    final fingerprint = _sha256Fingerprint(certificate);
    onCertificate?.call(fingerprint);
    if (bootstrap) return true;
    return expected != null && fingerprint == expected;
  };
  return IOClient(native);
}

String _sha256Fingerprint(X509Certificate certificate) {
  final normalizedPem = certificate.pem
      .replaceAll('-----BEGIN CERTIFICATE-----', '')
      .replaceAll('-----END CERTIFICATE-----', '')
      .replaceAll(RegExp(r'\s'), '');
  final der = base64Decode(normalizedPem);
  return sha256.convert(der).toString().toUpperCase();
}
