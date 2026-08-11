import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../design/app_theme.dart';
import 'controller.dart';
import 'domain.dart';

final class HubShell extends StatefulWidget {
  const HubShell({required this.controller, super.key});

  final HubController controller;

  @override
  State<HubShell> createState() => _HubShellState();
}

final class _HubShellState extends State<HubShell> {
  int index = 0;

  static const destinations = <_Destination>[
    _Destination(
      'Pulpit',
      Icons.space_dashboard_outlined,
      Icons.space_dashboard,
    ),
    _Destination(
      'Urządzenia',
      Icons.devices_other_outlined,
      Icons.devices_other,
    ),
    _Destination('Historia', Icons.show_chart_outlined, Icons.show_chart),
    _Destination('System', Icons.settings_outlined, Icons.settings),
  ];

  @override
  Widget build(BuildContext context) {
    final wide = MediaQuery.sizeOf(context).width >= 840;
    final content = switch (index) {
      0 => _OverviewPage(controller: widget.controller),
      1 => _DevicesPage(controller: widget.controller),
      2 => _HistoryPage(controller: widget.controller),
      _ => _SystemPage(controller: widget.controller),
    };
    return Scaffold(
      appBar: AppBar(
        title: Text(destinations[index].label),
        actions: <Widget>[
          if (widget.controller.errorMessage != null)
            IconButton(
              tooltip: widget.controller.errorMessage,
              onPressed: widget.controller.refresh,
              icon: Icon(
                Icons.cloud_off_outlined,
                color: Theme.of(context).colorScheme.error,
              ),
            ),
          IconButton(
            tooltip: 'Odśwież',
            onPressed: widget.controller.refreshing
                ? null
                : widget.controller.refresh,
            icon: widget.controller.refreshing
                ? const SizedBox.square(
                    dimension: 20,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : const Icon(Icons.refresh_rounded),
          ),
          const SizedBox(width: 8),
        ],
      ),
      body: Row(
        children: <Widget>[
          if (wide)
            NavigationRail(
              selectedIndex: index,
              labelType: NavigationRailLabelType.all,
              leading: const Padding(
                padding: EdgeInsets.only(bottom: 18),
                child: _SmallHubMark(),
              ),
              destinations: destinations
                  .map(
                    (destination) => NavigationRailDestination(
                      icon: Icon(destination.icon),
                      selectedIcon: Icon(destination.selectedIcon),
                      label: Text(destination.label),
                    ),
                  )
                  .toList(growable: false),
              onDestinationSelected: (value) => setState(() => index = value),
            ),
          Expanded(child: content),
        ],
      ),
      bottomNavigationBar: wide
          ? null
          : NavigationBar(
              selectedIndex: index,
              onDestinationSelected: (value) => setState(() => index = value),
              destinations: destinations
                  .map(
                    (destination) => NavigationDestination(
                      icon: Icon(destination.icon),
                      selectedIcon: Icon(destination.selectedIcon),
                      label: destination.label,
                    ),
                  )
                  .toList(growable: false),
            ),
    );
  }
}

final class _OverviewPage extends StatelessWidget {
  const _OverviewPage({required this.controller});

  final HubController controller;

