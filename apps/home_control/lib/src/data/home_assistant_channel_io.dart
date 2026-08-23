import 'dart:io';

import 'package:web_socket_channel/io.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

final HttpClient _webSocketClient = _createWebSocketClient();

WebSocketChannel connectHomeAssistantChannel(Uri uri, {String? hostHeader}) =>
    IOWebSocketChannel.connect(
      uri,
      headers: hostHeader == null
          ? null
          : <String, String>{HttpHeaders.hostHeader: hostHeader},
      customClient: _webSocketClient,
    );

HttpClient _createWebSocketClient() {
  final delegate = HttpClient()..findProxy = (_) => 'DIRECT';
  return _NoRedirectHttpClient(delegate);
}

final class _NoRedirectHttpClient implements HttpClient {
  _NoRedirectHttpClient(this._delegate);

  final HttpClient _delegate;

  @override
  Future<HttpClientRequest> openUrl(String method, Uri url) async {
    final request = await _delegate.openUrl(method, url);
    request.followRedirects = false;
    return request;
  }

  @override
  void close({bool force = false}) => _delegate.close(force: force);

  @override
  dynamic noSuchMethod(Invocation invocation) => throw UnsupportedError(
    'WebSocket client supports only direct openUrl and close operations.',
  );
}
