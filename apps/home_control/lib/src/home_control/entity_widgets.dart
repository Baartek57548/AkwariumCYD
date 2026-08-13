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
    return Semantics(
      liveRegion: true,
      container: true,
      child: Material(
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
  String? _commandFailureKey;

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
        final enabled =
            entity.available &&
            entity.writable &&
            !widget.controller.snapshot!.isOffline &&
            !widget.controller.isPending(entity.id);
        final historyEnabled =
            entity.available && !widget.controller.snapshot!.isOffline;
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
                  if (entity.attributes.isNotEmpty) ...<Widget>[
                    const SizedBox(height: ProductSpacing.md),
                    _EntityAttributes(entity: entity),
                  ],
                  if (entity.writable) ...<Widget>[
                    const SizedBox(height: ProductSpacing.md),
                    _DetailedControl(
                      entity: entity,
                      enabled: enabled,
                      onValue: (value) async {
                        final succeeded = await requestEntityCommand(
                          context,
                          entity,
                          value,
                          widget.controller,
                        );
                        if (!mounted) return;
                        setState(() {
                          _commandFailureKey = succeeded
                              ? null
                              : widget.controller.failure?.messageKey ??
                                    'errorUnknown';
                        });
                      },
                    ),
                    if (_commandFailureKey case final key?) ...<Widget>[
                      const SizedBox(height: ProductSpacing.sm),
                      Semantics(
                        liveRegion: true,
                        child: Material(
                          color: Theme.of(context).colorScheme.errorContainer,
                          borderRadius: BorderRadius.circular(
                            ProductRadius.card,
                          ),
                          child: Padding(
                            padding: const EdgeInsets.all(ProductSpacing.sm),
                            child: Row(
                              children: <Widget>[
                                Icon(
                                  Icons.error_outline_rounded,
                                  color: Theme.of(
                                    context,
                                  ).colorScheme.onErrorContainer,
                                ),
                                const SizedBox(width: ProductSpacing.sm),
                                Expanded(child: Text(strings.t(key))),
                                IconButton(
                                  tooltip: strings.t('dismiss'),
                                  onPressed: () =>
                                      setState(() => _commandFailureKey = null),
                                  icon: const Icon(Icons.close_rounded),
                                ),
                              ],
                            ),
                          ),
                        ),
                      ),
                    ],
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
                      ],
                    ),
                    Align(
                      alignment: Alignment.centerRight,
                      child: Wrap(
                        spacing: ProductSpacing.xs,
                        children: <Widget>[
                          TextButton(
                            onPressed:
                                widget.controller.historyLoading ||
                                    !historyEnabled
                                ? null
                                : () => widget.controller.loadHistory(
                                    entity,
                                    const Duration(days: 1),
                                  ),
                            child: Text(strings.t('history24h')),
                          ),
                          TextButton(
                            onPressed:
                                widget.controller.historyLoading ||
                                    !historyEnabled
                                ? null
                                : () => widget.controller.loadHistory(
                                    entity,
                                    const Duration(days: 7),
                                  ),
                            child: Text(strings.t('history7d')),
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(height: ProductSpacing.sm),
                    if (widget.controller.historyLoading)
                      const LinearProgressIndicator()
                    else if (widget.controller.historyEntityId == entity.id &&
                        widget.controller.history.isNotEmpty)
                      _HistoryChart(
                        points: widget.controller.history,
                        unit: entity.unit,
                      )
                    else if (widget.controller.historyEntityId != entity.id)
                      _Hint(text: strings.t('historyPrompt'))
                    else
                      _Hint(text: strings.t('historyEmpty')),
                  ],
                  const SizedBox(height: ProductSpacing.lg),
                  Text(
                    strings.withValue(
                      'entitySource',
                      widget.controller.snapshot?.sourceName ??
                          entity.id.sourceId,
                    ),
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                    ),
                  ),
                  Text(
                    strings.withValue(
                      'entityUpdated',
                      strings.relativeTime(entity.updatedAt),
                    ),
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                    ),
                  ),
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
  const _DetailedControl({
    required this.entity,
    required this.enabled,
    required this.onValue,
  });

  final HomeEntity entity;
  final bool enabled;
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
    if (entity.type == HomeEntityType.cover) {
      return SegmentedButton<bool>(
        segments: <ButtonSegment<bool>>[
          ButtonSegment<bool>(
            value: false,
            label: Text(strings.t('closeCover')),
            icon: const Icon(Icons.vertical_align_bottom_rounded),
          ),
          ButtonSegment<bool>(
            value: true,
            label: Text(strings.t('openCover')),
            icon: const Icon(Icons.vertical_align_top_rounded),
          ),
        ],
        selected: <bool>{entity.booleanValue == true},
        onSelectionChanged: widget.enabled
            ? (selection) => widget.onValue(selection.first)
            : null,
      );
    }
    if (entity.type == HomeEntityType.lock) {
      return SegmentedButton<bool>(
        segments: <ButtonSegment<bool>>[
          ButtonSegment<bool>(
            value: false,
            label: Text(strings.t('lockAction')),
            icon: const Icon(Icons.lock_rounded),
          ),
          ButtonSegment<bool>(
            value: true,
            label: Text(strings.t('unlockAction')),
            icon: const Icon(Icons.lock_open_rounded),
          ),
        ],
        selected: <bool>{entity.booleanValue == true},
        onSelectionChanged: widget.enabled
            ? (selection) => widget.onValue(selection.first)
            : null,
      );
    }
    if (entity.type == HomeEntityType.alarmControlPanel) {
      const modes = <String>[
        'disarmed',
        'armed_home',
        'armed_away',
        'armed_night',
      ];
      final current = modes.contains(entity.state)
          ? entity.state as String
          : null;
      return DropdownButtonFormField<String>(
        key: ValueKey<String>(
          '${entity.id.value}:${entity.state}:${widget.enabled}',
        ),
        initialValue: current,
        isExpanded: true,
        decoration: InputDecoration(labelText: strings.t('alarmMode')),
        items: <DropdownMenuItem<String>>[
          for (final mode in modes)
            DropdownMenuItem<String>(
              value: mode,
              child: Text(
                strings.t('alarm_$mode'),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
            ),
        ],
        onChanged: widget.enabled
            ? (value) {
                if (value != null) widget.onValue(value);
              }
            : null,
      );
    }
    if (entity.type == HomeEntityType.vacuum ||
        entity.type == HomeEntityType.mediaPlayer) {
      return SegmentedButton<bool>(
        segments: <ButtonSegment<bool>>[
          ButtonSegment<bool>(
            value: false,
            label: Text(
              strings.t(
                entity.type == HomeEntityType.vacuum
                    ? 'returnToBase'
                    : 'turnOff',
              ),
            ),
            icon: const Icon(Icons.stop_rounded),
          ),
          ButtonSegment<bool>(
            value: true,
            label: Text(
              strings.t(
                entity.type == HomeEntityType.vacuum ? 'start' : 'turnOn',
              ),
            ),
            icon: const Icon(Icons.play_arrow_rounded),
          ),
        ],
        selected: <bool>{entity.booleanValue == true},
        onSelectionChanged: widget.enabled
            ? (selection) => widget.onValue(selection.first)
            : null,
      );
    }
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
        onSelectionChanged: widget.enabled
            ? (selection) => widget.onValue(selection.first)
            : null,
      );
    }
    if (entity.type.supportsPress) {
      return FilledButton.icon(
        onPressed: widget.enabled ? () => widget.onValue(true) : null,
        icon: const Icon(Icons.play_arrow_rounded),
        label: Text(strings.t('run')),
      );
    }
    if (<HomeEntityType>{
      HomeEntityType.select,
      HomeEntityType.inputSelect,
    }.contains(entity.type)) {
      final options = entity.constraints.options;
      final state = entity.state?.toString();
      return DropdownButtonFormField<String>(
        key: ValueKey<String>(
          '${entity.id.value}:${entity.state}:${widget.enabled}',
        ),
        initialValue: options.contains(state) ? state : null,
        isExpanded: true,
        decoration: InputDecoration(labelText: strings.t('selectOption')),
        items: <DropdownMenuItem<String>>[
          for (final option in options)
            DropdownMenuItem<String>(
              value: option,
              child: Text(option, maxLines: 1, overflow: TextOverflow.ellipsis),
            ),
        ],
        onChanged: widget.enabled
            ? (value) {
                if (value != null) widget.onValue(value);
              }
            : null,
      );
    }
    if (<HomeEntityType>{
      HomeEntityType.number,
      HomeEntityType.inputNumber,
      HomeEntityType.climate,
    }.contains(entity.type)) {
      final min = entity.constraints.minimum ?? 0;
      final max = entity.constraints.maximum ?? 100;
      final attributeTemperature = entity.attributes['temperature'];
      final temperature = attributeTemperature is num
          ? attributeTemperature.toDouble()
          : double.tryParse(attributeTemperature?.toString() ?? '');
      final current =
          (_draftNumber ?? entity.numericValue ?? temperature ?? min).clamp(
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
            onChanged: widget.enabled
                ? (value) => setState(() => _draftNumber = value)
                : null,
            onChangeEnd: widget.enabled
                ? (value) {
                    widget.onValue(value);
                    if (mounted) setState(() => _draftNumber = null);
                  }
                : null,
          ),
          Text(strings.withValue('setValue', current.toStringAsFixed(1))),
        ],
      );
    }
    if (<HomeEntityType>{
      HomeEntityType.text,
      HomeEntityType.inputText,
    }.contains(entity.type)) {
      return TextFormField(
        enabled: widget.enabled,
        initialValue: entity.state?.toString() ?? '',
        maxLength: 255,
        textInputAction: TextInputAction.done,
        decoration: InputDecoration(
          labelText: strings.t('textValue'),
          suffixIcon: const Icon(Icons.send_rounded),
        ),
        onFieldSubmitted: widget.onValue,
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

final class _EntityAttributes extends StatelessWidget {
  const _EntityAttributes({required this.entity});

  final HomeEntity entity;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final entries = entity.attributes.entries
        .where((entry) => !_hiddenAttributes.contains(entry.key))
        .take(12)
        .toList(growable: false);
    if (entries.isEmpty) return const SizedBox.shrink();
    return ExpansionTile(
      tilePadding: EdgeInsets.zero,
      title: Text(strings.t('attributes')),
      children: <Widget>[
        for (final entry in entries)
          ListTile(
            dense: true,
            contentPadding: EdgeInsets.zero,
            title: Text(entry.key.replaceAll('_', ' ')),
            trailing: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 190),
              child: Text(
                _safeAttributeText(entry.value),
                maxLines: 3,
                overflow: TextOverflow.ellipsis,
                textAlign: TextAlign.end,
              ),
            ),
          ),
      ],
    );
  }

  static const Set<String> _hiddenAttributes = <String>{
    'access_token',
    'api_key',
    'password',
    'token',
  };

  static String _safeAttributeText(Object? value) {
    final text = value?.toString() ?? '—';
    return text.length <= 240 ? text : '${text.substring(0, 237)}…';
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
    final samples =
        points
            .map((point) {
              final value = point.value;
              final parsed = value is num
                  ? value.toDouble()
                  : double.tryParse(value?.toString() ?? '');
              return parsed != null && parsed.isFinite
                  ? (time: point.time, value: parsed)
                  : null;
            })
            .whereType<({DateTime time, double value})>()
            .toList(growable: false)
          ..sort((a, b) => a.time.compareTo(b.time));
    if (samples.length < 2) return;
    final values = samples
        .map((sample) => sample.value)
        .toList(growable: false);
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
    final firstTime = samples.first.time.millisecondsSinceEpoch;
    final timeRange = math.max(
      1,
      samples.last.time.millisecondsSinceEpoch - firstTime,
    );
    for (var index = 0; index < samples.length; index++) {
      final sample = samples[index];
      final x =
          size.width *
          (sample.time.millisecondsSinceEpoch - firstTime) /
          timeRange;
      final y = size.height - ((sample.value - minimum) / range * size.height);
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

final class _HistoryChart extends StatelessWidget {
  const _HistoryChart({required this.points, required this.unit});

  final List<HistoryPoint> points;
  final String unit;

  @override
  Widget build(BuildContext context) {
    final values = points
        .map((point) => point.value)
        .whereType<num>()
        .map((value) => value.toDouble())
        .where((value) => value.isFinite)
        .toList(growable: false);
    final minimum = values.isEmpty ? 0 : values.reduce(math.min);
    final maximum = values.isEmpty ? 0 : values.reduce(math.max);
    final suffix = unit.isEmpty ? '' : ' $unit';
    final label = HomeControlStrings.of(context)
        .withValues('historySummary', <String, Object>{
          'minimum': '${minimum.toStringAsFixed(1)}$suffix',
          'maximum': '${maximum.toStringAsFixed(1)}$suffix',
          'samples': values.length,
        });
    return Semantics(
      image: true,
      label: label,
      child: SizedBox(
        height: 180,
        child: CustomPaint(
          painter: _HistoryPainter(
            points: points,
            color: Theme.of(context).colorScheme.primary,
            gridColor: Theme.of(context).colorScheme.outlineVariant,
          ),
        ),
      ),
    );
  }
}