  @override
  Widget build(BuildContext context) {
    final system = controller.system;
    final critical = controller.entities
        .where((entity) => entity.critical && entity.booleanState == true)
        .toList(growable: false);
    final metrics = controller.entities
        .where(
          (entity) =>
              entity.kind == HubEntityKind.sensor && entity.state != null,
        )
        .take(6)
        .toList(growable: false);
    final controls = controller.entities
        .where(
          (entity) =>
              entity.writable &&
              !entity.critical &&
              (entity.kind == HubEntityKind.switchEntity ||
                  entity.kind == HubEntityKind.light),
        )
        .take(6)
        .toList(growable: false);
    return _PageBody(
      children: <Widget>[
        _HealthHero(
          online: system?.onlineDeviceCount ?? 0,
          total: system?.deviceCount ?? 0,
          criticalCount: critical.length,
        ),
        if (critical.isNotEmpty) ...<Widget>[
          const SizedBox(height: 22),
          Text(
            'Wymaga uwagi',
            style: Theme.of(
              context,
            ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
          ),
          const SizedBox(height: 12),
          ...critical.map(
            (entity) => Padding(
              padding: const EdgeInsets.only(bottom: 10),
              child: _AlertTile(entity: entity),
            ),
          ),
        ],
        const SizedBox(height: 22),
        _SectionHeader(
          title: 'Aktualne pomiary',
          subtitle: '${metrics.length} najważniejszych encji',
        ),
        const SizedBox(height: 12),
        if (metrics.isEmpty)
          const _EmptyState(
            icon: Icons.sensors_off_outlined,
            text: 'Czekam na pierwsze dane z urządzeń.',
          )
        else
          LayoutBuilder(
            builder: (context, constraints) => GridView.builder(
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                crossAxisCount: constraints.maxWidth >= 720 ? 3 : 2,
                mainAxisExtent: 142,
                crossAxisSpacing: 12,
                mainAxisSpacing: 12,
              ),
              itemCount: metrics.length,
              itemBuilder: (context, index) =>
                  _MetricCard(entity: metrics[index]),
            ),
          ),
        const SizedBox(height: 24),
        _SectionHeader(
          title: 'Szybkie sterowanie',
          subtitle: 'Komendy czasowe realizuje autonomiczny CYD',
        ),
        const SizedBox(height: 12),
        if (controls.isEmpty)
          const _EmptyState(
            icon: Icons.toggle_off_outlined,
            text: 'Brak dostępnych wyjść do sterowania.',
          )
        else
          ...controls.map(
            (entity) => Padding(
              padding: const EdgeInsets.only(bottom: 10),
              child: _EntityTile(controller: controller, entity: entity),
            ),
          ),
      ],
    );
  }
}

final class _DevicesPage extends StatelessWidget {
  const _DevicesPage({required this.controller});

  final HubController controller;

  @override
  Widget build(BuildContext context) {
    return _PageBody(
      children: <Widget>[
        _SectionHeader(
          title: 'Rejestr urządzeń',
          subtitle:
              '${controller.devices.length} urządzeń · ${controller.entities.length} encji',
        ),
        const SizedBox(height: 14),
        if (controller.devices.isEmpty)
          const _EmptyState(
            icon: Icons.radar_outlined,
            text: 'AquaHub nie odebrał jeszcze komunikatu discovery.',
          )
        else
          ...controller.devices.map(
            (device) => Padding(
              padding: const EdgeInsets.only(bottom: 12),
              child: _DeviceCard(
                controller: controller,
                device: device,
                entities: controller.entities
                    .where((entity) => entity.deviceId == device.id)
                    .toList(growable: false),
              ),
            ),
          ),
      ],
    );
  }
}

final class _DeviceCard extends StatelessWidget {
  const _DeviceCard({
    required this.controller,
    required this.device,
    required this.entities,
  });

  final HubController controller;
  final HubDevice device;
  final List<HubEntity> entities;

  @override
  Widget build(BuildContext context) {
    return Card(
      clipBehavior: Clip.antiAlias,
      child: ExpansionTile(
        leading: CircleAvatar(
          backgroundColor: device.online
              ? AquaColors.green.withValues(alpha: 0.16)
              : Theme.of(context).colorScheme.surfaceContainerHighest,
          child: Icon(
            device.online ? Icons.router_rounded : Icons.router_outlined,
            color: device.online
                ? AquaColors.green
                : Theme.of(context).colorScheme.onSurfaceVariant,
          ),
        ),
        title: Text(
          device.name,
          style: const TextStyle(fontWeight: FontWeight.w700),
        ),
        subtitle: Text(
          <String>[
            if (device.area.isNotEmpty) device.area,
            device.online ? 'online' : 'offline',
            '${entities.length} encji',
          ].join(' · '),
        ),
        childrenPadding: const EdgeInsets.fromLTRB(14, 0, 14, 14),
        children: <Widget>[
          if (device.model.isNotEmpty || device.firmwareVersion.isNotEmpty)
            Padding(
              padding: const EdgeInsets.fromLTRB(4, 4, 4, 12),
              child: Align(
                alignment: Alignment.centerLeft,
                child: Text(
                  <String>[
                    if (device.manufacturer.isNotEmpty) device.manufacturer,
                    if (device.model.isNotEmpty) device.model,
                    if (device.firmwareVersion.isNotEmpty)
                      'FW ${device.firmwareVersion}',
                  ].join(' · '),
                  style: TextStyle(
                    color: Theme.of(context).colorScheme.onSurfaceVariant,
                  ),
                ),
              ),
            ),
          ...entities.map(
            (entity) => Padding(
              padding: const EdgeInsets.only(bottom: 8),
              child: _EntityTile(controller: controller, entity: entity),
            ),
          ),
        ],
      ),
    );
  }
}

final class _EntityTile extends StatelessWidget {
  const _EntityTile({required this.controller, required this.entity});

