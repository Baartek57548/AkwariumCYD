import 'package:http/http.dart' as http;

import 'transport_stub.dart'
    if (dart.library.io) 'transport_io.dart'
    as platform;

http.Client createHubHttpClient({
  required bool bootstrap,
  String? expectedFingerprint,
  void Function(String fingerprint)? onCertificate,
}) => platform.createHubHttpClient(
  bootstrap: bootstrap,
  expectedFingerprint: expectedFingerprint,
  onCertificate: onCertificate,
);
