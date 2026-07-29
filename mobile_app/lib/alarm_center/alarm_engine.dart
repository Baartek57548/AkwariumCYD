import 'alarm_models.dart';
import 'alarm_repository.dart';

final class AlarmPolicy {
  const AlarmPolicy({
    this.warningActivationSamples = 2,
    this.criticalActivationSamples = 1,
    this.resolutionSamples = 2,
    this.notificationCooldown = const Duration(minutes: 30),
    this.maximumObservationGap = const Duration(minutes: 10),
  }) : assert(warningActivationSamples > 0),
       assert(criticalActivationSamples > 0),
       assert(resolutionSamples > 0);

  final int warningActivationSamples;
  final int criticalActivationSamples;
  final int resolutionSamples;
  final Duration notificationCooldown;
  final Duration maximumObservationGap;

  int activationSamples(AlarmSeverity severity) =>
      severity == AlarmSeverity.critical
      ? criticalActivationSamples
      : warningActivationSamples;

  AlarmPolicy copyWith({
    int? warningActivationSamples,
    int? criticalActivationSamples,
    int? resolutionSamples,
    Duration? notificationCooldown,
    Duration? maximumObservationGap,
  }) {
    return AlarmPolicy(
      warningActivationSamples:
          warningActivationSamples ?? this.warningActivationSamples,
      criticalActivationSamples:
          criticalActivationSamples ?? this.criticalActivationSamples,
      resolutionSamples: resolutionSamples ?? this.resolutionSamples,
      notificationCooldown: notificationCooldown ?? this.notificationCooldown,
      maximumObservationGap:
          maximumObservationGap ?? this.maximumObservationGap,
    );
  }
}

/// Deterministyczny silnik alarmów. Cały snapshot jest oceniany w jednej
/// transakcji, więc równoległy sync tła nie może zgubić potwierdzenia alarmu.
final class AlarmEngine {
  const AlarmEngine(this.store);

  final AlarmStore store;