  final HubController controller;
  final HubEntity entity;

  @override
  Widget build(BuildContext context) {
    final commanding = controller.isCommanding(entity.id);
    return Material(
      color: Theme.of(
        context,
      ).colorScheme.surfaceContainerHighest.withValues(alpha: 0.5),
      borderRadius: BorderRadius.circular(16),
      child: ListTile(
        leading: Icon(_iconFor(entity.kind)),
        title: Text(entity.name, maxLines: 1, overflow: TextOverflow.ellipsis),
        subtitle: Text(
          entity.critical
              ? '${entity.formattedState} · encja krytyczna'
              : entity.id,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
        ),
        trailing: commanding
            ? const SizedBox.square(
                dimension: 24,
                child: CircularProgressIndicator(strokeWidth: 2),
              )
            : _control(context),
        onTap: entity.writable && !entity.critical && !commanding
            ? () => _openControl(context)
            : null,
      ),
    );
  }

  Widget _control(BuildContext context) {
    if (entity.critical) {
      return Icon(
        Icons.lock_outline,
        color: Theme.of(context).colorScheme.error,
      );
    }
    if (entity.writable && entity.kind.isBoolean) {
      return Switch(
        value: entity.booleanState ?? false,
        onChanged: (value) => controller.sendCommand(entity, value),
      );
    }
    if (entity.writable) {
      return const Icon(Icons.chevron_right_rounded);
    }
    return ConstrainedBox(
      constraints: const BoxConstraints(maxWidth: 118),
      child: Text(
        entity.formattedState,
        textAlign: TextAlign.end,
        maxLines: 2,
        overflow: TextOverflow.ellipsis,
        style: const TextStyle(fontWeight: FontWeight.w700),
      ),
    );
  }

  Future<void> _openControl(BuildContext context) async {
    Object? value;
    switch (entity.kind) {
      case HubEntityKind.switchEntity:
      case HubEntityKind.light:
        value = !(entity.booleanState ?? false);
      case HubEntityKind.number:
        value = await _showNumberDialog(context, entity);
      case HubEntityKind.select:
        value = await _showSelectDialog(context, entity);
      case HubEntityKind.button:
        value = await _showButtonDialog(context, entity) ? null : _cancelled;
      case HubEntityKind.sensor:
      case HubEntityKind.binarySensor:
        return;
    }
    if (value != _cancelled && context.mounted) {
      await controller.sendCommand(entity, value);
    }
  }
}

const Object _cancelled = Object();

Future<Object?> _showNumberDialog(
  BuildContext context,
  HubEntity entity,
) async {
  final minimum = entity.minimum ?? 0;
  final maximum = entity.maximum ?? 100;
  if (maximum <= minimum || !minimum.isFinite || !maximum.isFinite) {
    return _cancelled;
  }
  var value = (entity.numericState ?? minimum)
      .clamp(minimum, maximum)
      .toDouble();
  final result = await showDialog<double>(
    context: context,
    builder: (context) => StatefulBuilder(
      builder: (context, setDialogState) => AlertDialog(
        title: Text(entity.name),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: <Widget>[
            Text(
              '${value.toStringAsFixed(2)} ${entity.unit}',
              style: Theme.of(context).textTheme.headlineSmall,
            ),
            Slider(
              value: value,
              min: minimum,
              max: maximum,
              divisions: _sliderDivisions(minimum, maximum, entity.step),
              onChanged: (next) => setDialogState(() => value = next),
            ),
          ],
        ),
        actions: <Widget>[
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Anuluj'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, value),
            child: const Text('Ustaw'),
          ),
        ],
      ),
    ),
  );
  return result ?? _cancelled;
}

