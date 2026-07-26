import 'dart:async';

import 'package:flutter/material.dart';

import 'connection_health.dart';
import 'controller_session.dart';

class ControllerConnectionHealthBar extends StatefulWidget {
  const ControllerConnectionHealthBar({super.key, required this.session});

  final ControllerSession session;

  @override
  State<ControllerConnectionHealthBar> createState() =>
      _ControllerConnectionHealthBarState();
}

class _ControllerConnectionHealthBarState
    extends State<ControllerConnectionHealthBar>
    with WidgetsBindingObserver {
  Timer? _ageTimer;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    widget.session.addListener(_onSessionChanged);
    _startAgeTimer();
  }

  void _startAgeTimer() {
    _ageTimer?.cancel();
    _ageTimer = Timer.periodic(const Duration(seconds: 15), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      _startAgeTimer();
    } else {
      _ageTimer?.cancel();
      _ageTimer = null;
    }
  }

  @override
  void didUpdateWidget(ControllerConnectionHealthBar oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (identical(oldWidget.session, widget.session)) return;
    oldWidget.session.removeListener(_onSessionChanged);
    widget.session.addListener(_onSessionChanged);
  }

  void _onSessionChanged() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _ageTimer?.cancel();
    widget.session.removeListener(_onSessionChanged);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final health = widget.session.connectionHealth;
    final colors = Theme.of(context).colorScheme;
    final tone = switch (health.phase) {
      ControllerConnectionPhase.online => colors.primary,
      ControllerConnectionPhase.connecting => colors.tertiary,
      ControllerConnectionPhase.reconnecting => colors.tertiary,
      ControllerConnectionPhase.offline =>
        widget.session.isOfflineMode ? colors.outline : colors.error,
    };
    final detail = switch (health.phase) {
      ControllerConnectionPhase.connecting =>
        'Nawiązywanie pierwszego połączenia ze sterownikiem',
      ControllerConnectionPhase.online => 'Telemetria jest aktualizowana',
      ControllerConnectionPhase.reconnecting =>
        widget.session.automaticReconnect
            ? 'Zachowano ostatnie dane · próba ${health.failedAttempts + 1}'
            : 'Zachowano ostatnie dane · automatyczne łączenie wyłączone',
      ControllerConnectionPhase.offline =>
        widget.session.isOfflineMode
            ? 'Lokalny podgląd · wybierz połączenie w prawym górnym rogu'
            : widget.session.automaticReconnect
            ? 'Sterownik nie odpowiada · ponawianie automatyczne'
            : 'Sterownik nie odpowiada · automatyczne łączenie wyłączone',
    };

    return Semantics(
      container: true,
      label:
          'Stan połączenia: ${health.phaseLabel}. '
          'Sygnał ${health.rssi == null ? "nieznany" : "${health.rssi} dBm"}. '
          'Opóźnienie ${health.roundTrip == null ? "nieznane" : "${health.roundTrip!.inMilliseconds} milisekund"}. '
          'Ostatnia synchronizacja ${health.ageLabel(DateTime.now())}.',
      child: Material(
        color: colors.surfaceContainerLow,
        child: DecoratedBox(
          decoration: BoxDecoration(
            border: Border(bottom: BorderSide(color: colors.outlineVariant)),
          ),
          child: Padding(
            padding: const EdgeInsets.fromLTRB(14, 10, 10, 10),
            child: LayoutBuilder(
              builder: (context, constraints) {
                final textScale = MediaQuery.textScalerOf(context).scale(1);
                final compact = constraints.maxWidth < 680 || textScale > 1.35;
                final status = _StatusSummary(
                  health: health,
                  detail: detail,
                  tone: tone,
                  busy: widget.session.busy,
                  canRetry: !widget.session.isOfflineMode,
                  onRetry: () => unawaited(widget.session.connect()),
                );
                final metrics = _ConnectionMetrics(health: health);

                if (compact) {
                  return Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [status, const SizedBox(height: 10), metrics],
                  );
                }
                return Row(
                  children: [
                    Expanded(child: status),
                    const SizedBox(width: 20),
                    Flexible(child: metrics),
                  ],
                );
              },
            ),
          ),
        ),
      ),
    );
  }
}

