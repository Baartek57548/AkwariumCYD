import 'dart:math' as math;

import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:flutter/material.dart';
import 'package:home_entities/home_entities.dart';

import 'controller.dart';
import 'strings.dart';

IconData iconForEntity(HomeEntityType type) => switch (type) {
  HomeEntityType.light => Icons.lightbulb_rounded,
  HomeEntityType.switchEntity => Icons.toggle_on_rounded,
  HomeEntityType.sensor => Icons.sensors_rounded,
  HomeEntityType.binarySensor => Icons.radar_rounded,
  HomeEntityType.climate => Icons.thermostat_rounded,
  HomeEntityType.cover => Icons.blinds_rounded,
  HomeEntityType.lock => Icons.lock_rounded,
  HomeEntityType.alarmControlPanel => Icons.shield_rounded,
  HomeEntityType.camera => Icons.videocam_rounded,
  HomeEntityType.mediaPlayer => Icons.speaker_rounded,
  HomeEntityType.fan => Icons.air_rounded,
  HomeEntityType.vacuum => Icons.cleaning_services_rounded,
  HomeEntityType.weather => Icons.cloud_rounded,
  HomeEntityType.person => Icons.person_rounded,
  HomeEntityType.deviceTracker => Icons.location_on_rounded,
  HomeEntityType.scene => Icons.auto_awesome_rounded,
  HomeEntityType.script => Icons.play_circle_rounded,
  HomeEntityType.automation => Icons.account_tree_rounded,
  HomeEntityType.button ||
  HomeEntityType.inputButton => Icons.smart_button_rounded,
  HomeEntityType.number || HomeEntityType.inputNumber => Icons.pin_rounded,
  HomeEntityType.select || HomeEntityType.inputSelect => Icons.list_alt_rounded,
  HomeEntityType.text || HomeEntityType.inputText => Icons.text_fields_rounded,
  HomeEntityType.update => Icons.system_update_rounded,
  HomeEntityType.unknown => Icons.extension_rounded,
};

final class SourceStatusBanner extends StatelessWidget {
  const SourceStatusBanner({
    required this.snapshot,
    required this.failureKey,
    required this.onDismiss,
    super.key,
  });