int? _sliderDivisions(double minimum, double maximum, double? step) {
  if (step == null || step <= 0) return null;
  final divisions = ((maximum - minimum) / step).round();
  return divisions >= 1 && divisions <= 1000 ? divisions : null;
}

Future<Object?> _showSelectDialog(
  BuildContext context,
  HubEntity entity,
) async {
  if (entity.options.isEmpty) return _cancelled;
  final result = await showDialog<String>(
    context: context,
    builder: (context) => SimpleDialog(
      title: Text(entity.name),
      children: entity.options
          .map(
            (option) => ListTile(
              title: Text(option),
              trailing: entity.state == option
                  ? const Icon(Icons.check_circle_rounded)
                  : null,
              onTap: () => Navigator.pop(context, option),
            ),
          )
          .toList(growable: false),
    ),
  );
  return result ?? _cancelled;
}

Future<bool> _showButtonDialog(BuildContext context, HubEntity entity) async =>
    await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: Text(entity.name),
        content: const Text('Czy na pewno wysłać tę komendę do urządzenia?'),
        actions: <Widget>[
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Anuluj'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('Wyślij'),
          ),
        ],
      ),
    ) ??
    false;

final class _HistoryPage extends StatefulWidget {
  const _HistoryPage({required this.controller});

  final HubController controller;

  @override
  State<_HistoryPage> createState() => _HistoryPageState();
}

final class _HistoryPageState extends State<_HistoryPage> {
  String? selectedId;
  Future<List<HubHistoryPoint>>? history;

  @override
  Widget build(BuildContext context) {
    final candidates = widget.controller.entities
        .where((entity) => entity.kind == HubEntityKind.sensor)
        .toList(growable: false);
    if (candidates.isNotEmpty &&
        !candidates.any((entity) => entity.id == selectedId)) {
      selectedId = candidates.first.id;
      history = widget.controller.loadHistory(selectedId!);
    }
    final selected = candidates.cast<HubEntity?>().firstWhere(
      (entity) => entity?.id == selectedId,
      orElse: () => null,
    );
    return _PageBody(
      children: <Widget>[
        _SectionHeader(
          title: 'Historia lokalna',
          subtitle: 'Do 180 ostatnich zmian zapisanych w PSRAM panelu',
        ),
        const SizedBox(height: 16),
        if (candidates.isEmpty)
          const _EmptyState(
            icon: Icons.insights_outlined,
            text: 'Brak encji liczbowych z historią.',
          )
        else ...<Widget>[
          DropdownButtonFormField<String>(
            initialValue: selectedId,
            decoration: const InputDecoration(
              labelText: 'Pomiar',
              prefixIcon: Icon(Icons.timeline_rounded),
            ),
            items: candidates
                .map(
                  (entity) => DropdownMenuItem<String>(
                    value: entity.id,
                    child: Text(entity.name),
                  ),
                )
                .toList(growable: false),
            onChanged: (value) {
              if (value == null) return;
              setState(() {
                selectedId = value;
                history = widget.controller.loadHistory(value);
              });
            },
          ),
          const SizedBox(height: 18),
          FutureBuilder<List<HubHistoryPoint>>(
            future: history,
            builder: (context, snapshot) {
              if (snapshot.connectionState != ConnectionState.done) {
                return const SizedBox(
                  height: 280,
                  child: Center(child: CircularProgressIndicator()),
                );
              }
              if (snapshot.hasError) {
                return _EmptyState(
                  icon: Icons.error_outline,
                  text: snapshot.error.toString(),
                );
              }
              final points = snapshot.data ?? const <HubHistoryPoint>[];
              final numeric = points
                  .where((point) => point.value is num)
                  .toList(growable: false);
              if (numeric.length < 2) {
                return const _EmptyState(
                  icon: Icons.hourglass_empty_rounded,
                  text: 'Za mało zmian, aby narysować wykres.',
                );
              }
              return _HistoryChart(entity: selected!, points: numeric);
            },
          ),
        ],
      ],
    );
  }
}

