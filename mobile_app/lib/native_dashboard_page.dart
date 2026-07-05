import 'dart:async';

import 'package:flutter/material.dart';

import 'connectivity/controller_transport.dart';

class NativeDashboardPage extends StatefulWidget {
  const NativeDashboardPage({super.key, required this.transport});

  final ControllerTransport transport;

  @override
  State<NativeDashboardPage> createState() => _NativeDashboardPageState();
}

class _NativeDashboardPageState extends State<NativeDashboardPage> {
  StreamSubscription<ControllerTransportState>? _stateSubscription;
  StreamSubscription<ControllerSnapshot>? _snapshotSubscription;
  ControllerTransportState _state = ControllerTransportState.connecting;
  ControllerSnapshot? _snapshot;
  String? _connectionError;
  String? _sessionPin;
  final Set<OutputChannel> _busyOutputs = {};
  bool _feeding = false;

  @override
  void initState() {
    super.initState();
    _stateSubscription = widget.transport.stateChanges.listen((state) {
      if (mounted) {
        setState(() => _state = state);
      }
    });
    _snapshotSubscription = widget.transport.snapshots.listen((snapshot) {
      if (mounted) {
        setState(() {
          _snapshot = snapshot;
          _connectionError = null;
        });
      }
    });
    _connect();
  }

  Future<void> _connect() async {
    setState(() {
      _state = ControllerTransportState.connecting;
      _connectionError = null;
    });
    try {
      await widget.transport.connect();
    } catch (error) {
      if (mounted) {
        setState(() {
          _state = ControllerTransportState.error;
          _connectionError = 'Nie udało się połączyć: $error';
        });
      }
    }
  }

  Future<String?> _requirePin() async {
    if (_sessionPin != null) {
      return _sessionPin;
    }
    final pin = await showDialog<String>(
      context: context,
      builder: (_) => const _PinDialog(),
    );
    if (pin != null && mounted) {
      setState(() => _sessionPin = pin);
    }
    return pin;
  }

  Future<void> _setOutput(OutputChannel channel, bool enabled) async {
    final pin = await _requirePin();
    if (pin == null || !mounted) {
      return;
    }
    setState(() => _busyOutputs.add(channel));
    final result = await widget.transport.setOutput(channel, enabled, pin);
    if (!mounted) {
      return;
    }
    setState(() => _busyOutputs.remove(channel));
    if (result.code == 'pin_invalid') {
      setState(() => _sessionPin = null);
    }
    _showResult(result);
  }

  Future<void> _feed() async {
    final pin = await _requirePin();
    if (pin == null || !mounted) {
      return;
    }
    setState(() => _feeding = true);
    final result = await widget.transport.feed(pin);
    if (!mounted) {
      return;
    }
    setState(() => _feeding = false);
    if (result.code == 'pin_invalid') {
      setState(() => _sessionPin = null);
    }
    _showResult(result);
  }

  void _showResult(ControllerCommandResult result) {
    final colors = Theme.of(context).colorScheme;
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(
        SnackBar(
          backgroundColor: result.success ? colors.primary : colors.error,
          content: Text(result.message),
        ),
      );
  }

