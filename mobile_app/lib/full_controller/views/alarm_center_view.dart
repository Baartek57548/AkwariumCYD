import 'dart:async';

import 'package:flutter/material.dart';

import '../../alarm_center/alarm_center_api.dart';
import '../../controller_runtime_services.dart';
import '../../design_system.dart';
import '../../local_history/local_history_api.dart';
import '../widgets.dart';

class AlarmCenterView extends StatefulWidget {
  const AlarmCenterView({super.key, required this.services});

  final ControllerRuntimeServices services;

  @override
  State<AlarmCenterView> createState() => _AlarmCenterViewState();
}

class _AlarmCenterViewState extends State<AlarmCenterView> {
  final Set<String> _busyItems = <String>{};
  bool _savingPreferences = false;
  bool _requestingPermission = false;

  @override
  void initState() {
    super.initState();
    widget.services.addListener(_onChanged);
    unawaited(widget.services.initialize());
  }

  @override
  void didUpdateWidget(AlarmCenterView oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (identical(oldWidget.services, widget.services)) return;
    oldWidget.services.removeListener(_onChanged);
    widget.services.addListener(_onChanged);
    unawaited(widget.services.initialize());
  }

  @override
  void dispose() {
    widget.services.removeListener(_onChanged);
    super.dispose();
  }

  void _onChanged() {
    if (mounted) setState(() {});
  }

  Future<void> _acknowledge(AlarmRecord alarm) async {
    if (_busyItems.contains(alarm.key)) return;
    setState(() => _busyItems.add(alarm.key));
    try {
      await widget.services.acknowledgeAlarm(alarm.key);
      if (mounted) _message('Alarm został potwierdzony.');
    } on Object {
      if (mounted) _message('Nie udało się potwierdzić alarmu.', error: true);
    } finally {
      if (mounted) setState(() => _busyItems.remove(alarm.key));
    }
  }

  Future<void> _completeReminder(ServiceReminder reminder) async {
    if (_busyItems.contains(reminder.id)) return;
    setState(() => _busyItems.add(reminder.id));
    try {
      await widget.services.completeReminder(reminder.id);
      if (mounted) _message('Zapisano wykonanie zadania serwisowego.');
    } on Object {
      if (mounted) _message('Nie udało się zapisać zadania.', error: true);
    } finally {
      if (mounted) setState(() => _busyItems.remove(reminder.id));
    }
  }

  Future<bool> _savePreferences(
    AlarmNotificationPreferences preferences,
  ) async {
    if (_savingPreferences) return false;
    setState(() => _savingPreferences = true);
    try {
      await widget.services.saveAlarmPreferences(preferences);
      return true;
    } on Object {
      if (mounted) {
        _message('Nie udało się zapisać ustawień alarmów.', error: true);
      }
      return false;
    } finally {
      if (mounted) setState(() => _savingPreferences = false);
    }
  }

  Future<void> _setNotificationsEnabled(bool enabled) async {
    if (!enabled) {
      await _savePreferences(
        widget.services.preferences.copyWith(enabled: false),
      );
      return;
    }
    if (_requestingPermission || _savingPreferences) return;
    setState(() => _requestingPermission = true);
    try {
      final granted = await widget.services.requestNotificationPermission();
      if (!mounted) return;
      if (!granted) {
        _message(
          'Nie przyznano uprawnienia. Powiadomienia pozostają wyłączone.',
          error: true,
        );
        return;
      }
      final saved = await _savePreferences(
        widget.services.preferences.copyWith(enabled: true),
      );
      if (mounted && saved) {
        _message('Powiadomienia o alarmach są aktywne.');
      }
    } finally {
      if (mounted) setState(() => _requestingPermission = false);
    }
  }

  Future<void> _requestPermission() async {
    if (_requestingPermission) return;
    setState(() => _requestingPermission = true);
    try {
      final granted = await widget.services.requestNotificationPermission();
      if (!mounted) return;
      _message(
        granted
            ? 'Uprawnienie systemowe jest aktywne.'
            : 'Nie przyznano uprawnienia do powiadomień.',
        error: !granted,
      );
    } finally {
      if (mounted) setState(() => _requestingPermission = false);
    }
  }