final class _HistoryChart extends StatelessWidget {
  const _HistoryChart({required this.entity, required this.points});

  final HubEntity entity;
  final List<HubHistoryPoint> points;

  @override
  Widget build(BuildContext context) {
    final values = points
        .map((point) => (point.value! as num).toDouble())
        .toList();
    final minimum = values.reduce(math.min);
    final maximum = values.reduce(math.max);
    final average =
        values.reduce((left, right) => left + right) / values.length;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: <Widget>[
            Wrap(
              alignment: WrapAlignment.spaceBetween,
              runSpacing: 8,
              children: <Widget>[
                Text(
                  entity.name,
                  style: Theme.of(context).textTheme.titleLarge,
                ),
                Text(
                  'min ${minimum.toStringAsFixed(2)} · śr. ${average.toStringAsFixed(2)} · max ${maximum.toStringAsFixed(2)} ${entity.unit}',
                  style: TextStyle(
                    color: Theme.of(context).colorScheme.onSurfaceVariant,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 20),
            SizedBox(
              height: 250,
              child: CustomPaint(
                painter: _HistoryPainter(
                  points: values,
                  lineColor: Theme.of(context).colorScheme.primary,
                  gridColor: Theme.of(context).colorScheme.outlineVariant,
                ),
                child: const SizedBox.expand(),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

final class _HistoryPainter extends CustomPainter {
  const _HistoryPainter({
    required this.points,
    required this.lineColor,
    required this.gridColor,
  });

  final List<double> points;
  final Color lineColor;
  final Color gridColor;

  @override
  void paint(Canvas canvas, Size size) {
    final grid = Paint()
      ..color = gridColor
      ..strokeWidth = 1;
    for (var row = 0; row <= 4; row++) {
      final y = size.height * row / 4;
      canvas.drawLine(Offset(0, y), Offset(size.width, y), grid);
    }
    final minimum = points.reduce(math.min);
    final maximum = points.reduce(math.max);
    final span = maximum == minimum ? 1.0 : maximum - minimum;
    final path = Path();
    for (var index = 0; index < points.length; index++) {
      final x = size.width * index / (points.length - 1);
      final y = size.height - ((points[index] - minimum) / span * size.height);
      if (index == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }
    canvas.drawPath(
      path,
      Paint()
        ..color = lineColor
        ..style = PaintingStyle.stroke
        ..strokeWidth = 3
        ..strokeCap = StrokeCap.round
        ..strokeJoin = StrokeJoin.round,
    );
  }

  @override
  bool shouldRepaint(covariant _HistoryPainter oldDelegate) =>
      oldDelegate.points != points ||
      oldDelegate.lineColor != lineColor ||
      oldDelegate.gridColor != gridColor;
}

final class _SystemPage extends StatelessWidget {
  const _SystemPage({required this.controller});

  final HubController controller;

  @override
  Widget build(BuildContext context) {
    final system = controller.system;
    final credentials = controller.credentials;
    return _PageBody(
      children: <Widget>[
        _SectionHeader(
          title: 'AquaHub ESP32‑P4',
          subtitle: credentials?.baseUri.toString() ?? 'brak aktywnej sesji',
        ),
        const SizedBox(height: 14),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: Column(
              children: <Widget>[
                _InfoRow(
                  'Czas pracy',
                  system == null ? '—' : _duration(system.uptime),
                ),
                _InfoRow(
                  'Wolna pamięć',
                  system == null
                      ? '—'
                      : '${(system.freeHeapBytes / 1024).round()} KiB',
                ),
                _InfoRow(
                  'Urządzenia',
                  system == null
                      ? '—'
                      : '${system.onlineDeviceCount}/${system.deviceCount} online',
                ),
                _InfoRow(
                  'Encje',
                  system == null
                      ? '—'
                      : '${system.entityCount}, w tym ${system.writableEntityCount} sterowalnych',
                ),
                _InfoRow(
                  'Komunikaty',
                  system == null
                      ? '—'
                      : '${system.acceptedMessages} przyjętych · ${system.rejectedMessages} odrzuconych',
                ),
                _InfoRow(
                  'Rewizja rejestru',
                  system?.registryRevision.toString() ?? '—',
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 18),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: <Widget>[
                Row(
                  children: <Widget>[
                    Icon(
                      Icons.verified_user_outlined,
                      color: Theme.of(context).colorScheme.primary,
                    ),
                    const SizedBox(width: 10),
                    Text(
                      'Tożsamość TLS',
                      style: Theme.of(context).textTheme.titleMedium?.copyWith(
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 14),
                SelectableText(
                  credentials == null
                      ? '—'
                      : _groupFingerprint(credentials.tlsFingerprint),
                  style: const TextStyle(fontFamily: 'monospace', height: 1.5),
                ),
                const SizedBox(height: 12),
                const Text(
                  'Aplikacja przypina ten odcisk. Zmiana certyfikatu wymaga ponownego parowania przy panelu.',
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 18),
        Card(
          child: ListTile(
            leading: const Icon(Icons.vpn_lock_outlined),
            title: const Text('Dostęp zdalny przez VPN'),
            subtitle: const Text(
              'Nie wystawiaj portów 8443 ani 8883 bezpośrednio do Internetu.',
            ),
          ),
        ),
        const SizedBox(height: 24),
        OutlinedButton.icon(
          onPressed: () => _confirmDisconnect(context),
          icon: const Icon(Icons.link_off_rounded),
          label: const Text('Usuń parowanie z telefonu'),
        ),
      ],
    );
  }

  Future<void> _confirmDisconnect(BuildContext context) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Usunąć parowanie?'),
        content: const Text(
          'Token i przypięty odcisk certyfikatu zostaną usunięte z telefonu.',
        ),
        actions: <Widget>[
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Anuluj'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('Usuń'),
          ),
        ],
      ),
    );
    if (confirmed == true) await controller.disconnect();
  }
}

final class _HealthHero extends StatelessWidget {
  const _HealthHero({
    required this.online,
    required this.total,
    required this.criticalCount,
  });

  final int online;
  final int total;
  final int criticalCount;

  @override
  Widget build(BuildContext context) {
    final healthy = criticalCount == 0 && total > 0 && online == total;
    final color = healthy
        ? AquaColors.green
        : criticalCount > 0
        ? AquaColors.red
        : AquaColors.amber;
    return Container(
      padding: const EdgeInsets.all(24),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          colors: <Color>[
            color.withValues(alpha: 0.22),
            color.withValues(alpha: 0.06),
          ],
        ),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: color.withValues(alpha: 0.35)),
      ),
      child: Row(
        children: <Widget>[
          CircleAvatar(
            radius: 28,
            backgroundColor: color.withValues(alpha: 0.18),
            child: Icon(
              healthy
                  ? Icons.check_circle_outline
                  : Icons.monitor_heart_outlined,
              color: color,
              size: 32,
            ),
          ),
          const SizedBox(width: 18),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                Text(
                  healthy
                      ? 'System działa prawidłowo'
                      : criticalCount > 0
                      ? 'Wykryto alarm'
                      : 'Część urządzeń jest offline',
                  style: Theme.of(
                    context,
                  ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
                ),
                const SizedBox(height: 5),
                Text(
                  '$online z $total urządzeń online · $criticalCount aktywnych alarmów',
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

final class _MetricCard extends StatelessWidget {
  const _MetricCard({required this.entity});

  final HubEntity entity;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(17),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: <Widget>[
            Icon(
              _iconFor(entity.kind),
              color: Theme.of(context).colorScheme.primary,
            ),
            const Spacer(),
            Text(
              entity.formattedState,
              style: Theme.of(
                context,
              ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
            ),
            const SizedBox(height: 4),
            Text(entity.name, maxLines: 1, overflow: TextOverflow.ellipsis),
          ],
        ),
      ),
    );
  }
}

final class _AlertTile extends StatelessWidget {
  const _AlertTile({required this.entity});

  final HubEntity entity;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Theme.of(context).colorScheme.errorContainer,
      borderRadius: BorderRadius.circular(16),
      child: ListTile(
        leading: Icon(
          Icons.warning_amber_rounded,
          color: Theme.of(context).colorScheme.onErrorContainer,
        ),
        title: Text(
          entity.name,
          style: const TextStyle(fontWeight: FontWeight.w700),
        ),
        subtitle: Text(entity.id),
        trailing: const Icon(Icons.lock_outline),
      ),
    );
  }
}

final class _PageBody extends StatelessWidget {
  const _PageBody({required this.children});

  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.fromLTRB(20, 12, 20, 34),
      children: <Widget>[
        Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 1040),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: children,
            ),
          ),
        ),
      ],
    );
  }
}

final class _SectionHeader extends StatelessWidget {
  const _SectionHeader({required this.title, required this.subtitle});

  final String title;
  final String subtitle;

  @override
  Widget build(BuildContext context) {
    return Wrap(
      alignment: WrapAlignment.spaceBetween,
      crossAxisAlignment: WrapCrossAlignment.end,
      runSpacing: 4,
      children: <Widget>[
        Text(
          title,
          style: Theme.of(
            context,
          ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
        ),
        Text(
          subtitle,
          style: TextStyle(
            color: Theme.of(context).colorScheme.onSurfaceVariant,
          ),
        ),
      ],
    );
  }
}

final class _EmptyState extends StatelessWidget {
  const _EmptyState({required this.icon, required this.text});

  final IconData icon;
  final String text;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(28),
        child: Column(
          children: <Widget>[
            Icon(
              icon,
              size: 38,
              color: Theme.of(context).colorScheme.onSurfaceVariant,
            ),
            const SizedBox(height: 12),
            Text(text, textAlign: TextAlign.center),
          ],
        ),
      ),
    );
  }
}

final class _InfoRow extends StatelessWidget {
  const _InfoRow(this.label, this.value);

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          Expanded(
            child: Text(
              label,
              style: TextStyle(
                color: Theme.of(context).colorScheme.onSurfaceVariant,
              ),
            ),
          ),
          const SizedBox(width: 14),
          Flexible(
            child: Text(
              value,
              textAlign: TextAlign.end,
              style: const TextStyle(fontWeight: FontWeight.w700),
            ),
          ),
        ],
      ),
    );
  }
}

