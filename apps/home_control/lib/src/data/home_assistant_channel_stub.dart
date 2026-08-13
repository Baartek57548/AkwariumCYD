import 'package:web_socket_channel/web_socket_channel.dart';

WebSocketChannel connectHomeAssistantChannel(Uri uri, {String? hostHeader}) =>
    WebSocketChannel.connect(uri);
