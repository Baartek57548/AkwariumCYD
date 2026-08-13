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

    return Card(
      color: surface,
      clipBehavior: Clip.antiAlias,
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
                child: pending
                    ? _PendingControl(entity: entity)
                    : _QuickControl(
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
        label: '$action: ${eã_7¶‰žËkºwµçd°4(€€€€€€€€€€€€€€€€€€€ÍÑå±”èQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹Ñ•áÑQ¡•µ”¹‰½‘åMµ…±°ü¹½Áå]¥Ñ  4(€€€€€€€€€€€€€€€€€€€€€½±½ÈèQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹½±½ÉM¡•µ”¹½¹MÕÉ™…•Y…É¥…¹Ð°4(€€€€€€€€€€€€€€€€€€€€¤°4(€€€€€€€€€€€€€€€€€€¤°4(€€€€€€€€€€€€€€€€€Q•áÐ 4(€€€€€€€€€€€€€€€€€€€ÍÑÉ¥¹Ì¹Ý¥Ñ¡Y…±Õ” 4(€€€€€€€€€€€€€€€€€€€€€€•¹Ñ¥ÑåUÁ‘…Ñ•œ°4(€€€€€€€€€€€€€€€€€€€€€ÍÑÉ¥¹Ì¹É•±…Ñ¥Ù•Q¥µ”¡•¹Ñ¥Ñä¹ÕÁ‘…Ñ•‘Ð¤°4(€€€€€€€€€€€€€€€€€€€€¤°4(€€€€€€€€€€€€€€€€€€€ÍÑå±”èQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹Ñ•áÑQ¡•µ”¹‰½‘åMµ…±°ü¹½Áå]¥Ñ  4(€€€€€€€€€€€€€€€€€€€€€½±½ÈèQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹½±½ÉM¡•µ”¹½¹MÕÉ™…•Y…É¥…¹Ð°4(€€€€€€€€€€€€€€€€€€€€¤°4(€€€€€€€€€€€€€€€€€€¤°4(€€€€€€€€€€€€€€€€€Q•áÐ 4(€€€€€€€€€€€€€€€€€€€•¹Ñ¥Ñä¹¥¹Ù…±Õ”°4(€€€€€€€€€€€€€€€€€€€ÍÑå±”èQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹Ñ•áÑQ¡•µ”¹‰½‘åMµ…±°ü¹½Áå]¥Ñ  4(€€€€€€€€€€€€€€€€€€€€€½±½ÈèQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹½±½ÉM¡•µ”¹½¹MÕÉ™…•Y…É¥…¹Ð°4(€€€€€€€€€€€€€€€€€€€€¤°4(€€€€€€€€€€€€€€€€€€¤°4(€€€€€€€€€€€€€€€t°4(€€€€€€€€€€€€€€¤°4(€€€€€€€€€€€€¤°4(€€€€€€€€€€¤°4(€€€€€€€€¤ì4(€€€€€ô°4(€€€€¤ì4(€ô4)ô4(4)™¥¹…°±…ÍÌ}•Ñ…¥±•‘½¹ÑÉ½°•áÑ•¹‘ÌMÑ…Ñ•™Õ±]¥‘•Ðì4(€½¹ÍÐ}•Ñ…¥±•‘½¹ÑÉ½°¡ì4(€€€É•ÅÕ¥É•Ñ¡¥Ì¹•¹Ñ¥Ñä°4(€€€É•ÅÕ¥É•Ñ¡¥Ì¹•¹…‰±•°4(€€€É•ÅÕ¥É•Ñ¡¥Ì¹½¹Y…±Õ”°4(€ô¤ì4(4(€™¥¹…°!½µ•¹Ñ¥Ñä•¹Ñ¥Ñäì4(€™¥¹…°‰½½°•¹…‰±•ì4(€™¥¹…°Y…±Õ•¡…¹•ñ=‰©•Ðüø½¹Y…±Õ”ì4(4(€½Ù•ÉÉ¥‘”4(€MÑ…Ñ”ñ}•Ñ…¥±•‘½¹ÑÉ½°øÉ•…Ñ•MÑ…Ñ” ¤€ôø}•Ñ…¥±•‘½¹ÑÉ½±MÑ…Ñ” ¤ì4)ô4(4)™¥¹…°±…ÍÌ}•Ñ…¥±•‘½¹ÑÉ½±MÑ…Ñ”•áÑ•¹‘ÌMÑ…Ñ”ñ}•Ñ…¥±•‘½¹ÑÉ½°øì4(€‘½Õ‰±”ü}‘É…™Ñ9Õµ‰•Èì4(4(€!½µ•¹Ñ¥Ñä•Ð•¹Ñ¥Ñä€ôøÝ¥‘•Ð¹•¹Ñ¥Ñäì4(4(€½Ù•ÉÉ¥‘”4(€]¥‘•Ð‰Õ¥±¡	Õ¥±‘½¹Ñ•áÐ½¹Ñ•áÐ¤ì4(€€€™¥¹…°ÍÑÉ¥¹Ì€ô!½µ•½¹ÑÉ½±MÑÉ¥¹Ì¹½˜¡½¹Ñ•áÐ¤ì4(€€€¥˜€¡•¹Ñ¥Ñä¹ÑåÁ”€ôô!½µ•¹Ñ¥ÑåQåÁ”¹½Ù•È¤ì4(€€€€€É•ÑÕÉ¸M•µ•¹Ñ•‘	ÕÑÑ½¸ñ‰½½°ø 4(€€€€€€€Í•µ•¹ÑÌè€ñ	ÕÑÑ½¹M•µ•¹Ðñ‰½½°øùl4(€€€€€€€€€	ÕÑÑ½¹M•µ•¹Ðñ‰½½°ø 4(€€€€€€€€€€€Ù…±Õ”è™…±Í”°4(€€€€€€€€€€€±…‰•°èQ•áÐ¡ÍÑÉ¥¹Ì¹Ð ±½Í•½Ù•Èœ¤¤°4(€€€€€€€€€€€¥½¸è½¹ÍÐ%½¸¡%½¹Ì¹Ù•ÉÑ¥…±}…±¥¹}‰½ÑÑ½µ}É½Õ¹‘•¤°4(€€€€€€€€€€¤°4(€€€€€€€€€	ÕÑÑ½¹M•µ•¹Ðñ‰½½°ø 4(€€€€€€€€€€€Ù…±Õ”èÑÉÕ”°4(€€€€€€€€€€€±…‰•°èQ•áÐ¡ÍÑÉ¥¹Ì¹Ð ½Á•¹½Ù•Èœ¤¤°4(€€€€€€€€€€€¥½¸è½¹ÍÐ%½¸¡%½¹Ì¹Ù•ÉÑ¥…±}…±¥¹}Ñ½Á}É½Õ¹‘•¤°4(€€€€€€€€€€¤°4(€€€€€€€t°4(€€€€€€€Í•±•Ñ•è€ñ‰½½°ùí•¹Ñ¥Ñä¹‰½½±•…¹Y…±Õ”€ôôÑÉÕ•ô°4(€€€€€€€½¹M•±•Ñ¥½¹¡…¹•èÝ¥‘•Ð¹•¹…‰±•4(€€€€€€€€€€€€ü€¡Í•±•Ñ¥½¸¤€ôøÝ¥‘•Ð¹½¹Y…±Õ”¡Í•±•Ñ¥½¸¹™¥ÉÍÐ¤4(€€€€€€€€€€€€è¹Õ±°°4(€€€€€€¤ì4(€€€ô4(€€€¥˜€¡•¹Ñ¥Ñä¹ÑåÁ”€ôô!½µ•¹Ñ¥ÑåQåÁ”¹±½¬¤ì4(€€€€€É•ÑÕÉ¸M•µ•¹Ñ•‘	ÕÑÑ½¸ñ‰½½°ø 4(€€€€€€€Í•µ•¹ÑÌè€ñ	ÕÑÑ½¹M•µ•¹Ðñ‰½½°øùl4(€€€€€€€€€	ÕÑÑ½¹M•µ•¹Ðñ‰½½°ø 4(€€€€€€€€€€€Ù…±Õ”è™…±Í”°4(€€€€€€€€€€€±…‰•°èQ•áÐ¡ÍÑÉ¥¹Ì¹Ð ±½­Ñ¥½¸œ¤¤°4(€€€€€€€€€€€¥½¸è½¹ÍÐ%½¸¡%½¹Ì¹±½­}É½Õ¹‘•¤°4(€€€€€€€€€€¤°4(€€€€€€€€€	ÕÑÑ½¹M•µ•¹Ðñ‰½½°ø 4(€€€€€€€€€€€Ù…±Õ”èÑÉÕ”°4(€€€€€€€€€€€±…‰•°èQ•áÐ¡ÍÑÉ¥¹Ì¹Ð Õ¹±½­Ñ¥½¸œ¤¤°4(€€€€€€€€€€€¥½¸è½¹ÍÐ%½¸¡%½¹Ì¹±½­}½Á•¹}É½Õ¹‘•¤°4(€€€€€€€€€€¤°4(€€€€€€€t°4(€€€€€€€Í•±•Ñ•è€ñ‰½½°ùí•¹Ñ¥Ñä¹‰½½±•…¹Y…±Õ”€ôôÑÉÕ•ô°4(€€€€€€€½¹M•±•Ñ¥½¹¡…¹•èÝ¥‘•Ð¹•¹…‰±•4(€€€€€€€€€€€€ü€¡Í•±•Ñ¥½¸¤€ôøÝ¥‘•Ð¹½¹Y…±Õ”¡Í•±•Ñ¥½¸¹™¥ÉÍÐ¤4(€€€€€€€€€€€€è¹Õ±°°4(€€€€€€¤ì4(€€€ô4(€€€¥˜€¡•¹Ñ¥Ñä¹ÑåÁ”€ôô!½µ•¹Ñ¥ÑåQåÁ”¹…±…Éµ½¹ÑÉ½±A…¹•°¤ì4(€€€€€½¹ÍÐµ½‘•Ì€ô€ñMÑÉ¥¹œùl4(€€€€€€€€‘¥Í…Éµ•œ°4(€€€€€€€€…Éµ•‘}¡½µ”œ°4(€€€€€€€€…Éµ•‘}…Ý…äœ°4(€€€€€€€€…Éµ•‘}¹¥¡Ðœ°4(€€€€€tì4(€€€€€™¥¹…°ÕÉÉ•¹Ð€ôµ½‘•Ì¹½¹Ñ…¥¹Ì¡•¹Ñ¥Ñä¹ÍÑ…Ñ”¤4(€€€€€€€€€€ü•¹Ñ¥Ñä¹ÍÑ…Ñ”…ÌMÑÉ¥¹œ4(€€€€€€€€€€è¹Õ±°ì4(€€€€€É•ÑÕÉ¸É½Á‘½Ý¹	ÕÑÑ½¹½Éµ¥•±ñMÑÉ¥¹œø 4(€€€€€€€­•äèY…±Õ•-•äñMÑÉ¥¹œø 4(€€€€€€€€€€œ‘í•¹Ñ¥Ñä¹¥¹Ù…±Õ•ôè‘í•¹Ñ¥Ñä¹ÍÑ…Ñ•ôè‘íÝ¥‘•Ð¹•¹…‰±•‘ôœ°4(€€€€€€€€¤°4(€€€€€€€¥¹¥Ñ¥…±Y…±Õ”èÕÉÉ•¹Ð°4(€€€€€€€¥ÍáÁ…¹‘•èÑÉÕ”°4(€€€€€€€‘•½É…Ñ¥½¸è%¹ÁÕÑ•½É…Ñ¥½¸¡±…‰•±Q•áÐèÍÑÉ¥¹Ì¹Ð …±…Éµ5½‘”œ¤¤°4(€€€€€€€¥Ñ•µÌè€ñÉ½Á‘½Ý¹5•¹Õ%Ñ•´ñMÑÉ¥¹œøùl4(€€€€€€€€€™½È€¡™¥¹…°µ½‘”¥¸µ½‘•Ì¤4(€€€€€€€€€€€É½Á‘½Ý¹5•¹Õ%Ñ•´ñMÑÉ¥¹œø 4(€€€€€€€€€€€€€Ù…±Õ”èµ½‘”°4(€€€€€€€€€€€€€¡¥±èQ•áÐ 4(€€€€€€€€€€€€€€€ÍÑÉ¥¹Ì¹Ð …±…Éµ|‘µ½‘”œ¤°4(€€€€€€€€€€€€€€€µ…á1¥¹•Ìè€Ä°4(€€€€€€€€€€€€€€€½Ù•É™±½ÜèQ•áÑ=Ù•É™±½Ü¹•±±¥ÁÍ¥Ì°4(€€€€€€€€€€€€€€¤°4(€€€€€€€€€€€€¤°4(€€€€€€€t°4(€€€€€€€½¹¡…¹•èÝ¥‘•Ð¹•¹…‰±•4(€€€€€€€€€€€€ü€¡Ù…±Õ”¤ì4(€€€€€€€€€€€€€€€¥˜€¡Ù…±Õ”€„ô¹Õ±°¤Ý¥‘•Ð¹½¹Y…±Õ”¡Ù…±Õ”¤ì4(€€€€€€€€€€€€€ô4(€€€€€€€€€€€€è¹Õ±°°4(€€€€€€¤ì4(€€€ô4(€€€¥˜€¡•¹Ñ¥Ñä¹ÑåÁ”€ôô!½µ•¹Ñ¥ÑåQåÁ”¹Ù…ÕÕ´ñð4(€€€€€€€•¹Ñ¥Ñä¹ÑåÁ”€ôô!½µ•¹Ñ¥ÑåQåÁ”¹µ•‘¥…A±…å•È¤ì4(€€€€€É•ÑÕÉ¸M•µ•¹Ñ•‘	ÕÑÑ½¸ñ‰½½°ø 4(€€€€€€€Í•µ•¹ÑÌè€ñ	ÕÑÑ½¹M•µ•¹Ðñ‰½½°øùl4(€€€€€€€€€	ÕÑÑ½¹M•µ•¹Ðñ‰½½°ø 4(€€€€€€€€€€€Ù…±Õ”è™…±Í”°4(€€€€€€€€€€€±…‰•°èQ•áÐ 4(€€€€€€€€€€€€€ÍÑÉ¥¹Ì¹Ð 4(€€€€€€€€€€€€€€€•¹Ñ¥Ñä¹ÑåÁ”€ôô!½µ•¹Ñ¥ÑåQåÁ”¹Ù…ÕÕ´4(€€€€€€€€€€€€€€€€€€€€ü€É•ÑÕÉ¹Q½	…Í”œ4(€€€€€€€€€€€€€€€€€€€€è€ÑÕÉ¹=™˜œ°4(€€€€€€€€€€€€€€¤°4(€€€€€€€€€€€€¤°4(€€€€€€€€€€€¥½¸è½¹ÍÐ%½¸¡%½¹Ì¹ÍÑ½Á}É½Õ¹‘•¤°4(€€€€€€€€€€¤°4(€€€€€€€€€	ÕÑÑ½¹M•µ•¹Ðñ‰½½°ø 4(€€€€€€€€€€€Ù…±Õ”èÑÉÕ”°4(€€€€€€€€€€€±…‰•°èQ•áÐ 4(€€€€€€€€€€€€€ÍÑÉ¥¹Ì¹Ð 4(€€€€€€€€€€€€€€€•¹Ñ¥Ñä¹ÑåÁ”€ôô!½µ•¹Ñ¥ÑåQåÁ”¹Ù…ÕÕ´€ü€ÍÑ…ÉÐœ€è€ÑÕÉ¹=¸œ°4(€€€€€€€€€€€€€€¤°4(€€€€€€€€€€€€¤°4(€€€€€€€€€€€¥½¸è½¹ÍÐ%½¸¡%½¹Ì¹Á±…å}…ÉÉ½Ý}É½Õ¹‘•¤°4(€€€€€€€€€€¤°4(€€€€€€€t°4(€€€€€€€Í•±•Ñ•è€ñ‰½½°ùí•¹Ñ¥Ñä¹‰½½±•…¹Y…±Õ”€ôôÑÉÕ•ô°4(€€€€€€€½¹M•±•Ñ¥½¹¡…¹•èÝ¥‘•Ð¹•¹…‰±•4(€€€€€€€€€€€€ü€¡Í•±•Ñ¥½¸¤€ôøÝ¥‘•Ð¹½¹Y…±Õ”¡Í•±•Ñ¥½¸¹™¥ÉÍÐ¤4(€€€€€€€€€€€€è¹Õ±°°4(€€€€€€¤ì4(€€€ô4(€€€¥˜€¡•¹Ñ¥Ñä¹ÑåÁ”¹ÍÕÁÁ½ÉÑÍQ½±”¤ì4(€€€€€™¥¹…°Ù…±Õ”€ô•¹Ñ¥Ñä¹‰½½±•…¹Y…±Õ”€ôôÑÉÕ”ì4(€€€€€É•ÑÕÉ¸M•µ•¹Ñ•‘	ÕÑÑ½¸ñ‰½½°ø 4(€€€€€€€Í•µ•¹ÑÌè€ñ	ÕÑÑ½¹M•µ•¹Ðñ‰½½°øùl4(€€€€€€€€€	ÕÑÑ½¹M•µ•¹Ðñ‰½½°ø 4(€€€€€€€€€€€Ù…±Õ”è™…±Í”°4(€€€€€€€€€€€±…‰•°èQ•áÐ¡ÍÑÉ¥¹Ì¹Ð ÑÕÉ¹=™˜œ¤¤°4(€€€€€€€€€€€¥½¸è½¹ÍÐ%½¸¡%½¹Ì¹Á½Ý•É}Í•ÑÑ¥¹Í}¹•Ý}É½Õ¹‘•¤°4(€€€€€€€€€€¤°4(€€€€€€€€€	ÕÑÑ½¹M•µ•¹Ðñ‰½½°ø 4(€€€€€€€€€€€Ù…±Õ”èÑÉÕ”°4(€€€€€€€€€€€±…‰•°èQ•áÐ¡ÍÑÉ¥¹Ì¹Ð ÑÕÉ¹=¸œ¤¤°4(€€€€€€€€€€€¥½¸è½¹ÍÐ%½¸¡%½¹Ì¹Á½Ý•É}É½Õ¹‘•¤°4(€€€€€€€€€€¤°4(€€€€€€€t°4(€€€€€€€Í•±•Ñ•è€ñ‰½½°ùíÙ…±Õ•ô°4(€€€€€€€½¹M•±•Ñ¥½¹¡…¹•èÝ¥‘•Ð¹•¹…‰±•4(€€€€€€€€€€€€ü€¡Í•±•Ñ¥½¸¤€ôøÝ¥‘•Ð¹½¹Y…±Õ”¡Í•±•Ñ¥½¸¹™¥ÉÍÐ¤4(€€€€€€€€€€€€è¹Õ±°°4(€€€€€€¤ì4(€€€ô4(€€€¥˜€¡•¹Ñ¥Ñä¹ÑåÁ”¹ÍÕÁÁ½ÉÑÍAÉ•ÍÌ¤ì4(€€€€€É•ÑÕÉ¸¥±±•‘	ÕÑÑ½¸¹¥½¸ 4(€€€€€€€½¹AÉ•ÍÍ•èÝ¥‘•Ð¹•¹…‰±•€ü€ ¤€ôøÝ¥‘•Ð¹½¹Y…±Õ”¡ÑÉÕ”¤€è¹Õ±°°4(€€€€€€€¥½¸è½¹ÍÐ%½¸¡%½¹Ì¹Á±…å}…ÉÉ½Ý}É½Õ¹‘•¤°4(€€€€€€€±…‰•°èQ•áÐ¡ÍÑÉ¥¹Ì¹Ð ÉÕ¸œ¤¤°4(€€€€€€¤ì4(€€€ô4(€€€¥˜€ ñ!½µ•¹Ñ¥ÑåQåÁ”ùì4(€€€€€!½µ•¹Ñ¥ÑåQåÁ”¹Í•±•Ð°4(€€€€€!½µ•¹Ñ¥ÑåQåÁ”¹¥¹ÁÕÑM•±•Ð°4(€€€ô¹½¹Ñ…¥¹Ì¡•¹Ñ¥Ñä¹ÑåÁ”¤¤ì4(€€€€€™¥¹…°½ÁÑ¥½¹Ì€ô•¹Ñ¥Ñä¹½¹ÍÑÉ…¥¹ÑÌ¹½ÁÑ¥½¹Ìì4(€€€€€™¥¹…°ÍÑ…Ñ”€ô•¹Ñ¥Ñä¹ÍÑ…Ñ”ü¹Ñ½MÑÉ¥¹œ ¤ì4(€€€€€É•ÑÕÉ¸É½Á‘½Ý¹	ÕÑÑ½¹½Éµ¥•±ñMÑÉ¥¹œø 4(€€€€€€€­•äèY…±Õ•-•äñMÑÉ¥¹œø 4(€€€€€€€€€€œ‘í•¹Ñ¥Ñä¹¥¹Ù…±Õ•ôè‘í•¹Ñ¥Ñä¹ÍÑ…Ñ•ôè‘íÝ¥‘•Ð¹•¹…‰±•‘ôœ°4(€€€€€€€€¤°4(€€€€€€€¥¹¥Ñ¥…±Y…±Õ”è½ÁÑ¥½¹Ì¹½¹Ñ…¥¹Ì¡ÍÑ…Ñ”¤€üÍÑ…Ñ”€è¹Õ±°°4(€€€€€€€¥ÍáÁ…¹‘•èÑÉÕ”°4(€€€€€€€‘•½É…Ñ¥½¸è%¹ÁÕÑ•½É…Ñ¥½¸¡±…‰•±Q•áÐèÍÑÉ¥¹Ì¹Ð Í•±•Ñ=ÁÑ¥½¸œ¤¤°4(€€€€€€€¥Ñ•µÌè€ñÉ½Á‘½Ý¹5•¹Õ%Ñ•´ñMÑÉ¥¹œøùl4(€€€€€€€€€™½È€¡™¥¹…°½ÁÑ¥½¸¥¸½ÁÑ¥½¹Ì¤4(€€€€€€€€€€€É½Á‘½Ý¹5•¹Õ%Ñ•´ñMÑÉ¥¹œø 4(€€€€€€€€€€€€€Ù…±Õ”è½ÁÑ¥½¸°4(€€€€€€€€€€€€€¡¥±èQ•áÐ¡½ÁÑ¥½¸°µ…á1¥¹•Ìè€Ä°½Ù•É™±½ÜèQ•áÑ=Ù•É™±½Ü¹•±±¥ÁÍ¥Ì¤°4(€€€€€€€€€€€€¤°4(€€€€€€€t°4(€€€€€€€½¹¡…¹•èÝ¥‘•Ð¹•¹…‰±•4(€€€€€€€€€€€€ü€¡Ù…±Õ”¤ì4(€€€€€€€€€€€€€€€¥˜€¡Ù…±Õ”€„ô¹Õ±°¤Ý¥‘•Ð¹½¹Y…±Õ”¡Ù…±Õ”¤ì4(€€€€€€€€€€€€€ô4(€€€€€€€€€€€€è¹Õ±°°4(€€€€€€¤ì4(€€€ô4(€€€¥˜€ ñ!½µ•¹Ñ¥ÑåQåÁ”ùì4(€€€€€!½µ•¹Ñ¥ÑåQåÁ”¹¹Õµ‰•È°4(€€€€€!½µ•¹Ñ¥ÑåQåÁ”¹¥¹ÁÕÑ9Õµ‰•È°4(€€€€€!½µ•¹Ñ¥ÑåQåÁ”¹±¥µ…Ñ”°4(€€€ô¹½¹Ñ…¥¹Ì¡•¹Ñ¥Ñä¹ÑåÁ”¤¤ì4(€€€€€™¥¹…°µ¥¸€ô•¹Ñ¥Ñä¹½¹ÍÑÉ…¥¹ÑÌ¹µ¥¹¥µÕ´€üü€Àì4(€€€€€™¥¹…°µ…à€ô•¹Ñ¥Ñä¹½¹ÍÑÉ…¥¹ÑÌ¹µ…á¥µÕ´€üü€ÄÀÀì4(€€€€€™¥¹…°…ÑÑÉ¥‰ÕÑ•Q•µÁ•É…ÑÕÉ”€ô•¹Ñ¥Ñä¹…ÑÑÉ¥‰ÕÑ•ÍlÑ•µÁ•É…ÑÕÉ”tì4(€€€€€™¥¹…°Ñ•µÁ•É…ÑÕÉ”€ô…ÑÑÉ¥‰ÕÑ•Q•µÁ•É…ÑÕÉ”¥Ì¹Õ´4(€€€€€€€€€€ü…ÑÑÉ¥‰ÕÑ•Q•µÁ•É…ÑÕÉ”¹Ñ½½Õ‰±” ¤4(€€€€€€€€€€è‘½Õ‰±”¹ÑÉåA…ÉÍ”¡…ÑÑÉ¥‰ÕÑ•Q•µÁ•É…ÑÕÉ”ü¹Ñ½MÑÉ¥¹œ ¤€üü€œœ¤ì4(€€€€€™¥¹…°ÕÉÉ•¹Ð€ô4(€€€€€€€€€€¡}‘É…™Ñ9Õµ‰•È€üü•¹Ñ¥Ñä¹¹Õµ•É¥Y…±Õ”€üüÑ•µÁ•É…ÑÕÉ”€üüµ¥¸¤¹±…µÀ 4(€€€€€€€€€€€µ¥¸°4(€€€€€€€€€€€µ…à°4(€€€€€€€€€€¤ì4(€€€€€É•ÑÕÉ¸½±Õµ¸ 4(€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl4(€€€€€€€€€M±¥‘•È 4(€€€€€€€€€€€Ù…±Õ”èÕÉÉ•¹Ð°4(€€€€€€€€€€€µ¥¸èµ¥¸°4(€€€€€€€€€€€µ…àèµ…à€ðôµ¥¸€üµ¥¸€¬€Ä€èµ…à°4(€€€€€€€€€€€‘¥Ù¥Í¥½¹Ìè}‘¥Ù¥Í¥½¹Ì¡•¹Ñ¥Ñä°µ¥¸°µ…à¤°4(€€€€€€€€€€€±…‰•°èÕÉÉ•¹Ð¹Ñ½MÑÉ¥¹Í¥á• Ä¤°4(€€€€€€€€€€€½¹¡…¹•èÝ¥‘•Ð¹•¹…‰±•4(€€€€€€€€€€€€€€€€ü€¡Ù…±Õ”¤€ôøÍ•ÑMÑ…Ñ”  ¤€ôø}‘É…™Ñ9Õµ‰•È€ôÙ…±Õ”¤4(€€€€€€€€€€€€€€€€è¹Õ±°°4(€€€€€€€€€€€½¹¡…¹•¹èÝ¥‘•Ð¹•¹…‰±•4(€€€€€€€€€€€€€€€€ü€¡Ù…±Õ”¤ì4(€€€€€€€€€€€€€€€€€€€Ý¥‘•Ð¹½¹Y…±Õ”¡Ù…±Õ”¤ì4(€€€€€€€€€€€€€€€€€€€¥˜€¡µ½Õ¹Ñ•¤Í•ÑMÑ…Ñ”  ¤€ôø}‘É…™Ñ9Õµ‰•È€ô¹Õ±°¤ì4(€€€€€€€€€€€€€€€€€ô4(€€€€€€€€€€€€€€€€è¹Õ±°°4(€€€€€€€€€€¤°4(€€€€€€€€€Q•áÐ¡ÍÑÉ¥¹Ì¹Ý¥Ñ¡Y…±Õ” Í•ÑY…±Õ”œ°ÕÉÉ•¹Ð¹Ñ½MÑÉ¥¹Í¥á• Ä¤¤¤°4(€€€€€€€t°4(€€€€€€¤ì4(€€€ô4(€€€¥˜€ ñ!½µ•¹Ñ¥ÑåQåÁ”ùì4(€€€€€!½µ•¹Ñ¥ÑåQåÁ”¹Ñ•áÐ°4(€€€€€!½µ•¹Ñ¥ÑåQåÁ”¹¥¹ÁÕÑQ•áÐ°4(€€€ô¹½¹Ñ…¥¹Ì¡•¹Ñ¥Ñä¹ÑåÁ”¤¤ì4(€€€€€É•ÑÕÉ¸Q•áÑ½Éµ¥•± 4(€€€€€€€•¹…‰±•èÝ¥‘•Ð¹•¹…‰±•°4(€€€€€€€¥¹¥Ñ¥…±Y…±Õ”è•¹Ñ¥Ñä¹ÍÑ…Ñ”ü¹Ñ½MÑÉ¥¹œ ¤€üü€œœ°4(€€€€€€€µ…á1•¹Ñ è€ÈÔÔ°4(€€€€€€€Ñ•áÑ%¹ÁÕÑÑ¥½¸èQ•áÑ%¹ÁÕÑÑ¥½¸¹‘½¹”°4(€€€€€€€‘•½É…Ñ¥½¸è%¹ÁÕÑ•½É…Ñ¥½¸ 4(€€€€€€€€€±…‰•±Q•áÐèÍÑÉ¥¹Ì¹Ð Ñ•áÑY…±Õ”œ¤°4(€€€€€€€€€ÍÕ™™¥á%½¸è½¹ÍÐ%½¸¡%½¹Ì¹Í•¹‘}É½Õ¹‘•¤°4(€€€€€€€€¤°4(€€€€€€€½¹¥•±‘MÕ‰µ¥ÑÑ•èÝ¥‘•Ð¹½¹Y…±Õ”°4(€€€€€€¤ì4(€€€ô4(€€€É•ÑÕÉ¸}!¥¹Ð¡Ñ•áÐèÍÑÉ¥¹Ì¹Ð •ÉÉ½ÉU¹ÍÕÁÁ½ÉÑ•œ¤¤ì4(€ô4(4(€¥¹Ðü}‘¥Ù¥Í¥½¹Ì¡!½µ•¹Ñ¥Ñä•¹Ñ¥Ñä°‘½Õ‰±”µ¥¸°‘½Õ‰±”µ…à¤ì4(€€€™¥¹…°ÍÑ•À€ô•¹Ñ¥Ñä¹½¹ÍÑÉ…¥¹ÑÌ¹ÍÑ•Àì4(€€€¥˜€¡ÍÑ•À€ôô¹Õ±°ñðÍÑ•À€ðô€Àñðµ…à€ðôµ¥¸¤É•ÑÕÉ¸¹Õ±°ì4(€€€É•ÑÕÉ¸µ…Ñ ¹µ…à Ä°€ ¡µ…à€´µ¥¸¤€¼ÍÑ•À¤¹É½Õ¹ ¤¤¹±…µÀ Ä°€ÄÀÀÀ¤¹Ñ½%¹Ð ¤ì4(€ô4)ô4(4)™¥¹…°±…ÍÌ}¹Ñ¥ÑåÑÑÉ¥‰ÕÑ•Ì•áÑ•¹‘ÌMÑ…Ñ•±•ÍÍ]¥‘•Ðì4(€½¹ÍÐ}¹Ñ¥ÑåÑÑÉ¥‰ÕÑ•Ì¡íÉ•ÅÕ¥É•Ñ¡¥Ì¹•¹Ñ¥Ñåô¤ì4(4(€™¥¹…°!½µ•¹Ñ¥Ñä•¹Ñ¥Ñäì4(4(€½Ù•ÉÉ¥‘”4(€]¥‘•Ð‰Õ¥±¡	Õ¥±‘½¹Ñ•áÐ½¹Ñ•áÐ¤ì4(€€€™¥¹…°ÍÑÉ¥¹Ì€ô!½µ•½¹ÑÉ½±MÑÉ¥¹Ì¹½˜¡½¹Ñ•áÐ¤ì4(€€€™¥¹…°•¹ÑÉ¥•Ì€ô•¹Ñ¥Ñä¹…ÑÑÉ¥‰ÕÑ•Ì¹•¹ÑÉ¥•Ì4(€€€€€€€€¹Ý¡•É” ¡•¹ÑÉä¤€ôø€…}¡¥‘‘•¹ÑÑÉ¥‰ÕÑ•Ì¹½¹Ñ…¥¹Ì¡•¹ÑÉä¹­•ä¤¤4(€€€€€€€€¹Ñ…­” ÄÈ¤4(€€€€€€€€¹Ñ½1¥ÍÐ¡É½Ý…‰±”è™…±Í”¤ì4(€€€¥˜€¡•¹ÑÉ¥•Ì¹¥ÍµÁÑä¤É•ÑÕÉ¸½¹ÍÐM¥é•‘	½à¹Í¡É¥¹¬ ¤ì4(€€€É•ÑÕÉ¸áÁ…¹Í¥½¹Q¥±” 4(€€€€€Ñ¥±•A…‘‘¥¹œè‘•%¹Í•ÑÌ¹é•É¼°4(€€€€€Ñ¥Ñ±”èQ•áÐ¡ÍÑÉ¥¹Ì¹Ð …ÑÑÉ¥‰ÕÑ•Ìœ¤¤°4(€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl4(€€€€€€€™½È€¡™¥¹…°•¹ÑÉä¥¸•¹ÑÉ¥•Ì¤4(€€€€€€€€€1¥ÍÑQ¥±” 4(€€€€€€€€€€€‘•¹Í”èÑÉÕ”°4(€€€€€€€€€€€½¹Ñ•¹ÑA…‘‘¥¹œè‘•%¹Í•ÑÌ¹é•É¼°4(€€€€€€€€€€€Ñ¥Ñ±”èQ•áÐ¡•¹ÑÉä¹­•ä¹É•Á±…•±° |œ°€œ€œ¤¤°4(€€€€€€€€€€€ÑÉ…¥±¥¹œè½¹ÍÑÉ…¥¹•‘	½à 4(€€€€€€€€€€€€€½¹ÍÑÉ…¥¹ÑÌè½¹ÍÐ	½á½¹ÍÑÉ…¥¹ÑÌ¡µ…á]¥‘Ñ è€ÄäÀ¤°4(€€€€€€€€€€€€€¡¥±èQ•áÐ 4(€€€€€€€€€€€€€€€}Í…™•ÑÑÉ¥‰ÕÑ•Q•áÐ¡•¹ÑÉä¹Ù…±Õ”¤°4(€€€€€€€€€€€€€€€µ…á1¥¹•Ìè€Ì°4(€€€€€€€€€€€€€€€½Ù•É™±½ÜèQ•áÑ=Ù•É™±½Ü¹•±±¥ÁÍ¥Ì°4(€€€€€€€€€€€€€€€Ñ•áÑ±¥¸èQ•áÑ±¥¸¹•¹°4(€€€€€€€€€€€€€€¤°4(€€€€€€€€€€€€¤°4(€€€€€€€€€€¤°4(€€€€€t°4(€€€€¤ì4(€ô4(4(€ÍÑ…Ñ¥Œ½¹ÍÐM•ÐñMÑÉ¥¹œø}¡¥‘‘•¹ÑÑÉ¥‰ÕÑ•Ì€ô€ñMÑÉ¥¹œùì4(€€€€…•ÍÍ}Ñ½­•¸œ°4(€€€€…Á¥}­•äœ°4(€€€€Á…ÍÍÝ½Éœ°4(€€€€Ñ½­•¸œ°4(€ôì4(4(€ÍÑ…Ñ¥ŒMÑÉ¥¹œ}Í…™•ÑÑÉ¥‰ÕÑ•Q•áÐ¡=‰©•ÐüÙ…±Õ”¤ì4(€€€™¥¹…°Ñ•áÐ€ôÙ…±Õ”ü¹Ñ½MÑÉ¥¹œ ¤€üü€ŸŠPœì4(€€€É•ÑÕÉ¸Ñ•áÐ¹±•¹Ñ €ðô€ÈÐÀ€üÑ•áÐ€è€œ‘íÑ•áÐ¹ÍÕ‰ÍÑÉ¥¹œ À°€ÈÌÜ¥÷Š˜œì4(€ô4)ô4(4)™¥¹…°±…ÍÌ}!¥¹Ð•áÑ•¹‘ÌMÑ…Ñ•±•ÍÍ]¥‘•Ðì4(€½¹ÍÐ}!¥¹Ð¡íÉ•ÅÕ¥É•Ñ¡¥Ì¹Ñ•áÑô¤ì4(4(€™¥¹…°MÑÉ¥¹œÑ•áÐì4(4(€½Ù•ÉÉ¥‘”4(€]¥‘•Ð‰Õ¥±¡	Õ¥±‘½¹Ñ•áÐ½¹Ñ•áÐ¤€ôø½¹Ñ…¥¹•È 4(€€€Á…‘‘¥¹œè½¹ÍÐ‘•%¹Í•ÑÌ¹…±°¡AÉ½‘ÕÑMÁ…¥¹œ¹µ¤°4(€€€‘•½É…Ñ¥½¸è	½á•½É…Ñ¥½¸ 4(€€€€€½±½ÈèQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹½±½ÉM¡•µ”¹ÍÕÉ™…•½¹Ñ…¥¹•É!¥ °4(€€€€€‰½É‘•ÉI…‘¥ÕÌè	½É‘•ÉI…‘¥ÕÌ¹¥ÉÕ±…È¡AÉ½‘ÕÑI…‘¥ÕÌ¹…É¤°4(€€€€¤°4(€€€¡¥±èQ•áÐ¡Ñ•áÐ¤°4(€€¤ì4)ô4(4)™¥¹…°±…ÍÌ}!¥ÍÑ½ÉåA…¥¹Ñ•È•áÑ•¹‘ÌÕÍÑ½µA…¥¹Ñ•Èì4(€½¹ÍÐ}!¥ÍÑ½ÉåA…¥¹Ñ•È¡ì4(€€€É•ÅÕ¥É•Ñ¡¥Ì¹Á½¥¹ÑÌ°4(€€€É•ÅÕ¥É•Ñ¡¥Ì¹½±½È°4(€€€É•ÅÕ¥É•Ñ¡¥Ì¹É¥‘½±½È°4(€ô¤ì4(4(€™¥¹…°1¥ÍÐñ!¥ÍÑ½ÉåA½¥¹ÐøÁ½¥¹ÑÌì4(€™¥¹…°½±½È½±½Èì4(€™¥¹…°½±½ÈÉ¥‘½±½Èì4(4(€½Ù•ÉÉ¥‘”4(€Ù½¥Á…¥¹Ð¡…¹Ù…Ì…¹Ù…Ì°M¥é”Í¥é”¤ì4(€€€™¥¹…°Í…µÁ±•Ì€ô4(€€€€€€€Á½¥¹ÑÌ4(€€€€€€€€€€€€¹µ…À ¡Á½¥¹Ð¤ì4(€€€€€€€€€€€€€™¥¹…°Ù…±Õ”€ôÁ½¥¹Ð¹Ù…±Õ”ì4(€€€€€€€€€€€€€™¥¹…°Á…ÉÍ•€ôÙ…±Õ”¥Ì¹Õ´4(€€€€€€€€€€€€€€€€€€üÙ…±Õ”¹Ñ½½Õ‰±” ¤4(€€€€€€€€€€€€€€€€€€è‘½Õ‰±”¹ÑÉåA…ÉÍ”¡Ù…±Õ”ü¹Ñ½MÑÉ¥¹œ ¤€üü€œœ¤ì4(€€€€€€€€€€€€€É•ÑÕÉ¸Á…ÉÍ•€„ô¹Õ±°€˜˜Á…ÉÍ•¹¥Í¥¹¥Ñ”4(€€€€€€€€€€€€€€€€€€ü€¡Ñ¥µ”èÁ½¥¹Ð¹Ñ¥µ”°Ù…±Õ”èÁ…ÉÍ•¤4(€€€€€€€€€€€€€€€€€€è¹Õ±°ì4(€€€€€€€€€€€ô¤4(€€€€€€€€€€€€¹Ý¡•É•QåÁ”ð¡í…Ñ•Q¥µ”Ñ¥µ”°‘½Õ‰±”Ù…±Õ•ô¤ø ¤4(€€€€€€€€€€€€¹Ñ½1¥ÍÐ¡É½Ý…‰±”è™…±Í”¤4(€€€€€€€€€€¸¹Í½ÉÐ ¡„°ˆ¤€ôø„¹Ñ¥µ”¹½µÁ…É•Q¼¡ˆ¹Ñ¥µ”¤¤ì4(€€€¥˜€¡Í…µÁ±•Ì¹±•¹Ñ €ð€È¤É•ÑÕÉ¸ì4(€€€™¥¹…°Ù…±Õ•Ì€ôÍ…µÁ±•Ì4(€€€€€€€€¹µ…À ¡Í…µÁ±”¤€ôøÍ…µÁ±”¹Ù…±Õ”¤4(€€€€€€€€¹Ñ½1¥ÍÐ¡É½Ý…‰±”è™…±Í”¤ì4(€€€™¥¹…°µ¥¹¥µÕ´€ôÙ…±Õ•Ì¹É•‘Õ”¡µ…Ñ ¹µ¥¸¤ì4(€€€™¥¹…°µ…á¥µÕ´€ôÙ…±Õ•Ì¹É•‘Õ”¡µ…Ñ ¹µ…à¤ì4(€€€™¥¹…°É…¹”€ôµ…á¥µÕ´€ôôµ¥¹¥µÕ´€ü€Ä¸À€èµ…á¥µÕ´€´µ¥¹¥µÕ´ì4(€€€™¥¹…°É¥€ôA…¥¹Ð ¤4(€€€€€€¸¹½±½È€ôÉ¥‘½±½È4(€€€€€€¸¹ÍÑÉ½­•]¥‘Ñ €ô€Äì4(€€€™½È€¡Ù…ÈÉ½Ü€ô€ÀìÉ½Ü€ðô€ÐìÉ½Ü¬¬¤ì4(€€€€€™¥¹…°ä€ôÍ¥é”¹¡•¥¡Ð€¨É½Ü€¼€Ðì4(€€€€€…¹Ù…Ì¹‘É…Ý1¥¹”¡=™™Í•Ð À°ä¤°=™™Í•Ð¡Í¥é”¹Ý¥‘Ñ °ä¤°É¥¤ì4(€€€ô4(€€€™¥¹…°Á…Ñ €ôA…Ñ  ¤ì4(€€€™¥¹…°™¥ÉÍÑQ¥µ”€ôÍ…µÁ±•Ì¹™¥ÉÍÐ¹Ñ¥µ”¹µ¥±±¥Í•½¹‘ÍM¥¹•Á½ ì4(€€€™¥¹…°Ñ¥µ•I…¹”€ôµ…Ñ ¹µ…à 4(€€€€€€Ä°4(€€€€€Í…µÁ±•Ì¹±…ÍÐ¹Ñ¥µ”¹µ¥±±¥Í•½¹‘ÍM¥¹•Á½ €´™¥ÉÍÑQ¥µ”°4(€€€€¤ì4(€€€™½È€¡Ù…È¥¹‘•à€ô€Àì¥¹‘•à€ðÍ…µÁ±•Ì¹±•¹Ñ ì¥¹‘•à¬¬¤ì4(€€€€€™¥¹…°Í…µÁ±”€ôÍ…µÁ±•Ím¥¹‘•átì4(€€€€€™¥¹…°à€ô4(€€€€€€€€€Í¥é”¹Ý¥‘Ñ €¨4(€€€€€€€€€€¡Í…µÁ±”¹Ñ¥µ”¹µ¥±±¥Í•½¹‘ÍM¥¹•Á½ €´™¥ÉÍÑQ¥µ”¤€¼4(€€€€€€€€€Ñ¥µ•I…¹”ì4(€€€€€™¥¹…°ä€ôÍ¥é”¹¡•¥¡Ð€´€ ¡Í…µÁ±”¹Ù…±Õ”€´µ¥¹¥µÕ´¤€¼É…¹”€¨Í¥é”¹¡•¥¡Ð¤ì4(€€€€€¥˜€¡¥¹‘•à€ôô€À¤ì4(€€€€€€€Á…Ñ ¹µ½Ù•Q¼¡à°ä¤ì4(€€€€€ô•±Í”ì4(€€€€€€€Á…Ñ ¹±¥¹•Q¼¡à°ä¤ì4(€€€€€ô4(€€€ô4(€€€…¹Ù…Ì¹‘É…ÝA…Ñ  4(€€€€€Á…Ñ °4(€€€€€A…¥¹Ð ¤4(€€€€€€€€¸¹½±½È€ô½±½È4(€€€€€€€€¸¹ÍÑå±”€ôA…¥¹Ñ¥¹MÑå±”¹ÍÑÉ½­”4(€€€€€€€€¸¹ÍÑÉ½­•]¥‘Ñ €ô€Ì4(€€€€€€€€¸¹ÍÑÉ½­•…À€ôMÑÉ½­•…À¹É½Õ¹4(€€€€€€€€¸¹ÍÑÉ½­•)½¥¸€ôMÑÉ½­•)½¥¸¹É½Õ¹°4(€€€€¤ì4(€ô4(4(€½Ù•ÉÉ¥‘”4(€‰½½°Í¡½Õ±‘I•Á…¥¹Ð¡½Ù…É¥…¹Ð}!¥ÍÑ½ÉåA…¥¹Ñ•È½±‘•±•…Ñ”¤€ôø4(€€€€€½±‘•±•…Ñ”¹Á½¥¹ÑÌ€„ôÁ½¥¹ÑÌñð4(€€€€€½±‘•±•…Ñ”¹½±½È€„ô½±½Èñð4(€€€€€½±‘•±•…Ñ”¹É¥‘½±½È€„ôÉ¥‘½±½Èì4)ô4(4)™¥¹…°±…ÍÌ}!¥ÍÑ½Éå¡…ÉÐ•áÑ•¹‘ÌMÑ…Ñ•±•ÍÍ]¥‘•Ðì4(€½¹ÍÐ}!¥ÍÑ½Éå¡…ÉÐ¡íÉ•ÅÕ¥É•Ñ¡¥Ì¹Á½¥¹ÑÌ°É•ÅÕ¥É•Ñ¡¥Ì¹Õ¹¥Ñô¤ì4(4(€™¥¹…°1¥ÍÐñ!¥ÍÑ½ÉåA½¥¹ÐøÁ½¥¹ÑÌì4(€™¥¹…°MÑÉ¥¹œÕ¹¥Ðì4(4(€½Ù•ÉÉ¥‘”4(€]¥‘•Ð‰Õ¥±¡	Õ¥±‘½¹Ñ•áÐ½¹Ñ•áÐ¤ì4(€€€™¥¹…°Ù…±Õ•Ì€ôÁ½¥¹ÑÌ4(€€€€€€€€¹µ…À ¡Á½¥¹Ð¤€ôøÁ½¥¹Ð¹Ù…±Õ”¤4(€€€€€€€€¹Ý¡•É•QåÁ”ñ¹Õ´ø ¤4(€€€€€€€€¹µ…À ¡Ù…±Õ”¤€ôøÙ…±Õ”¹Ñ½½Õ‰±” ¤¤4(€€€€€€€€¹Ý¡•É” ¡Ù…±Õ”¤€ôøÙ…±Õ”¹¥Í¥¹¥Ñ”¤4(€€€€€€€€¹Ñ½1¥ÍÐ¡É½Ý…‰±”è™…±Í”¤ì4(€€€™¥¹…°µ¥¹¥µÕ´€ôÙ…±Õ•Ì¹¥ÍµÁÑä€ü€À€èÙ…±Õ•Ì¹É•‘Õ”¡µ…Ñ ¹µ¥¸¤ì4(€€€™¥¹…°µ…á¥µÕ´€ôÙ…±Õ•Ì¹¥ÍµÁÑä€ü€À€èÙ…±Õ•Ì¹É•‘Õ”¡µ…Ñ ¹µ…à¤ì4(€€€™¥¹…°ÍÕ™™¥à€ôÕ¹¥Ð¹¥ÍµÁÑä€ü€œœ€è€œ€‘Õ¹¥Ðœì4(€€€™¥¹…°±…‰•°€ô!½µ•½¹ÑÉ½±MÑÉ¥¹Ì¹½˜¡½¹Ñ•áÐ¤4(€€€€€€€€¹Ý¥Ñ¡Y…±Õ•Ì ¡¥ÍÑ½ÉåMÕµµ…Éäœ°€ñMÑÉ¥¹œ°=‰©•Ðùì4(€€€€€€€€€€µ¥¹¥µÕ´œè€œ‘íµ¥¹¥µÕ´¹Ñ½MÑÉ¥¹Í¥á• Ä¥ô‘ÍÕ™™¥àœ°4(€€€€€€€€€€µ…á¥µÕ´œè€œ‘íµ…á¥µÕ´¹Ñ½MÑÉ¥¹Í¥á• Ä¥ô‘ÍÕ™™¥àœ°4(€€€€€€€€€€Í…µÁ±•ÌœèÙ…±Õ•Ì¹±•¹Ñ °4(€€€€€€€ô¤ì4(€€€É•ÑÕÉ¸M•µ…¹Ñ¥Ì 4(€€€€€¥µ…”èÑÉÕ”°4(€€€€€±…‰•°è±…‰•°°4(€€€€€¡¥±èM¥é•‘	½à 4(€€€€€€€¡•¥¡Ðè€ÄàÀ°4(€€€€€€€¡¥±èÕÍÑ½µA…¥¹Ð 4(€€€€€€€€€Á…¥¹Ñ•Èè}!¥ÍÑ½ÉåA…¥¹Ñ•È 4(€€€€€€€€€€€Á½¥¹ÑÌèÁ½¥¹ÑÌ°4(€€€€€€€€€€€½±½ÈèQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹½±½ÉM¡•µ”¹ÁÉ¥µ…Éä°4(€€€€€€€€€€€É¥‘½±½ÈèQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹½±½ÉM¡•µ”¹½ÕÑ±¥¹•Y…É¥…¹Ð°4(€€€€€€€€€€¤°4(€€€€€€€€¤°4(€€€€€€¤°4(€€€€¤ì4(€ô4)ô4(