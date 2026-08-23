import 'dart:async';

import 'domain.dart';
import 'event_socket_stub.dart'
    if (dart.library.io) 'event_socket_io.dart'
    as platform;

abstract interface class HubEventSocket {
  Stream<void> get registryChanges;

  Future<void> connect();

  Future<void> close();
}

HubEventSocket createHubEventSocket(HubCredentials credentials) =>
    platform.createHubEventSocket(credentials);