  void _message(String text, {bool error = false}) {
    final colors = Theme.of(context).colorScheme;
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(
        SnackBar(
          content: Text(text),
          backgroundColor: error ? colors.error : null,
          behavior: SnackBarBehavior.floating,
          showCloseIcon: true,
        ),
      );
  }

  @override
  Widget build(BuildContext context) {
    final services = widget.services;
    final active = services.alarms
        .where((alarm) => alarm.isActive)
        .toList(growable: false);
    final resolved = services.alarms
        .where((alarm) => !alarm.isActive)
        .take(20)
        .toList(growable: false);
    return Scaffold(
      appBar: AppBar(
        title: const Text('Alarmy i opieka'),
        actions: [
          IconButton(
            tooltip: 'Odśwież lokalne dane',
            onPressed: () => unawaited(services.refresh()),
            icon: const Icon(Icons.refresh_rounded),
          ),
          const SizedBox(width: 4),
        ],
      ),
      body: ControllerPageBody(
        maxWidth: 980,
        onRefresh: services.refresh,
        children: [
          _AlarmSummaryCard(
            activeCount: active.length,
            criticalCount: services.criticalAlarmCount,
            initialized: services.initialized,
            hasEvaluatedSnapshot: services.hasEvaluatedCompleteSnapshot,
            lastEvaluatedAt: services.lastCompleteMeasurementAt,
          ),
          if (services.warning case final warning?) ...[
            const SizedBox(height: AquaSpacing.sm),
            StatusBanner(
              icon: Icons.info_outline_rounded,
              title: 'Ograniczona usługa lokalna',
              message: warning,
              isError: false,
            ),
          ],
          const SizedBox(height: AquaSpacing.lg),
          const SectionHeader(
            title: 'Aktywne alarmy',
            description:
                'Alarm pozostaje zapisany także bez połączenia ze sterownikiem.',
          ),
          if (active.isEmpty)
            services.hasEvaluatedCompleteSnapshot
                ? const _EmptyState(
                    icon: Icons.verified_rounded,
                    title: 'Brak aktywnych alarmów',
                    message:
                        'Ostatni kompletny odczyt nie wymaga interwencji. '
                        'Historia rozwiązanych zdarzeń pozostaje poniżej.',
                  )
                : const _EmptyState(
                    icon: Icons.sensors_off_rounded,
                    title: 'Brak zweryfikowanych danych',
                    message:
                        'Połącz sterownik i poczekaj na kompletny odczyt. '
                        'Do tego czasu aplikacja nie potwierdza bezpiecznego '
                        'stanu akwarium.',
                    success: false,
                  )
          else
            for (final alarm in active) ...[
              _AlarmCard(
                alarm: alarm,
                busy: _busyItems.contains(alarm.key),
                onAcknowledge: () => unawaited(_acknowledge(alarm)),
              ),
              const SizedBox(height: AquaSpacing.sm),
            ],
          const SizedBox(height: AquaSpacing.lg),
          const SectionHeader(
            title: 'Przypomnienia serwisowe',
            description: 'Lokalny plan regularnej opieki nad akwarium.',
          ),
          ResponsiveGrid(
            minimumChildWidth: 280,
            spacing: AquaSpacing.sm,
            children: [
              for (final reminder in services.reminders)
                _ReminderCard(
                  reminder: reminder,
                  busy: _busyItems.contains(reminder.id),
                  onComplete: () => unawaited(_completeReminder(reminder)),
                ),
            ],
          ),
          const SizedBox(height: AquaSpacing.lg),
          _NotificationSettingsCard(
            preferences: services.preferences,
            saving: _savingPreferences || _requestingPermission,
            onChanged: (value) => unawaited(_savePreferences(value)),
            onEnabledChanged: (value) =>
                unawaited(_setNotificationsEnabled(value)),
            onRequestPermission: () => unawaited(_requestPermission()),
          ),
          if (resolved.isNotEmpty) ...[
            const SizedBox(height: AquaSpacing.lg),
            Card(
              clipBehavior: Clip.antiAlias,
              child: ExpansionTile(
                leading: const Icon(Icons.task_alt_rounded),
                title: const Text(
                  'Ostatnio rozwiązane',
                  style: TextStyle(fontWeight: FontWeight.w800),
                ),
                subtitle: Text('${resolved.length} zapisanych zdarzeń'),
                children: [
                  for (final alarm in resolved)
                    ListTile(
                      leading: const Icon(Icons.check_circle_outline_rounded),
                      title: Text(alarm.title),
                      subtitle: Text(
                        '${alarm.message}\n${_dateTime(alarm.resolvedAt)}',
                      ),
                      isThreeLine: true,
                    ),
                ],
              ),
            ),
          ],
          if (services.history.isNotEmpty) ...[
            const SizedBox(height: AquaSpacing.sm),
            Card(
              clipBehavior: Clip.antiAlias,
              child: ExpansionTile(
                leading: const Icon(Icons.history_rounded),
                title: const Text(
                  'Historia lokalna',
                  style: TextStyle(fontWeight: FontWeight.w800),
                ),
                subtitle: Text(
                  '${services.history.length} ostatnich pomiarów i operacji',
                ),
                children: [
                  for (final entry in services.history.take(40))
                    ListTile(
                      leading: Icon(_historyIcon(entry.category)),
                      title: Text(entry.title),
                      subtitle: Text(
                        entry.detail.isEmpty
                            ? _dateTime(entry.timestamp)
                            : '${entry.detail}\n${_dateTime(entry.timestamp)}',
                      ),
                      isThreeLine: entry.detail.isNotEmpty,
                    ),
                ],
              ),
            ),
          ],
        ],
      ),
    );
  }
}