  final HomeSnapshot snapshot;
  final String? failureKey;
  final VoidCallback onDismiss;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final scheme = Theme.of(context).colorScheme;
    final critical = snapshot.isOffline || failureKey != null;
    final warning = snapshot.isStale || snapshot.isPartial;
    if (!critical && !warning) return const SizedBox.shrink();
    final background = critical
        ? scheme.errorContainer
        : scheme.tertiaryContainer;
    final foreground = critical
        ? scheme.onErrorContainer
        : scheme.onTertiaryContainer;
    final keyName =
        failureKey ??
        (snapshot.isOffline
            ? 'errorOffline'
            : snapshot.isPartial
            ? 'partial'
            : 'stale');
    return Material(
      color: background,
      child: SafeArea(
        bottom: false,
        child: Padding(
          padding: const EdgeInsets.symmetric(
            horizontal: ProductSpacing.md,
            vertical: ProductSpacing.sm,
          ),
          child: Row(
            children: <Widget>[
              Icon(
                critical ? Icons.cloud_off_rounded : Icons.schedule_rounded,
                color: foreground,
              ),
              const SizedBox(width: ProductSpacing.sm),
              Expanded(
                child: Text(
                  strings.t(keyName),
                  style: TextStyle(color: foreground),
                ),
              ),
              if (failureKey != null)
                IconButton(
                  tooltip: strings.t('dismiss'),
                  onPressed: onDismiss,
                  color: foreground,
                  icon: const Icon(Icons.close_rounded),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

final class EntityCard extends StatelessWidget {
  const EntityCard({
    required this.entity,
    required this.controller,
    this.compact = false,
    super.key,
  });

  final HomeEntity entity;
  final HomeControlController controller;
  final bool compact;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final scheme = Theme.of(context).colorScheme;
    final pending = controller.isPending(entity.id);
    final enabled = entity.available && !controller.snapshot!.isOffline;
    return Semantics(
      container: true,
      label: '${entity.name}, ${strings.entityState(entity)}',
      child: Card(
        clipBehavior: Clip.antiAlias,
        child: InkWell(
          onTap: () => showEntityDetails(context, entity, controller),
          child: Padding(
            padding: EdgeInsets.all(
              compact ? ProductSpacing.sm : ProductSpacing.md,
            ),
            child: Row(
              children: <Widget>[
                Container(
                  width: 46,
                  height: 46,
                  decoration: BoxDecoration(
                    color: entity.booleanValue == true
                        ? scheme.primaryContainer
                        : scheme.surfaceContainerHighest,
                    borderRadius: BorderRadius.circular(15),
                  ),
                  child: Icon(
                    iconForEntity(entity.type),
                    color: entity.booleanValue == true
                        ? scheme.onPrimaryContainer
                        : scheme.onSurfaceVariant,
                  ),
                ),
                const SizedBox(width: ProductSpacing.sm),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: <Widget>[
                      Text(
                        entity.name,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: Theme.of(context).textTheme.titleMedium
                            ?.copyWith(fontWeight: FontWeight.w700),
                      ),
                      const SizedBox(height: 2),
                      Text(
                        pending
                            ? strings.t('commandPending')
                            : strings.entityState(entity),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: Theme.of(context).textTheme.bodySmall?.copyWith(
                          color: enabled
                              ? scheme.onSurfaceVariant
                              : scheme.error,
                        ),
                      ),
                    ],
                  ),
                ),
                const SizedBox(width: ProductSpacing.xs),
                if (pending)
                  const SizedBox(
                    width: 24,
                    height: 24,
                    child: CircularProgressIndicator(strokeWidth: 2.5),
                  )
                else
                  _QuickControl(
                    entity: entity,
                    enabled: enabled,
                    onValue: (value) => requestEntityCommand(
                      context,
                      entity,
                      value,
                      controller,
                    ),
                  ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

final class _QuickControl extends StatelessWidget {
  const _QuickControl({
    required this.entity,
    required this.enabled,
    required this.onValue,
  });

  final HomeEntity entity;
  final bool enabled;
  final ValueChanged<Object?> onValue;

  @override
  Widget build(BuildContext context) {
    if (!entity.writable || entity.type == HomeEntityType.unknown) {
      return const Icon(Icons.chevron_right_rounded);
    }
    if (entity.type.supportsToggle) {
      return Switch(
        value: entity.booleanValue == true,
        onChanged: enabled ? onValue : null,
      );
    }
    if (entity.type.supportsPress) {
      return IconButton.filledTonal(
        tooltip: HomeControlStrings.of(context).t('run'),
        onPressed: enabled ? () => onValue(true) : null,
        icon: const Icon(Icons.play_arrow_rounded),
      );
    }
    return const Icon(Icons.chevron_right_rounded);
  }
}

Future<bool> requestEntityCommand(
  BuildContext context,
  HomeEntity entity,
  Object? value,
  HomeControlController controller,
) async {
  if (entity.risk != HomeCommandRisk.routine) {
    final strings = HomeControlStrings.of(context);
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        icon: Icon(
          entity.risk == HomeCommandRisk.critical
              ? Icons.warning_amber_rounded
              : Icons.info_outline_rounded,
        ),
        title: Text(strings.t('confirmTitle')),
        content: Text(
          strings.t(
            entity.risk == HomeCommandRisk.critical
                ? 'confirmCritical'
                : 'confirmConsequential',
          ),
        ),
        actions: <Widget>[
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: Text(strings.t('cancel')),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: Text(strings.t('confirm')),
          ),
        ],
      ),
    );
    if (confirmed != true) return false;
  }
  return controller.sendCommand(entity, value);
}

Future<void> showEntityDetails(
  BuildContext context,
  HomeEntity entity,
  HomeControlController controller,
) async {
  await showModalBottomSheet<void>(
    context: context,
    isScrollControlled: true,
    showDragHandle: true,
    builder: (context) =>
        _EntityDetails(entityId: entity.id, controller: controller),
  );
}

final class _EntityDetails extends StatefulWidget {
  const _EntityDetails({required this.entityId, required this.controller});

  final SourceScopedId entityId;
  final HomeControlController controller;

  @override
  State<_EntityDetails> createState() => _EntityDetailsState();
}

final class _EntityDetailsState extends State<_EntityDetails> {
  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: widget.controller,
      builder: (context, _) {
        final strings = HomeControlStrings.of(context);
        final entity = widget.controller.snapshot?.entity(widget.entityId);
        if (entity == null) {
          return SizedBox(
            height: 260,
            child: Center(child: Text(strings.t('removed'))),
          );
        }
        final favorite = widget.controller.dashboard.favorites.contains(
          entity.id.value,
        );
        return SafeArea(
          child: ConstrainedBox(
            constraints: BoxConstraints(
              maxHeight: MediaQuery.sizeOf(context).height * 0.88,
            ),
            child: SingleChildScrollView(
              padding: const EdgeInsets.fromLTRB(24, 4, 24, 32),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: <Widget>[
                  Row(
                    children: <Widget>[
                      Icon(iconForEntity(entity.type), size: 34),
                      const SizedBox(width: ProductSpacing.sm),
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: <Widget>[
                            Text(
                              entity.name,
                              style: Theme.of(context).textTheme.headlineSmall
                                  ?.copyWith(fontWeight: FontWeight.w800),
                            ),
                            Text(strings.entityType(entity.type)),
                          ],
                        ),
                      ),
                      IconButton(
                        tooltip: strings.t(
                          favorite ? 'removeFavorite' : 'favorite',
                        ),
                        onPressed: () =>
                            widget.controller.toggleFavorite(entity),
                        icon: Icon(
                          favorite
                              ? Icons.star_rounded
                              : Icons.star_border_rounded,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: ProductSpacing.lg),
                  Text(
                    strings.entityState(entity),
                    style: Theme.of(context).textTheme.displaySmall?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  const SizedBox(height: ProductSpacing.md),
                  if (entity.type == HomeEntityType.unknown)
                    _Hint(text: strings.t('unknownEntityHint')),
                  if (entity.writable) ...<Widget>[
                    const SizedBox(height: ProductSpacing.md),
                    _DetailedControl(
                      entity: entity,
                      onValue: (value) => requestEntityCommand(
                        context,
                        entity,
                        value,
                        widget.controller,
                      ),
                    ),
                  ],
                  if (entity.type.supportsHistory) ...<Widget>[
                    const SizedBox(height: ProductSpacing.xl),
                    Row(
                      children: <Widget>[
                        Expanded(
                          child: Text(
                            strings.t('history'),
                            style: Theme.of(context).textTheme.titleLarge
                                ?.copyWith(fontWeight: FontWeight.w800),
                          ),
                        ),
                        TextButton(
                          onPressed: widget.controller.historyLoading
                              ? null
                              : () => widget.controller.loadHistory(
                                  entity,
                                  const Duration(days: 1),
                                ),
                          child: Text(strings.t('history24h')),
                        ),
                        TextButton(
                          onPressed: widget.controller.historyLoading
                              ? null
                              : () => widget.controller.loadHistory(
                                  entity,
                                  const Duration(days: 7),
                                ),
                          child: Text(strings.t('history7d')),
                        ),
                      ],
                    ),
                    const SizedBox(height: ProductSpacing.sm),
                    if (widget.controller.historyLoading)
                      const LinearProgressIndicator()
                    else if (widget.controller.historyEntityId == entity.id &&
                        widget.controller.history.isNotEmpty)
                      SizedBox(
                        height: 180,
                        child: CustomPaint(
                          painter: _HistoryPainter(
                            points: widget.controller.history,
                            color: Theme.of(context).colorScheme.primary,
                            gridColor: Theme.of(
                              context,
                            ).colorScheme.outlineVariant,
                          ),
                        ),
                      )
                    else
                      _Hint(text: strings.t('historyEmpty')),
                  ],
                  const SizedBox(height: ProductSpacing.lg),
                  Text(
                    entity.id.value,
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                    ),
                  ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}

final class _DetailedControl extends StatefulWidget {
  const _DetailedControl({required this.entity, required this.onValue});

  final HomeEntity entity;
  final ValueChanged<Object?> onValue;

  @override
  State<_DetailedControl> createState() => _DetailedControlState();
}

final class _DetailedControlState extends State<_DetailedControl> {
  double? _draftNumber;

  HomeEntity get entity => widget.entity;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    if (entity.type.supportsToggle) {
      final value = entity.booleanValue == true;
      return SegmentedButton<bool>(
        segments: <ButtonSegment<bool>>[
          ButtonSegment<bool>(
            value: false,
            label: Text(strings.t('turnOff')),
            icon: const Icon(Icons.power_settings_new_rounded),
          ),
          ButtonSegment<bool>(
            value: true,
            label: Text(strings.t('turnOn')),
            icon: const Icon(Icons.power_rounded),
          ),
        ],
        selected: <bool>{value},
        onSelectionChanged: (selection) => widget.onValue(selection.first),
      );
    }
    if (entity.type.supportsPress) {
      return FilledButton.icon(
        onPressed: () => widget.onValue(true),
        icon: const Icon(Icons.play_arrow_rounded),
        label: Text(strings.t('run')),
      );
    }
    if (<HomeEntityType>{
      HomeEntityType.select,
      HomeEntityType.inputSelect,
    }.contains(entity.type)) {
      return DropdownButtonFormField<String>(
        initialValue: entity.state?.toString(),
        decoration: InputDecoration(labelText: strings.t('selectOption')),
        items: <DropdownMenuItem<String>>[
          for (final option in entity.constraints.options)
            DropdownMenuItem<String>(value: option, child: Text(option)),
        ],
        onChanged: (value) {
          if (value != null) widget.onValue(value);
        },
      );
    }
    if (<HomeEntityType>{
      HomeEntityType.number,
      HomeEntityType.inputNumber,
    }.contains(entity.type)) {
      final min = entity.constraints.minimum ?? 0;
      final max = entity.constraints.maximum ?? 100;
      final current = (_draftNumber ?? entity.numericValue ?? min).clamp(
        min,
        max,
      );
      return Column(
        children: <Widget>[
          Slider(
            value: current,
            min: min,
            max: max <= min ? min + 1 : max,
            divisions: _divisions(entity, min, max),
            label: current.toStringAsFixed(1),
            onChanged: (value) => setState(() => _draftNumber = value),
            onChangeEnd: (value) {
              widget.onValue(value);
              if (mounted) setState(() => _draftNumber = null);
            },
          ),
          Text(strings.withValue('setValue', current.toStringAsFixed(1))),
        ],
      );
    }
    return _Hint(text: strings.t('errorUnsupported'));
  }

  int? _divisions(HomeEntity entity, double min, double max) {
    final step = entity.constraints.step;
    if (step == null || step <= 0 || max <= min) return null;
    return math.max(1, ((max - min) / step).round()).clamp(1, 1000).toInt();
  }
}

final class _Hint extends StatelessWidget {
  const _Hint({required this.text});

  final String text;

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.all(ProductSpacing.md),
    decoration: BoxDecoration(
      color: Theme.of(context).colorScheme.surfaceContainerHigh,
      borderRadius: BorderRadius.circular(ProductRadius.card),
    ),
    child: Text(text),
  );
}

final class _HistoryPainter extends CustomPainter {
  const _HistoryPainter({
    required this.points,
    required this.color,
    required this.gridColor,
  });

  final List<HistoryPoint> points;
  final Color color;
  final Color gridColor;

  @override
  void paint(Canvas canvas, Size size) {
    final values = points
        .map((point) {
          final value = point.value;
          if (value is num) return value.toDouble();
          return double.tryParse(value?.toString() ?? '');
        })
        .whereType<double>()
        .where((value) => value.isFinite)
        .toList(growable: false);
    if (values.length < 2) return;
    final minimum = values.reduce(math.min);
    final maximum = values.reduce(math.max);
    final range = maximum == minimum ? 1.0 : maximum - minimum;
    final grid = Paint()
      ..color = gridColor
      ..strokeWidth = 1;
    for (var row = 0; row <= 4; row++) {
      final y = size.height * row / 4;
      canvas.drawLine(Offset(0, y), Offset(size.width, y), grid);
    }
    final path = Path();
    for (var index = 0; index < values.length; index++) {
      final x = size.width * index / (values.length - 1);
      final y = size.height - ((values[index] - minimum) / range * size.height);
      if (index == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }
    canvas.drawPath(
      path,
      Paint()
        ..color = color
        ..style = PaintingStyle.stroke
        ..strokeWidth = 3
        ..strokeCap = StrokeCap.round
        ..strokeJoin = StrokeJoin.round,
    );
  }

  @override
  bool shouldRepaint(covariant _HistoryPainter oldDelegate) =>
      oldDelegate.points != points ||
      oldDelegate.color != color ||
      oldDelegate.gridColor != gridColor;
}