  @override
  void dispose() {
    _stateSubscription?.cancel();
    _snapshotSubscription?.cancel();
    unawaited(widget.transport.dispose());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final snapshot = _snapshot;
    return Scaffold(
      appBar: AppBar(
        title: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(widget.transport.displayName),
            Text(
              _stateLabel(_state),
              style: Theme.of(context).textTheme.labelSmall,
            ),
          ],
        ),
        actions: [
          if (_sessionPin != null)
            IconButton(
              onPressed: () => setState(() => _sessionPin = null),
              icon: const Icon(Icons.lock_open_rounded),
              tooltip: 'Usuń PIN z sesji',
            ),
        ],
      ),
      body: snapshot == null
          ? _ConnectionStateView(
              state: _state,
              error: _connectionError,
              onRetry: _connect,
            )
          : RefreshIndicator(
              onRefresh: _connect,
              child: ListView(
                padding: const EdgeInsets.fromLTRB(14, 10, 14, 28),
                children: [
                  if (widget.transport.isDeveloperTransport ||
                      snapshot.developerMode)
                    const _DevBanner(),
                  _SafetyBanner(snapshot: snapshot),
                  const SizedBox(height: 12),
                  Text(
                    'Telemetria',
                    style: Theme.of(context).textTheme.titleLarge?.copyWith(
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                  const SizedBox(height: 8),
                  LayoutBuilder(
                    builder: (context, constraints) {
                      final width = constraints.maxWidth > 520
                          ? (constraints.maxWidth - 12) / 2
                          : constraints.maxWidth;
                      return Wrap(
                        spacing: 12,
                        runSpacing: 12,
                        children: [
                          _MetricCard(
                            width: width,
                            icon: Icons.thermostat_rounded,
                            label: 'Temperatura',
                            value: snapshot.temperatureValid
                                ? '${snapshot.temperature.toStringAsFixed(2)} °C'
                                : '--',
                            detail:
                                'Cel ${snapshot.targetTemperature.toStringAsFixed(1)} °C',
                          ),
                          _MetricCard(
                            width: width,
                            icon: Icons.science_outlined,
                            label: 'Parametry wody',
                            value: snapshot.phValid
                                ? 'pH ${snapshot.ph.toStringAsFixed(2)}'
                                : 'pH --',
                            detail: snapshot.ecValid
                                ? 'EC ${snapshot.ec.toStringAsFixed(0)} µS/cm'
                                : 'EC --',
                          ),
                          _MetricCard(
                            width: width,
                            icon: Icons.light_mode_outlined,
                            label: 'Oświetlenie otoczenia',
                            value: snapshot.ldrValid ? '${snapshot.ldr}' : '--',
                            detail: 'Surowy odczyt LDR',
                          ),
                          _MetricCard(
                            width: width,
                            icon: Icons.memory_rounded,
                            label: 'Sterownik',
                            value:
                                '${(snapshot.freeHeapBytes / 1024).toStringAsFixed(1)} KiB',
                            detail:
                                'Czas pracy ${_formatUptime(snapshot.uptimeSeconds)}',
                          ),
                        ],
                      );
                    },
                  ),
                  const SizedBox(height: 20),
                  Text(
                    'Moduły',
                    style: Theme.of(context).textTheme.titleLarge?.copyWith(
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                  const SizedBox(height: 8),
                  Card(
                    child: Column(
                      children: [
                        for (
                          var index = 0;
                          index < OutputChannel.values.length;
                          index++
                        ) ...[
                          _OutputTile(
                            channel: OutputChannel.values[index],
                            enabled:
                                snapshot.outputs[OutputChannel.values[index]] ??
                                false,
                            busy: _busyOutputs.contains(
                              OutputChannel.values[index],
                            ),
                            onChanged: (value) =>
                                _setOutput(OutputChannel.values[index], value),
                          ),
                          if (index < OutputChannel.values.length - 1)
                            const Divider(height: 1),
                        ],
                      ],
                    ),
                  ),
                  const SizedBox(height: 14),
                  FilledButton.icon(
                    onPressed: _feeding ? null : _feed,
                    icon: _feeding
                        ? const SizedBox.square(
                            dimension: 18,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Icon(Icons.set_meal_rounded),
                    label: Text(
                      _feeding ? 'Uruchamianie…' : 'Podaj pokarm teraz',
                    ),
                  ),
                ],
              ),
            ),
    );
  }

  String _stateLabel(ControllerTransportState state) {
    return switch (state) {
      ControllerTransportState.disconnected => 'Rozłączono',
      ControllerTransportState.scanning => 'Skanowanie',
      ControllerTransportState.connecting => 'Łączenie…',
      ControllerTransportState.connected => 'Połączono',
      ControllerTransportState.error => 'Błąd połączenia',
    };
  }

  String _formatUptime(int totalSeconds) {
    final hours = totalSeconds ~/ 3600;
    final minutes = (totalSeconds % 3600) ~/ 60;
    final seconds = totalSeconds % 60;
    return '${hours.toString().padLeft(2, '0')}:'
        '${minutes.toString().padLeft(2, '0')}:'
        '${seconds.toString().padLeft(2, '0')}';
  }
}

class _ConnectionStateView extends StatelessWidget {
  const _ConnectionStateView({
    required this.state,
    required this.error,
    required this.onRetry,
  });

  final ControllerTransportState state;
  final String? error;
  final VoidCallback onRetry;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(28),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (state == ControllerTransportState.error)
              Icon(
                Icons.bluetooth_disabled_rounded,
                size: 70,
                color: Theme.of(context).colorScheme.error,
              )
            else
              const SizedBox.square(
                dimension: 52,
                child: CircularProgressIndicator(),
              ),
            const SizedBox(height: 20),
            Text(
              error ?? 'Nawiązywanie połączenia ze sterownikiem…',
              textAlign: TextAlign.center,
            ),
            if (error != null) ...[
              const SizedBox(height: 18),
              FilledButton.icon(
                onPressed: onRetry,
                icon: const Icon(Icons.refresh),
                label: const Text('Połącz ponownie'),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _DevBanner extends StatelessWidget {
  const _DevBanner();

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      margin: const EdgeInsets.only(bottom: 10),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: colors.tertiaryContainer,
        borderRadius: BorderRadius.circular(5),
      ),
      child: const Row(
        children: [
          Icon(Icons.science_outlined),
          SizedBox(width: 10),
          Expanded(
            child: Text(
              'TRYB DEV — dane i akcje są symulowane w RAM; sprzęt nie jest przełączany.',
            ),
          ),
        ],
      ),
    );
  }
}

class _SafetyBanner extends StatelessWidget {
  const _SafetyBanner({required this.snapshot});

  final ControllerSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    final safe = snapshot.alarmFlags == 0 && !snapshot.leakDetected;
    final colors = Theme.of(context).colorScheme;
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: safe ? colors.primaryContainer : colors.errorContainer,
        borderRadius: BorderRadius.circular(5),
      ),
      child: Row(
        children: [
          Icon(safe ? Icons.check_circle_rounded : Icons.warning_rounded),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              safe
                  ? 'System stabilny · poziom wody ${snapshot.waterLevelHigh ? 'OK' : 'NISKI'}'
                  : 'Aktywne alarmy: ${snapshot.alarmFlags} · '
                        '${snapshot.leakDetected ? 'WYKRYTO WYCIEK' : 'sprawdź czujniki'}',
              style: const TextStyle(fontWeight: FontWeight.w700),
            ),
          ),
        ],
      ),
    );
  }
}