class _AlarmSummaryCard extends StatelessWidget {
  const _AlarmSummaryCard({
    required this.activeCount,
    required this.criticalCount,
    required this.initialized,
    required this.hasEvaluatedSnapshot,
    required this.lastEvaluatedAt,
  });

  final int activeCount;
  final int criticalCount;
  final bool initialized;
  final bool hasEvaluatedSnapshot;
  final DateTime? lastEvaluatedAt;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final statusColors = context.statusColors;
    final tone = criticalCount > 0
        ? colors.error
        : activeCount > 0
        ? statusColors.warning
        : !hasEvaluatedSnapshot
        ? colors.outline
        : statusColors.success;
    return Card(
      margin: EdgeInsets.zero,
      color: tone.withValues(alpha: 0.12),
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.lg),
        child: Row(
          children: [
            Container(
              width: 56,
              height: 56,
              decoration: BoxDecoration(
                color: tone.withValues(alpha: 0.16),
                borderRadius: BorderRadius.circular(AquaRadius.control),
              ),
              child: Icon(
                criticalCount > 0
                    ? Icons.crisis_alert_rounded
                    : activeCount > 0
                    ? Icons.notification_important_rounded
                    : Icons.shield_rounded,
                color: tone,
                size: 30,
              ),
            ),
            const SizedBox(width: AquaSpacing.md),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    !initialized
                        ? 'Uruchamianie centrum alarmów…'
                        : criticalCount > 0
                        ? '$criticalCount alarmów krytycznych'
                        : activeCount > 0
                        ? '$activeCount aktywnych alarmów'
                        : !hasEvaluatedSnapshot
                        ? 'Brak danych do oceny'
                        : 'Akwarium pod opieką',
                    style: Theme.of(context).textTheme.titleLarge?.copyWith(
                      color: tone,
                      fontWeight: FontWeight.w900,
                    ),
                  ),
                  const SizedBox(height: AquaSpacing.xxs),
                  Text(
                    activeCount > 0
                        ? 'Alarm pozostaje dostępny także bez połączenia.'
                        : hasEvaluatedSnapshot
                        ? 'Ostatnia pełna ocena: ${_dateTime(lastEvaluatedAt)}.'
                        : 'Połącz sterownik, aby wykonać pierwszą pełną ocenę.',
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _AlarmCard extends StatelessWidget {
  const _AlarmCard({
    required this.alarm,
    required this.busy,
    required this.onAcknowledge,
  });

  final AlarmRecord alarm;
  final bool busy;
  final VoidCallback onAcknowledge;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final critical = alarm.severity == AlarmSeverity.critical;
    final tone = critical ? colors.error : context.statusColors.warning;
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.md),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Icon(
                  critical
                      ? Icons.crisis_alert_rounded
                      : Icons.warning_amber_rounded,
                  color: tone,
                ),
                const SizedBox(width: AquaSpacing.sm),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        alarm.title,
                        style: const TextStyle(fontWeight: FontWeight.w900),
                      ),
                      const SizedBox(height: AquaSpacing.xxs),
                      Text(alarm.message),
                    ],
                  ),
                ),
                Chip(
                  label: Text(
                    alarm.lifecycle == AlarmLifecycle.acknowledged
                        ? 'POTWIERDZONY'
                        : critical
                        ? 'KRYTYCZNY'
                        : 'UWAGA',
                  ),
                ),
              ],
            ),
            const SizedBox(height: AquaSpacing.sm),
            Text(
              'Ostatnio: ${_dateTime(alarm.lastTriggeredAt)} · '
              'wystąpienia: ${alarm.occurrences}',
              style: Theme.of(
                context,
              ).textTheme.bodySmall?.copyWith(color: colors.onSurfaceVariant),
            ),
            if (alarm.lifecycle == AlarmLifecycle.newAlarm) ...[
              const SizedBox(height: AquaSpacing.md),
              FilledButton.tonalIcon(
                onPressed: busy ? null : onAcknowledge,
                icon: busy
                    ? const SizedBox.square(
                        dimension: 18,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : const Icon(Icons.done_rounded),
                label: const Text('Potwierdź, że widziałem'),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _ReminderCard extends StatelessWidget {
  const _ReminderCard({
    required this.reminder,
    required this.busy,
    required this.onComplete,
  });

  final ServiceReminder reminder;
  final bool busy;
  final VoidCallback onComplete;

  @override
  Widget build(BuildContext context) {
    final due = reminder.isDue(DateTime.now());
    final colors = Theme.of(context).colorScheme;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.md),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                Icon(
                  due
                      ? Icons.event_busy_rounded
                      : Icons.event_available_rounded,
                  color: due ? context.statusColors.warning : colors.primary,
                ),
                const SizedBox(width: AquaSpacing.sm),
                Expanded(
                  child: Text(
                    reminder.title,
                    style: const TextStyle(fontWeight: FontWeight.w900),
                  ),
                ),
              ],
            ),
            const SizedBox(height: AquaSpacing.xs),
            Text(reminder.detail),
            const SizedBox(height: AquaSpacing.xs),
            Text(
              due
                  ? 'Termin minął: ${_dateTime(reminder.dueAt)}'
                  : 'Następny termin: ${_dateTime(reminder.dueAt)}',
              style: TextStyle(
                color: due
                    ? context.statusColors.warning
                    : colors.onSurfaceVariant,
                fontWeight: FontWeight.w700,
              ),
            ),
            const SizedBox(height: AquaSpacing.md),
            OutlinedButton.icon(
              onPressed: busy ? null : onComplete,
              icon: busy
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.task_alt_rounded),
              label: const Text('Oznacz jako wykonane'),
            ),
          ],
        ),
      ),
    );
  }
}

