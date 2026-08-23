import 'package:http/http.dart' as http;

http.Client createHubHttpClient({
  required bool bootstrap,
  String? expectedFingerprint,
  void Function(String fingerprint)? onCertificate,
}) {
  // Przeglądarka samodzielnie weryfikuje certyfikat i nie udostępnia go Dartowi.
  // Dlatego wersja webowa działa wyłącznie z certyfikatem zaufanym przez system.
  return http.Client();
}
