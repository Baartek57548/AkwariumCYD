import 'domain.dart';
import 'event_socket.dart';

HubEventSocket createHubEventSocket(HubCredentials credentials) =>
    _UnsupportedHubEventSocket();

final class _UnsupportedHubEventSocket implements HubEventSocket {
  @override
  Stream<void> get registryChanges => const Stream<void>.empty();

  @override
  Future<void> connect() async {}

  @override
  Future<void> close() async {}
}