class _NotificationSettingsCard extends StatelessWidget {
  const _NotificationSettingsCard({
    required this.preferences,
    required this.saving,
    required this.onChanged,
    required this.onEnabledChanged,
    required this.onRequestPermission,
  });

  final AlarmNotificationPreferences preferences;
  final bool saving;
  final ValueChanged<AlarmNotificationPreferences> onChanged;
  final ValueChanged<bool> onEnabledChanged;
  final VoidCallback onRequestPermission;

  @override
  Widget build(BuildContext context) {
    return Card(
      clipBehavior: Clip.antiAlias,
      child: ExpansionTile(
        leading: const Icon(Icons.notifications_active_outlined),
        title: const Text(
          'Powiadomienia',
          style: TextStyle(fontWeight: FontWeight.w900),
        ),
        subtitle: Text(
          preferences.backgroundSyncEnabled
              ? 'Alarmy lokalne · kontrola Wi‑Fi w tle'
              : 'Alarmy podczas używania aplikacji',
        ),
        childrenPadding: const EdgeInsets.fromLTRB(
          AquaSpacing.sm,
          0,
          AquaSpacing.sm,
          AquaSpacing.md,
        ),
        children: [
          SwitchListTile.adaptive(
            value: preferences.enabled,
            onChanged: saving ? null : onEnabledChanged,
            title: const Text('Powiadomienia o alarmach'),
            subtitle: const Text(
              'Krytyczne zdarzenia i ostrzeżenia z pełnych odczytów.',
            ),
          ),
          SwitchListTile.adaptive(
            value: preferences.backgroundSyncEnabled,
            onChanged: saving
                ? null
                : (value) => onChanged(
                    preferences.copyWith(backgroundSyncEnabled: value),
                  ),
            title: const Text('Sprawdzaj lokalne Wi‑Fi w tle'),
            subtitle: const Text(
              'Opcjonalnie, co około 30 minut. Działa tylko dla zapisanego '
              'sterownika w tej samej sieci; nie zastępuje chmury ani VPN.',
            ),
          ),
          SwitchListTile.adaptive(
            value: preferences.resolvedEnabled,
            onChanged: saving
                ? null
                : (value) =>
                      onChanged(preferences.copyWith(resolvedEnabled: value)),
            title: const Text('Informuj o rozwiązaniu alarmu'),
          ),
          const SizedBox(height: AquaSpacing.xs),
          SizedBox(
            width: double.infinity,
            child: OutlinedButton.icon(
              onPressed: onRequestPermission,
              icon: const Icon(Icons.security_rounded),
              label: const Text('Sprawdź uprawnienie systemowe'),
            ),
          ),
        ],
      ),
    );
  }
}