class _StatusSummary extends StatelessWidget {
  const _StatusSummary({
    required this.health,
    required this.detail,
    required this.tone,
    required this.busy,
    required this.canRetry,
    required this.onRetry,
  });

  final ControllerConnectionHealth health;
  final String detail;
  final Color tone;
  final bool busy;
  final bool canRetry;
  final VoidCallback onRetry;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Row(
      children: [
        Container(
          width: 12,
          height: 12,
          decoration: BoxDecoration(
            color: tone,
            shape: BoxShape.circle,
            boxShadow: [
              BoxShadow(color: tone.withValues(alpha: 0.35), blurRadius: 8),
            ],
          ),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Sterownik ${health.phaseLabel.toLowerCase()}',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(fontWeight: FontWeight.w800),
              ),
              Text(
                detail,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                style: Theme.of(
                  context,
                ).textTheme.bodySmall?.copyWith(color: colors.onSurfaceVariant),
              ),
            ],
          ),
        ),
        const SizedBox(width: 8),
        IconButton(
          onPressed: busy || !canRetry ? null : onRetry,
          tooltip: health.isOnline ? 'Odśwież połączenie' : 'Połącz ponownie',
          icon: busy
              ? const SizedBox.square(
                  dimension: 20,
                  child: CircularProgressIndicator(strokeWidth: 2),
                )
              : Icon(
                  health.isOnline
                      ? Icons.refresh_rounded
                      : Icons.wifi_protected_setup_rounded,
                ),
        ),
      ],
    );
  }
}

class _ConnectionMetrics extends StatelessWidget {
  const _ConnectionMetrics({required this.health});

  final ControllerConnectionHealth health;

  @override
  Widget build(BuildContext context) {
    final textScale = MediaQuery.textScalerOf(context).scale(1);
    final vertical = textScale > 1.6;
    final items = <Widget>[
      _HealthMetric(
        icon: _signalIcon(health.signalBars),
        label: 'Sygnał',
        value: health.rssi == null ? '—' : '${health.rssi} dBm',
        tooltip: health.signalLabel,
      ),
      _HealthMetric(
        icon: Icons.speed_rounded,
        label: 'Ping',
        value: health.roundTrip == null
            ? '—'
            : '${health.roundTrip!.inMilliseconds} ms',
      ),
      _HealthMetric(
        icon: Icons.schedule_rounded,
        label: 'Synchronizacja',
        value: health.ageLabel(DateTime.now()),
      ),
    ];

    if (vertical) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          for (var index = 0; index < items.length; index++) ...[
            items[index],
            if (index != items.length - 1) const SizedBox(height: 6),
          ],
        ],
      );
    }
    return Wrap(spacing: 18, runSpacing: 8, children: items);
  }

  static IconData _signalIcon(int bars) => switch (bars) {
    4 => Icons.signal_wifi_4_bar_rounded,
    3 => Icons.network_wifi_3_bar_rounded,
    2 => Icons.network_wifi_2_bar_rounded,
    1 => Icons.network_wifi_1_bar_rounded,
    _ => Icons.signal_wifi_off_rounded,
  };
}

class _HealthMetric extends StatelessWidget {
  const _HealthMetric({
    required this.icon,
    required this.label,
    required this.value,
    this.tooltip,
  });

  final IconData icon;
  final String label;
  final String value;
  final String? tooltip;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final content = Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: 18, color: colors.primary),
        const SizedBox(width: 7),
        Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              label,
              style: Theme.of(
                context,
              ).textTheme.labelSmall?.copyWith(color: colors.onSurfaceVariant),
            ),
            Text(value, style: const TextStyle(fontWeight: FontWeight.w700)),
          ],
        ),
      ],
    );
    return tooltip == null
        ? content
        : Tooltip(message: tooltip!, child: content);
  }
}
