import '../full_controller/command_center_models.dart';
import 'local_history_entry.dart';
import 'local_history_repository.dart';

/// Adapter integracyjny dla sesji sterownika. Zapisuje tylko wartości potrzebne
/// do wykresów offline; pełny status i dane uwierzytelniające nie trafiają do DB.
final class LocalHistoryRecorder {
  const LocalHistoryRecorder(this.repository);

  final LocalHistoryRepository repository;

  Future<void> recordStatus(
    CommandCenterModel model, {
    DateTime? observedAt,
    String source = 'controller',
  }) async {
    final at = (observedAt ?? DateTime.now()).toUtc();
    final values = <String, Object?>{
      for (final sensor in model.sensors)
        if (sensor.numericValue != null && sensor.valid)
          sensor.kind.name: sensor.numericValue,
      for (final sensor in model.sensors)
        if (sensor.binaryValue != null && sensor.valid)
          sensor.kind.name: sensor.binaryValue,
    };
    if (values.isEmpty) return;
    await repository.append(
      LocalHistoryEntry(
        id: LocalHistoryEntry.createId(
          timestamp: at,
          category: LocalHistoryCategory.measurement,
          discriminator: values.toString(),
        ),
        category: LocalHistoryCategory.measurement,
        timestamp: at,
        title: 'Odczyt sterownika',
        detail: model.safety.title,
        source: source,
        values: values,
      ),
    );
  }

  Future<void> recordCommand({
    required String action,
    required bool succeeded,
    String detail = '',
    DateTime? occurredAt,
  }) {
    final at = (occurredAt ?? DateTime.now()).toUtc();
    return repository.append(
      LocalHistoryEntry(
        id: LocalHistoryEntry.createId(
          timestamp: at,
          category: LocalHistoryCategory.command,
          discriminator: '$action|$succeeded',
        ),
        category: LocalHistoryCategory.command,
        timestamp: at,
        title: action,
        detail: detail,
        source: 'mobile',
        values: <String, Object?>{'succeeded': succeeded},
      ),
    );
  }
}
