import 'package:home_entities/home_entities.dart';
import 'package:secure_connectivity/secure_connectivity.dart';

abstract interface class HomeDataSource {
  String get sourceId;
  String get displayName;
  HomeSourceKind get kind;
  Stream<HomeEntity> get stateChanges;

  Future<HomeSnapshot> connect(CancellationToken cancellation);
  Future<HomeSnapshot> refresh(CancellationToken cancellation);
  Future<void> sendCommand(
    HomeEntity entity,
    Object? value,
    CancellationToken cancellation,
  );
  Future<List<HistoryPoint>> loadHistory(
    HomeEntity entity,
    Duration period,
    CancellationToken cancellation,
  );
  Future<void> installUpdate(HomeUpdate update, CancellationToken cancellation);
  Future<void> close();
}

abstract interface class HomeCredentialsCleaner {
  Future<void> clearCredentials();
}