final class _SmallHubMark extends StatelessWidget {
  const _SmallHubMark();

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 44,
      height: 44,
      decoration: BoxDecoration(
        gradient: const LinearGradient(
          colors: <Color>[AquaColors.cyan, AquaColors.blue],
        ),
        borderRadius: BorderRadius.circular(14),
      ),
      child: const Icon(Icons.hub_rounded, color: Colors.white),
    );
  }
}

IconData _iconFor(HubEntityKind kind) => switch (kind) {
  HubEntityKind.sensor => Icons.sensors_outlined,
  HubEntityKind.binarySensor => Icons.radio_button_checked,
  HubEntityKind.switchEntity => Icons.toggle_on_outlined,
  HubEntityKind.number => Icons.tune_rounded,
  HubEntityKind.select => Icons.list_alt_outlined,
  HubEntityKind.button => Icons.smart_button_outlined,
  HubEntityKind.light => Icons.lightbulb_outline_rounded,
};

String _duration(Duration value) {
  final days = value.inDays;
  final hours = value.inHours.remainder(24);
  final minutes = value.inMinutes.remainder(60);
  return days > 0 ? '$days d $hours h $minutes min' : '$hours h $minutes min';
}

String _groupFingerprint(String value) {
  final groups = <String>[];
  for (var index = 0; index < value.length; index += 4) {
    groups.add(value.substring(index, index + 4));
  }
  return groups.join(' ');
}

final class _Destination {
  const _Destination(this.label, this.icon, this.selectedIcon);

  final String label;
  final IconData icon;
  final IconData selectedIcon;
}