class _EmptyState extends StatelessWidget {
  const _EmptyState({
    required this.icon,
    required this.title,
    required this.message,
    this.success = true,
  });

  final IconData icon;
  final String title;
  final String message;
  final bool success;

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.all(AquaSpacing.lg),
        child: Column(
          children: [
            Icon(
              icon,
              size: 38,
              color: success
                  ? context.statusColors.success
                  : Theme.of(context).colorScheme.outline,
            ),
            const SizedBox(height: AquaSpacing.sm),
            Text(
              title,
              style: Theme.of(
                context,
              ).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w900),
            ),
            const SizedBox(height: AquaSpacing.xs),
            Text(message, textAlign: TextAlign.center),
          ],
        ),
      ),
    );
  }
}

IconData _historyIcon(LocalHistoryCategory category) => switch (category) {
  LocalHistoryCategory.measurement => Icons.monitor_heart_outlined,
  LocalHistoryCategory.alarm => Icons.notifications_active_outlined,
  LocalHistoryCategory.command => Icons.touch_app_outlined,
  LocalHistoryCategory.service => Icons.handyman_outlined,
  LocalHistoryCategory.synchronization => Icons.sync_rounded,
};

String _dateTime(DateTime? value) {
  if (value == null) return 'brak daty';
  final local = value.toLocal();
  String two(int number) => number.toString().padLeft(2, '0');
  return '${two(local.day)}.${two(local.month)}.${local.year} '
      '${two(local.hour)}:${two(local.minute)}';
}
