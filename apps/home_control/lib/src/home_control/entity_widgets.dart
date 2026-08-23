import 'dart:async';
import 'dart:math' as math;

import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:home_entities/home_entities.dart';

import '../design/components.dart';
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
    final status = failureKey != null
        ? _SourceBannerStatus.failure
        : snapshot.isOffline
        ? _SourceBannerStatus.offline
        : snapshot.isPartial
        ? _SourceBannerStatus.partial
        : _SourceBannerStatus.stale;
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
    final message = strings.t(keyName);
    return Material(
      color: background,
      child: SafeArea(
        bottom: false,
        child: DecoratedBox(
          decoration: BoxDecoration(
            border: Border(
              left: Directionality.of(context) == TextDirection.ltr
                  ? BorderSide(color: foreground, width: 4)
                  : BorderSide.none,
              right: Directionality.of(context) == TextDirection.rtl
                  ? BorderSide(color: foreground, width: 4)
                  : BorderSide.none,
            ),
          ),
          child: Row(
            children: <Widget>[
              Expanded(
                child: Semantics(
                  key: const ValueKey<String>('source-status-message'),
                  container: true,
                  liveRegion: true,
                  label: message,
                  child: ExcludeSemantics(
                    child: Padding(
                      padding: const EdgeInsets.symmetric(
                        horizontal: ProductSpacing.md,
                        vertical: ProductSpacing.sm,
                      ),
                      child: Row(
                        crossAxisAlignment: CrossAxisAlignment.center,
                        children: <Widget>[
                          Icon(_iconForSourceStatus(status), color: foreground),
                          const SizedBox(width: ProductSpacing.sm),
                          Expanded(
                            child: Text(
                              message,
                              maxLines: 3,
                              overflow: TextOverflow.ellipsis,
                              style: Theme.of(context).textTheme.bodyMedium
                                  ?.copyWith(
                                    color: foreground,
                                    fontWeight: FontWeight.w600,
                                  ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
              ),
              if (failureKey != null)
                SizedBox.square(
                  dimension: 48,
                  child: IconButton(
                    key: const ValueKey<String>('source-status-dismiss'),
                    tooltip: strings.t('dismiss'),
                    onPressed: onDismiss,
                    color: foreground,
                    icon: const Icon(Icons.close_rounded),
                  ),
                ),
              if (failureKey != null) const SizedBox(width: ProductSpacing.xs),
            ],
          ),
        ),
      ),
    );
  }
}

enum _SourceBannerStatus { failure, offline, partial, stale }

IconData _iconForSourceStatus(_SourceBannerStatus status) => switch (status) {
  _SourceBannerStatus.failure => Icons.error_outline_rounded,
  _SourceBannerStatus.offline => Icons.cloud_off_rounded,
  _SourceBannerStatus.partial => Icons.sync_problem_rounded,
  _SourceBannerStatus.stale => Icons.schedule_rounded,
};

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
    final offline = controller.snapshot?.isOffline ?? true;
    final enabled = entity.available && !offline && !pending;
    final active = entity.booleanValue == true;
    final hasQuickControl =
        entity.writable &&
        entity.type != HomeEntityType.unknown &&
        (entity.type.supportsToggle || entity.type.supportsPress);
    final stateText = pending
        ? strings.t('commandPending')
        : offline
        ? strings.t('offline')
        : strings.entityState(entity);
    final stateColor = pending
        ? scheme.primary
        : entity.available && !offline
        ? scheme.onSurfaceVariant
        : scheme.error;
    final surface = Color.alphaBlend(
      (pending
              ? scheme.secondary
              : active && enabled
              ? scheme.primary
              : scheme.onSurface)
          .withValues(
            alpha: pending
                ? 0.08
                : active && enabled
                ? 0.06
                : 0.02,
          ),
      scheme.surfaceContainerLow,
    );
    void openDetails() {
      showEntityDetails(context, entity, controller);
    }

    return TweenAnimationBuilder<Color?>(
      tween: ColorTween(end: surface),
      duration: ProductMotion.standard,
      curve: ProductMotion.stateChange,
      builder: (context, animatedSurface, child) => Card(
        color: animatedSurface,
        clipBehavior: Clip.antiAlias,
        child: child,
      ),
      child: Semantics(
        container: true,
        explicitChildNodes: true,
        child: Row(
          children: <Widget>[
            Expanded(
              child: Semantics(
                key: ValueKey<String>('entity-details-${entity.id.value}'),
                container: true,
                excludeSemantics: true,
                button: true,
                enabled: true,
                label: entity.name,
                value: stateText,
                hint: strings.t('details'),
                onTap: openDetails,
                child: InkWell(
                  excludeFromSemantics: true,
                  onTap: openDetails,
                  child: ConstrainedBox(
                    constraints: BoxConstraints(minHeight: compact ? 72 : 80),
                    child: Padding(
                      padding: EdgeInsets.fromLTRB(
                        compact ? ProductSpacing.sm : ProductSpacing.md,
                        compact ? ProductSpacing.sm : ProductSpacing.md,
                        ProductSpacing.sm,
                        compact ? ProductSpacing.sm : ProductSpacing.md,
                      ),
                      child: Row(
                        children: <Widget>[
                          _EntityIcon(
                            entity: entity,
                            active: active,
                            enabled: entity.available && !offline,
                            pending: pending,
                          ),
                          const SizedBox(width: ProductSpacing.sm),
                          Expanded(
                            child: Column(
                              mainAxisSize: MainAxisSize.min,
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: <Widget>[
                                Text(
                                  entity.name,
                                  maxLines: 2,
                                  overflow: TextOverflow.ellipsis,
                                  style: Theme.of(context).textTheme.titleMedium
                                      ?.copyWith(fontWeight: FontWeight.w700),
                                ),
                                const SizedBox(height: 2),
                                Text(
                                  stateText,
                                  maxLines: 2,
                                  overflow: TextOverflow.ellipsis,
                                  style: Theme.of(context).textTheme.bodySmall
                                      ?.copyWith(
                                        color: stateColor,
                                        fontWeight: pending
                                            ? FontWeight.w600
                                            : FontWeight.w400,
                                      ),
                                ),
                              ],
                            ),
                          ),
                          if (!hasQuickControl) ...<Widget>[
                            const SizedBox(width: ProductSpacing.xs),
                            ExcludeSemantics(
                              child: Icon(
                                Icons.chevron_right_rounded,
                                color: scheme.onSurfaceVariant,
                              ),
                            ),
                          ],
                        ],
                      ),
                    ),
                  ),
                ),
              ),
            ),
            if (hasQuickControl) ...<Widget>[
              const SizedBox(width: ProductSpacing.xs),
              Padding(
                padding: const EdgeInsets.only(right: ProductSpacing.sm),
                child: AnimatedSwitcher(
                  duration: ProductMotion.fast,
                  reverseDuration: ProductMotion.fast,
                  switchInCurve: ProductMotion.enter,
                  switchOutCurve: ProductMotion.exit,
                  transitionBuilder: (child, animation) => FadeTransition(
                    opacity: animation,
                    child: ScaleTransition(scale: animation, child: child),
                  ),
                  child: pending
                      ? KeyedSubtree(
                          key: ValueKey<String>(
                            'pending-control-${entity.id.value}',
                          ),
                          child: _PendingControl(entity: entity),
                        )
                      : KeyedSubtree(
                          key: ValueKey<String>(
                            'quick-control-${entity.id.value}',
                          ),
                          child: _QuickControl(
                            entity: entity,
                            enabled: enabled,
                            onValue: (value) => requestEntityCommand(
                              context,
                              entity,
                              value,
                              controller,
                            ),
                          ),
                        ),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

final class _EntityIcon extends StatelessWidget {
  const _EntityIcon({
    required this.entity,
    required this.active,
    required this.enabled,
    required this.pending,
  });

  final HomeEntity entity;
  final bool active;
  final bool enabled;
  final bool pending;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final background = pending
        ? scheme.secondaryContainer
        : active && enabled
        ? scheme.primaryContainer
        : scheme.surfaceContainerHighest;
    final foreground = pending
        ? scheme.onSecondaryContainer
        : active && enabled
        ? scheme.onPrimaryContainer
        : scheme.onSurfaceVariant.withValues(alpha: enabled ? 1 : 0.55);
    return ExcludeSemantics(
      child: Container(
        width: 48,
        height: 48,
        decoration: BoxDecoration(
          color: background,
          borderRadius: BorderRadius.circular(ProductRadius.control),
        ),
        child: Icon(iconForEntity(entity.type), color: foreground),
      ),
    );
  }
}

final class _PendingControl extends StatelessWidget {
  const _PendingControl({required this.entity});

  final HomeEntity entity;

  @override
  Widget build(BuildContext context) {
    final label = HomeControlStrings.of(context).t('commandPending');
    return Semantics(
      key: ValueKey<String>('entity-pending-${entity.id.value}'),
      container: true,
      liveRegion: true,
      label: entity.name,
      value: label,
      child: const ExcludeSemantics(
        child: SizedBox.square(
          dimension: 48,
          child: Padding(
            padding: EdgeInsets.all(12),
            child: CircularProgressIndicator(strokeWidth: 2.5),
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
    final strings = HomeControlStrings.of(context);
    if (entity.type.supportsToggle) {
      final value = entity.booleanValue == true;
      final action = strings.t(value ? 'turnOff' : 'turnOn');
      final activate = enabled ? () => onValue(!value) : null;
      return Semantics(
        key: ValueKey<String>('entity-toggle-${entity.id.value}'),
        container: true,
        label: '$action: ${entity.name}',
        value: strings.entityState(entity),
        toggled: value,
        enabled: enabled,
        onTap: activate,
        child: Tooltip(
          message: enabled ? action : strings.t('unavailable'),
          excludeFromSemantics: true,
          child: InkResponse(
            excludeFromSemantics: true,
            containedInkWell: true,
            highlightShape: BoxShape.circle,
            onTap: activate,
            child: SizedBox.square(
              dimension: 48,
              child: Center(
                child: ExcludeSemantics(
                  child: IgnorePointer(
                    child: Switch(
                      value: value,
                      onChanged: enabled ? (_) {} : null,
                      materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
      );
    }
    if (entity.type.supportsPress) {
      final run = strings.t('run');
      return SizedBox.square(
        dimension: 48,
        child: IconButton.filledTonal(
          key: ValueKey<String>('entity-run-${entity.id.value}'),
          tooltip:
              '${enabled ? run : strings.t('unavailable')}: ${entity.name}',
          onPressed: enabled ? () => onValue(true) : null,
          icon: const Icon(Icons.play_arrow_rounded),
        ),
      );
    }
    return const SizedBox.shrink();
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
      builder: (context) => HomeAlertDialog(
        icon: entity.risk == HomeCommandRisk.critical
            ? Icons.warning_amber_rounded
            : Icons.info_outline_rounded,
        title: strings.t('confirmTitle'),
        message: strings.t(
          entity.risk == HomeCommandRisk.critical
              ? 'confirmCritical'
              : 'confirmConsequential',
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
  unawaited(switch (entity.risk) {
    HomeCommandRisk.routine => HapticFeedback.selectionClick(),
    HomeCommandRisk.consequential => HapticFeedback.mediumImpact(),
    HomeCommandRisk.critical => HapticFeedback.heavyImpact(),
  });
  final accepted = await controller.sendCommand(entity, value);
  if (accepted) unawaited(HapticFeedback.lightImpact());
  return accepted;
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
      final valueLabel = entity.unit.isEmpty
          ? current.toStringAsFixed(1)
          : '${current.toStringAsFixed(1)} ${entity.unit}';
      return HomeSlider(
        label: entity.name,
        value: current,
        minimum: min,
        maximum: max <= min ? min + 1 : max,
        divisions: _divisions(entity, min, max),
        valueLabel: valueLabel,
        onChanged: widget.enabled
            ? (value) => setState(() => _draftNumber = value)
            : null,
        onChangeEnd: widget.enabled
            ? (value) {
                widget.onValue(value);
                if (mounted) setState(() => _draftNumber = null);
              }
            : null,
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
