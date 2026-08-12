import '../full_controller/command_center_models.dart';
import 'alarm_center.dart';
import 'alarm_engine.dart';
import 'alarm_models.dart';

extension AlarmCenterCommandCenterIngestion on AlarmCenter {
  Future<AlarmDispatchReport> ingestCommandCenterModel(
    CommandCenterModel model, {
    DateTime? observedAt,
    bool completeSnapshot = true,
    AlarmPolicy policy = const AlarmPolicy(),
  }) {
    return ingestSignals(
      model.activeAlarms.map(commandCenterAlarmSignal),
      observedAt: observedAt,
      completeSnapshot: completeSnapshot,
      policy: policy,
    );
  }
}

AlarmSignal commandCenterAlarmSignal(CommandCenterAlarm alarm) {
  return AlarmSignal(
    key: alarm.kind.name,
    severity: alarm.severity == CommandCenterAlarmSeverity.critical
        ? AlarmSeverity.critical
        : AlarmSeverity.warning,
    title: alarm.title,
    message: alarm.message,
  );
}
