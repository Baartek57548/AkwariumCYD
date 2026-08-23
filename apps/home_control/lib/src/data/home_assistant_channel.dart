import 'package:web_socket_channel/web_socket_channel.dart';

import 'home_assistant_channel_stub.dart'
    if (dart.library.io) 'home_assistant_channel_io.dart'
    as implementation;

WebSocketChannel connectHomeAssistantChannel(Uri uri, {String? hostHeader}) =>
    implementation.connectHomeAssistantChannel(uri, hostHeader: hostHeader);