  Future<AlarmProcessingResult> processSnapshot(
    Iterable<AlarmSignal> activeSignals, {
    DateTime? observedAt,
    AlarmPolicy policy = const AlarmPolicy(),
    bool completeSnapshot = true,
  }) {
    final now = (observedAt ?? DateTime.now()).toUtc();
    final currentSignals = _deduplicate(activeSignals);
    return store.transaction((transaction) async {
      final records = await transaction.records();
      final states = await transaction.signalStates();
      final keys = <String>{
        ...currentSignals.keys,
        if (completeSnapshot)
          ...states.keys.where((key) => !key.startsWith('remote:')),
        if (completeSnapshot)
          ...records.keys.where((key) => !key.startsWith('remote:')),
      };
      final transitions = <AlarmTransition>[];
      final notificationCandidates = <AlarmRecord>[];

      for (final key in keys) {
        final signal = currentSignals[key];
        final previousState = states[key];
        final previousRecord = records[key];
        final hasContinuity =
            previousState != null &&
            !now.isBefore(previousState.lastSeenAt) &&
            now.difference(previousState.lastSeenAt) <=
                policy.maximumObservationGap;

        if (signal != null) {
          final activeStreak = hasContinuity
              ? previousState.activeStreak + 1
              : 1;
          await transaction.saveSignalState(
            AlarmSignalState(
              key: key,
              activeStreak: activeStreak,
              clearStreak: 0,
              lastSeenAt: now,
            ),
          );
          if (activeStreak < policy.activationSamples(signal.severity)) {
            continue;
          }

          if (previousRecord == null ||
              previousRecord.lifecycle == AlarmLifecycle.resolved) {
            final opened = AlarmRecord(
              key: key,
              severity: signal.severity,
              lifecycle: AlarmLifecycle.newAlarm,
              title: signal.title,
              message: signal.message,
              firstTriggeredAt: previousRecord?.firstTriggeredAt ?? now,
              lastTriggeredAt: now,
              occurrences: (previousRecord?.occurrences ?? 0) + 1,
            );
            await transaction.saveRecord(opened);
            transitions.add(
              AlarmTransition(
                type: AlarmTransitionType.opened,
                record: opened,
                occurredAt: now,
              ),
            );
            notificationCandidates.add(opened);
            continue;
          }

          final escalated =
              previousRecord.severity == AlarmSeverity.warning &&
              signal.severity == AlarmSeverity.critical;
          final lastTriggeredAt = now.isBefore(previousRecord.lastTriggeredAt)
              ? previousRecord.lastTriggeredAt
              : now;
          final refreshed = previousRecord.copyWith(
            severity: signal.severity,
            lifecycle: escalated ? AlarmLifecycle.newAlarm : null,
            title: signal.title,
            message: signal.message,
            lastTriggeredAt: lastTriggeredAt,
            clearAcknowledgedAt: escalated,
            clearLastNotifiedAt: escalated,
          );
          await transaction.saveRecord(refreshed);
          final lastNotified = refreshed.lastNotifiedAt;
          final isDue =
              escalated ||
              (refreshed.lifecycle == AlarmLifecycle.newAlarm &&
                  (lastNotified == null ||
                      now.difference(lastNotified) >=
                          policy.notificationCooldown));
          if (isDue) {
            transitions.add(
              AlarmTransition(
                type: AlarmTransitionType.repeated,
                record: refreshed,
                occurredAt: now,
              ),
            );
            notificationCandidates.add(refreshed);
          }
          continue;
        }

        if (!completeSnapshot) continue;
        final clearStreak = hasContinuity ? previousState.clearStreak + 1 : 1;
        final clearState = AlarmSignalState(
          key: key,
          activeStreak: 0,
          clearStreak: clearStreak,
          lastSeenAt: now,
        );
        await transaction.saveSignalState(clearState);
        if (previousRecord != null &&
            previousRecord.isActive &&
            clearStreak >= policy.resolutionSamples) {
          final resolved = previousRecord.copyWith(
            lifecycle: AlarmLifecycle.resolved,
            resolvedAt: now,
          );
          await transaction.saveRecord(resolved);
          transitions.add(
            AlarmTransition(
              type: AlarmTransitionType.resolved,
              record: resolved,
              occurredAt: now,
            ),
          );
        }
        if ((previousRecord == null ||
                previousRecord.lifecycle == AlarmLifecycle.resolved) &&
            clearStreak >= policy.resolutionSamples) {
          await transaction.deleteSignalState(key);
        }
      }

      return AlarmProcessingResult(
        transitions: List<AlarmTransition>.unmodifiable(transitions),
        notificationCandidates: List<AlarmRecord>.unmodifiable(
          notificationCandidates,
        ),
      );
    });
  }

  Future<AlarmRecord?> acknowledge(String key, {DateTime? acknowledgedAt}) {
    final normalized = AlarmSignal(
      key: key,
      severity: AlarmSeverity.warning,
      title: 'Walidacja',
      message: 'Walidacja',
    ).key;
    final now = (acknowledgedAt ?? DateTime.now()).toUtc();
    return store.transaction((transaction) async {
      final record = (await transaction.records())[normalized];
      if (record == null ||
          !record.isActive ||
          record.lifecycle == AlarmLifecycle.acknowledged) {
        return null;
      }
      final acknowledged = record.copyWith(
        lifecycle: AlarmLifecycle.acknowledged,
        acknowledgedAt: now,
      );
      await transaction.saveRecord(acknowledged);
      return acknowledged;
    });
  }

  Future<void> markNotified(String key, {DateTime? notifiedAt}) async {
    final normalized = AlarmSignal(
      key: key,
      severity: AlarmSeverity.warning,
      title: 'Walidacja',
      message: 'Walidacja',
    ).key;
    final now = (notifiedAt ?? DateTime.now()).toUtc();
    await store.transaction((transaction) async {
      final record = (await transaction.records())[normalized];
      if (record == null || !record.isActive) return;
      await transaction.saveRecord(record.copyWith(lastNotifiedAt: now));
    });
  }

  static Map<String, AlarmSignal> _deduplicate(Iterable<AlarmSignal> signals) {
    final result = <String, AlarmSignal>{};
    for (final signal in signals) {
      final previous = result[signal.key];
      if (previous == null ||
          (previous.severity == AlarmSeverity.warning &&
              signal.severity == AlarmSeverity.critical)) {
        result[signal.key] = signal;
      }
    }
    return result;
  }
}