class _MetricCard extends StatelessWidget {
  const _MetricCard({
    required this.width,
    required this.icon,
    required this.label,
    required this.value,
    required this.detail,
  });

  final double width;
  final IconData icon;
  final String label;
  final String value;
  final String detail;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: width,
      child: Card(
        margin: EdgeInsets.zero,
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(icon, size: 20),
                  const SizedBox(width: 8),
                  Expanded(child: Text(label)),
                ],
              ),
              const SizedBox(height: 12),
              Text(
                value,
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                  fontWeight: FontWeight.w800,
                ),
              ),
              Text(
                detail,
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                  color: Theme.of(context).colorScheme.onSurfaceVariant,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _OutputTile extends StatelessWidget {
  const _OutputTile({
    required this.channel,
    required this.enabled,
    required this.busy,
    required this.onChanged,
  });

  final OutputChannel channel;
  final bool enabled;
  final bool busy;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    return SwitchListTile(
      secondary: busy
          ? const SizedBox.square(
              dimension: 22,
              child: CircularProgressIndicator(strokeWidth: 2),
            )
          : Icon(_channelIcon(channel)),
      title: Text(channel.label),
      subtitle: Text(enabled ? 'Włączony' : 'Wyłączony'),
      value: enabled,
      onChanged: busy ? null : onChanged,
    );
  }

  IconData _channelIcon(OutputChannel channel) {
    return switch (channel) {
      OutputChannel.light => Icons.lightbulb_rounded,
      OutputChannel.plantLight => Icons.local_florist_rounded,
      OutputChannel.filter => Icons.filter_alt_rounded,
      OutputChannel.heater => Icons.thermostat_rounded,
      OutputChannel.aeration => Icons.air_rounded,
    };
  }
}

class _PinDialog extends StatefulWidget {
  const _PinDialog();

  @override
  State<_PinDialog> createState() => _PinDialogState();
}

class _PinDialogState extends State<_PinDialog> {
  final TextEditingController _controller = TextEditingController();
  String? _error;

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  void _submit() {
    final value = _controller.text.trim();
    if (!RegExp(r'^\d{4,8}$').hasMatch(value)) {
      setState(() => _error = 'PIN musi zawierać od 4 do 8 cyfr.');
      return;
    }
    Navigator.of(context).pop(value);
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('PIN administratora'),
      content: TextField(
        controller: _controller,
        autofocus: true,
        obscureText: true,
        keyboardType: TextInputType.number,
        maxLength: 8,
        decoration: InputDecoration(errorText: _error),
        onSubmitted: (_) => _submit(),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Anuluj'),
        ),
        FilledButton(onPressed: _submit, child: const Text('Potwierdź')),
      ],
    );
  }
}
