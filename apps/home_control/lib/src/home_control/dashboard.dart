import 'dart:ui' as ui;

import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:aquacyd_protocol/aquacyd_protocol.dart';
import 'package:flutter/material.dart';
import 'package:home_entities/home_entities.dart';

import 'controller.dart';
import 'entity_widgets.dart';
import 'strings.dart';

final class HomeDashboardPage extends StatelessWidget {
  const HomeDashboardPage({
    required this.controller,
    this.onOpenRooms,
    this.onOpenDevices,
    this.onOpenUpdates,
    this.onCustomize,
    super.key,
  });

  final HomeControlController controller;
  final VoidCallback? onOpenRooms;
  final VoidCallback? onOpenDevices;
  final VoidCallback? onOpenUpdates;
  final VoidCallback? onCustomize;

  @override
  Widget build(BuildContext context) {
    final snapshot = controller.snapshot!;
    final strings = HomeControlStrings.of(context);
    final visible = controller.dashboard.order.where(
      (id) =>
          !controller.dashboard.hidden.contains(id) &&
          _sectionIsAvailable(id, snapshot),
    );
    final attention = _AttentionModel.fromSnapshot(
      snapshot,
      strings: strings,
      controller: controller,
      onOpenDevices: onOpenDevices,
      onOpenUpdates: onOpenUpdates,
      onOpenAquarium: () => showAquariumDetails(context, controller),
    );
    return RefreshIndicator(
      onRefresh: () async => controller.refresh(announce: true),
      child: CustomScrollView(
        key: const PageStorageKey<String>('home-dashboard'),
        slivers: <Widget>[
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 18, 20, 10),
            sliver: SliverToBoxAdapter(
              child: Center(
                child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 1280),
                  child: _HomeStatusHeader(
                    snapshot: snapshot,
                    attentionCount: attention.items.length,
                  ),
                ),
              ),
            ),
          ),
          if (attention.items.isNotEmpty)
            SliverPadding(
              padding: const EdgeInsets.fromLTRB(20, 6, 20, 10),
              sliver: SliverToBoxAdapter(
                child: Center(
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: 1280),
                    child: _AttentionCenter(model: attention),
                  ),
                ),
              ),
            ),
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 10, 20, 10),
            sliver: SliverToBoxAdapter(
              child: Center(
                child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 1280),
                  child: _DashboardSections(
                    sections: visible.toList(growable: false),
                    snapshot: snapshot,
                    controller: controller,
                    onOpenRooms: onOpenRooms,
                    onOpenDevices: onOpenDevices,
                    onCustomize: onCustomize,
                  ),
                ),
              ),
            ),
          ),
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 14, 20, 104),
            sliver: SliverToBoxAdapter(
              child: Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: <Widget>[
                  Icon(
                    Icons.lock_outline_rounded,
                    size: 15,
                    color: Theme.of(context).colorScheme.onSurfaceVariant,
                  ),
                  const SizedBox(width: 6),
                  Flexible(
                    child: Text(
                      strings.withValue(
                        'lastSync',
                        strings.relativeTime(snapshot.synchronizedAt),
                      ),
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: Theme.of(context).colorScheme.onSurfaceVariant,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  static bool _sectionIsAvailable(String id, HomeSnapshot snapshot) =>
      switch (id) {
        'aquarium' => AquariumOverview.fromSnapshot(snapshot).present,
        _ => true,
      };
}

final class _HomeStatusHeader extends StatelessWidget {
  const _HomeStatusHeader({
    required this.snapshot,
    required this.attentionCount,
  });

  final HomeSnapshot snapshot;
  final int attentionCount;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final scheme = Theme.of(context).colorScheme;
    final dark = Theme.of(context).brightness == Brightness.dark;
    final online = snapshot.devices.where((device) => device.available).length;
    final enabledAutomations = snapshot.automations
        .where((automation) => automation.enabled)
        .length;
    final updates = snapshot.updates
        .where(
          (update) =>
              update.phase == HomeUpdatePhase.available || update.mandatory,
        )
        .length;
    final critical = snapshot.isOffline;
    final needsAttention = critical || attentionCount > 0;
    final accent = critical
        ? scheme.error
        : needsAttention
        ? scheme.tertiary
        : scheme.primary;
    final statusKey = critical
        ? 'homeOfflineTitle'
        : needsAttention
        ? 'homeAttentionTitle'
        : 'homeHealthyTitle';
    final statusDescription = critical
        ? strings.t('homeOfflineDescription')
        : needsAttention
        ? strings.withValue('homeAttentionDescription', attentionCount)
        : strings.t('homeHealthyDescription');

    return Semantics(
      container: true,
      label: '${strings.t(statusKey)}. $statusDescription',
      child: Container(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
            colors: <Color>[
              scheme.surfaceContainerLow,
              Color.alphaBlend(
                accent.withValues(alpha: dark ? 0.11 : 0.07),
                scheme.surfaceContainerLow,
              ),
            ],
          ),
          borderRadius: BorderRadius.circular(ProductRadius.hero),
          border: Border.all(
            color: accent.withValues(alpha: dark ? 0.24 : 0.16),
          ),
          boxShadow: <BoxShadow>[
            BoxShadow(
              color: scheme.shadow.withValues(alpha: dark ? 0.16 : 0.06),
              blurRadius: 26,
              offset: const Offset(0, 10),
            ),
          ],
        ),
        child: Padding(
          padding: const EdgeInsets.all(ProductSpacing.lg),
          child: LayoutBuilder(
            builder: (context, constraints) {
              final wide = constraints.maxWidth >= 580;
              final introduction = _StatusIntroduction(
                snapshot: snapshot,
                statusKey: statusKey,
                description: statusDescription,
                accent: accent,
              );
              final metrics = _HeaderMetrics(
                online: online,
                devices: snapshot.devices.length,
                automations: enabledAutomations,
                updates: updates,
              );
              if (!wide) {
                return Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: <Widget>[
                    introduction,
                    const SizedBox(height: ProductSpacing.lg),
                    metrics,
                  ],
                );
              }
              return Row(
                crossAxisAlignment: CrossAxisAlignment.center,
                children: <Widget>[
                  Expanded(flex: 7, child: introduction),
                  const SizedBox(width: ProductSpacing.xl),
                  Expanded(flex: 5, child: metrics),
                ],
              );
            },
          ),
        ),
      ),
    );
  }
}

final class _StatusIntroduction extends StatelessWidget {
  const _StatusIntroduction({
    required this.snapshot,
    required this.statusKey,
    required this.description,
    required this.accent,
  });

  final HomeSnapshot snapshot;
  final String statusKey;
  final String description;
  final Color accent;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final theme = Theme.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: <Widget>[
        Wrap(
          spacing: ProductSpacing.xs,
          runSpacing: ProductSpacing.xs,
          crossAxisAlignment: WrapCrossAlignment.center,
          children: <Widget>[
            _StatusPill(
              label: snapshot.isOffline
                  ? strings.t('offline')
                  : strings.t('liveStatus'),
              color: accent,
              icon: snapshot.isOffline ? Icons.cloud_off_rounded : Icons.circle,
            ),
            Text(
              snapshot.sourceName,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: theme.textTheme.labelMedium?.copyWith(
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ],
        ),
        const SizedBox(height: ProductSpacing.sm),
        Text(
          strings.t(statusKey),
          maxLines: 2,
          overflow: TextOverflow.ellipsis,
          style: theme.textTheme.headlineMedium?.copyWith(
            fontWeight: FontWeight.w800,
            letterSpacing: -0.8,
          ),
        ),
        const SizedBox(height: ProductSpacing.xs),
        Text(
          description,
          maxLines: 3,
          overflow: TextOverflow.ellipsis,
          style: theme.textTheme.bodyMedium?.copyWith(
            color: theme.colorScheme.onSurfaceVariant,
            height: 1.45,
          ),
        ),
      ],
    );
  }
}

final class _HeaderMetrics extends StatelessWidget {
  const _HeaderMetrics({
    required this.online,
    required this.devices,
    required this.automations,
    required this.updates,
  });

  final int online;
  final int devices;
  final int automations;
  final int updates;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final metrics = <Widget>[
      _HeaderMetric(
        icon: Icons.devices_other_rounded,
        value: '$online/$devices',
        label: strings.t('devicesOnlineLabel'),
      ),
      _HeaderMetric(
        icon: Icons.auto_awesome_motion_rounded,
        value: '$automations',
        label: strings.t('automationsActiveLabel'),
      ),
      _HeaderMetric(
        icon: updates > 0
            ? Icons.system_update_rounded
            : Icons.verified_rounded,
        value: '$updates',
        label: strings.t('updatesAvailableLabel'),
      ),
    ];
    return LayoutBuilder(
      builder: (context, constraints) {
        final textScale = MediaQuery.textScalerOf(context).scale(1);
        final stack = textScale > 1.35;
        if (stack) {
          return Column(
            children: <Widget>[
              for (var index = 0; index < metrics.length; index++) ...<Widget>[
                SizedBox(width: double.infinity, child: metrics[index]),
                if (index != metrics.length - 1)
                  const SizedBox(height: ProductSpacing.xs),
              ],
            ],
          );
        }
        return IntrinsicHeight(
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: <Widget>[
              for (var index = 0; index < metrics.length; index++) ...<Widget>[
                Expanded(child: metrics[index]),
                if (index != metrics.length - 1)
                  const SizedBox(width: ProductSpacing.xs),
              ],
            ],
          ),
        );
      },
    );
  }
}

final class _HeaderMetric extends StatelessWidget {
  const _HeaderMetric({
    required this.icon,
    required this.value,
    required this.label,
  });

  final IconData icon;
  final String value;
  final String label;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      constraints: const BoxConstraints(minHeight: 86),
      padding: const EdgeInsets.all(ProductSpacing.sm),
      decoration: BoxDecoration(
        color: scheme.surface.withValues(alpha: 0.66),
        borderRadius: BorderRadius.circular(ProductRadius.control),
        border: Border.all(
          color: scheme.outlineVariant.withValues(alpha: 0.45),
        ),
      ),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          Wrap(
            spacing: 6,
            runSpacing: 2,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: <Widget>[
              Icon(icon, size: 17, color: scheme.primary),
              Text(
                value,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: Theme.of(context).textTheme.titleLarge?.copyWith(
                  fontWeight: FontWeight.w800,
                  fontFeatures: const <ui.FontFeature>[
                    ui.FontFeature.tabularFigures(),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 4),
          Text(
            label,
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
            style: Theme.of(context).textTheme.labelSmall?.copyWith(
              color: scheme.onSurfaceVariant,
              height: 1.15,
            ),
          ),
        ],
      ),
    );
  }
}

final class _StatusPill extends StatelessWidget {
  const _StatusPill({
    required this.label,
    required this.color,
    required this.icon,
  });

  final String label;
  final Color color;
  final IconData icon;

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
    decoration: BoxDecoration(
      color: color.withValues(alpha: 0.13),
      borderRadius: BorderRadius.circular(999),
      border: Border.all(color: color.withValues(alpha: 0.25)),
    ),
    child: Row(
      mainAxisSize: MainAxisSize.min,
      children: <Widget>[
        Icon(icon, size: icon == Icons.circle ? 8 : 15, color: color),
        const SizedBox(width: 7),
        Text(
          label,
          style: Theme.of(context).textTheme.labelMedium?.copyWith(
            color: color,
            fontWeight: FontWeight.w700,
          ),
        ),
      ],
    ),
  );
}

final class _AttentionModel {
  const _AttentionModel(this.items);

  factory _AttentionModel.fromSnapshot(
    HomeSnapshot snapshot, {
    required HomeControlStrings strings,
    required HomeControlController controller,
    required VoidCallback? onOpenDevices,
    required VoidCallback? onOpenUpdates,
    required VoidCallback onOpenAquarium,
  }) {
    final items = <_AttentionItem>[];
    if (snapshot.isOffline) {
      items.add(
        _AttentionItem(
          icon: Icons.cloud_off_rounded,
          title: strings.t('attentionOfflineTitle'),
          description: strings.t('attentionOfflineDescription'),
          action: controller.refreshing
              ? null
              : () => controller.refresh(announce: true),
        ),
      );
    } else if (snapshot.isStale || snapshot.isPartial) {
      items.add(
        _AttentionItem(
          icon: Icons.sync_problem_rounded,
          title: strings.t('attentionSyncTitle'),
          description: strings.t('attentionSyncDescription'),
          action: controller.refreshing
              ? null
              : () => controller.refresh(announce: true),
        ),
      );
    }
    final aquarium = AquariumOverview.fromSnapshot(snapshot);
    if (aquarium.present && aquarium.hasAlarm) {
      items.add(
        _AttentionItem(
          icon: Icons.water_drop_rounded,
          title: strings.t('attentionAquariumTitle'),
          description: strings.t('attentionAquariumDescription'),
          action: onOpenAquarium,
        ),
      );
    }
    final unavailable = snapshot.devices
        .where((device) => !device.available)
        .length;
    if (unavailable > 0) {
      items.add(
        _AttentionItem(
          icon: Icons.portable_wifi_off_rounded,
          title: strings.withValue('attentionDevicesTitle', unavailable),
          description: strings.t('attentionDevicesDescription'),
          action: onOpenDevices,
        ),
      );
    }
    final updates = snapshot.updates
        .where(
          (update) =>
              update.phase == HomeUpdatePhase.available || update.mandatory,
        )
        .length;
    if (updates > 0) {
      items.add(
        _AttentionItem(
          icon: Icons.system_update_alt_rounded,
          title: strings.withValue('attentionUpdatesTitle', updates),
          description: strings.t('attentionUpdatesDescription'),
          action: onOpenUpdates,
        ),
      );
    }
    return _AttentionModel(List<_AttentionItem>.unmodifiable(items));
  }

  final List<_AttentionItem> items;
}

final class _AttentionItem {
  const _AttentionItem({
    required this.icon,
    required this.title,
    required this.description,
    required this.action,
  });

  final IconData icon;
  final String title;
  final String description;
  final VoidCallback? action;
}

final class _AttentionCenter extends StatelessWidget {
  const _AttentionCenter({required this.model});

  final _AttentionModel model;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final scheme = Theme.of(context).colorScheme;
    return _SectionSurface(
      title: strings.t('attentionCenter'),
      icon: Icons.notifications_active_rounded,
      accent: scheme.tertiary,
      badge: '${model.items.length}',
      child: LayoutBuilder(
        builder: (context, constraints) {
          final columns = constraints.maxWidth >= 760 ? 2 : 1;
          final width = columns == 1
              ? constraints.maxWidth
              : (constraints.maxWidth - ProductSpacing.sm) / 2;
          return Wrap(
            spacing: ProductSpacing.sm,
            runSpacing: ProductSpacing.sm,
            children: <Widget>[
              for (final item in model.items.take(4))
                SizedBox(
                  width: width,
                  child: _AttentionTile(item: item),
                ),
            ],
          );
        },
      ),
    );
  }
}

final class _AttentionTile extends StatelessWidget {
  const _AttentionTile({required this.item});

  final _AttentionItem item;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Material(
      color: scheme.tertiaryContainer.withValues(alpha: 0.4),
      borderRadius: BorderRadius.circular(ProductRadius.control),
      child: InkWell(
        onTap: item.action,
        borderRadius: BorderRadius.circular(ProductRadius.control),
        child: Padding(
          padding: const EdgeInsets.all(ProductSpacing.sm),
          child: Row(
            children: <Widget>[
              Container(
                width: 44,
                height: 44,
                decoration: BoxDecoration(
                  color: scheme.tertiaryContainer,
                  borderRadius: BorderRadius.circular(ProductRadius.control),
                ),
                child: Icon(item.icon, color: scheme.onTertiaryContainer),
              ),
              const SizedBox(width: ProductSpacing.sm),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: <Widget>[
                    Text(
                      item.title,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: Theme.of(context).textTheme.titleSmall,
                    ),
                    const SizedBox(height: 2),
                    Text(
                      item.description,
                      maxLines: 2,
                      overflow: TextOverflow.ellipsis,
                      style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: scheme.onSurfaceVariant,
                      ),
                    ),
                  ],
                ),
              ),
              if (item.action != null)
                const Padding(
                  padding: EdgeInsets.only(left: ProductSpacing.xs),
                  child: Icon(Icons.chevron_right_rounded),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

enum _AquariumHealth { ok, alarm, offline, incomplete, stale }

final class AquariumDashboardCard extends StatelessWidget {
  const AquariumDashboardCard({
    required this.snapshot,
    required this.controller,
    this.interactive = true,
    super.key,
  });

  final HomeSnapshot snapshot;
  final HomeControlController controller;
  final bool interactive;

  @override
  Widget build(BuildContext context) {
    final overview = AquariumOverview.fromSnapshot(snapshot);
    final strings = HomeControlStrings.of(context);
    if (!overview.present) {
      return _SectionSurface(
        title: strings.t('aquarium'),
        icon: Icons.water_rounded,
        child: _PremiumEmptyState(
          icon: Icons.water_drop_outlined,
          title: strings.t('noAquariumTitle'),
          description: strings.t('noAquarium'),
        ),
      );
    }
    final temperature = overview.byRole(AquariumSemanticRole.temperature);
    final ph = overview.byRole(AquariumSemanticRole.ph);
    final ec = overview.byRole(AquariumSemanticRole.ec);
    final health = _aquariumHealth(overview, temperature);
    final status = strings.t(switch (health) {
      _AquariumHealth.ok => 'aquariumNoAlarms',
      _AquariumHealth.alarm => 'aquariumAlarm',
      _AquariumHealth.offline => 'aquariumOffline',
      _AquariumHealth.incomplete => 'aquariumIncomplete',
      _AquariumHealth.stale => 'aquariumStale',
    });
    final alarm = health == _AquariumHealth.alarm;
    final baseColors = alarm
        ? const <Color>[Color(0xFF5D1F2D), Color(0xFF2A121A)]
        : const <Color>[Color(0xFF0B4B59), Color(0xFF071E28)];
    return Semantics(
      container: true,
      button: interactive,
      label: '${strings.t('aquarium')}. $status',
      child: Material(
        color: Colors.transparent,
        borderRadius: BorderRadius.circular(ProductRadius.hero),
        clipBehavior: Clip.antiAlias,
        child: Ink(
          decoration: BoxDecoration(
            gradient: LinearGradient(
              begin: Alignment.topLeft,
              end: Alignment.bottomRight,
              colors: baseColors,
            ),
            borderRadius: BorderRadius.circular(ProductRadius.hero),
            border: Border.all(color: Colors.white.withValues(alpha: 0.09)),
            boxShadow: <BoxShadow>[
              BoxShadow(
                color: baseColors.first.withValues(alpha: 0.22),
                blurRadius: 30,
                offset: const Offset(0, 12),
              ),
            ],
          ),
          child: InkWell(
            onTap: interactive
                ? () => showAquariumDetails(context, controller)
                : null,
            borderRadius: BorderRadius.circular(ProductRadius.hero),
            child: Padding(
              padding: const EdgeInsets.all(ProductSpacing.lg),
              child: LayoutBuilder(
                builder: (context, constraints) {
                  final wide = constraints.maxWidth >= 560;
                  return wide
                      ? _WideAquariumContent(
                          temperature: temperature,
                          ph: ph,
                          ec: ec,
                          health: health,
                          status: status,
                          interactive: interactive,
                        )
                      : _CompactAquariumContent(
                          temperature: temperature,
                          ph: ph,
                          ec: ec,
                          health: health,
                          status: status,
                          interactive: interactive,
                        );
                },
              ),
            ),
          ),
        ),
      ),
    );
  }

  _AquariumHealth _aquariumHealth(
    AquariumOverview overview,
    HomeEntity? temperature,
  ) {
    if (overview.hasAlarm) return _AquariumHealth.alarm;
    if (snapshot.isOffline) return _AquariumHealth.offline;
    if (snapshot.isStale) return _AquariumHealth.stale;
    if (snapshot.isPartial || temperature == null || !temperature.available) {
      return _AquariumHealth.incomplete;
    }
    return _AquariumHealth.ok;
  }
}

final class _WideAquariumContent extends StatelessWidget {
  const _WideAquariumContent({
    required this.temperature,
    required this.ph,
    required this.ec,
    required this.health,
    required this.status,
    required this.interactive,
  });

  final HomeEntity? temperature;
  final HomeEntity? ph;
  final HomeEntity? ec;
  final _AquariumHealth health;
  final String status;
  final bool interactive;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    return Row(
      children: <Widget>[
        Expanded(
          flex: 5,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisSize: MainAxisSize.min,
            children: <Widget>[
              _AquariumHeading(
                health: health,
                status: status,
                interactive: interactive,
              ),
              const SizedBox(height: ProductSpacing.md),
              Text(
                temperature == null
                    ? strings.t('noData')
                    : strings.entityState(temperature!),
                style: Theme.of(context).textTheme.displaySmall?.copyWith(
                  color: Colors.white,
                  fontWeight: FontWeight.w700,
                  letterSpacing: -1.5,
                  fontFeatures: const <ui.FontFeature>[
                    ui.FontFeature.tabularFigures(),
                  ],
                ),
              ),
              Text(
                strings.t('waterTemperature'),
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                  color: Colors.white.withValues(alpha: 0.68),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(width: ProductSpacing.lg),
        Expanded(
          flex: 4,
          child: Row(
            children: <Widget>[
              Expanded(
                child: _AquariumMetric(
                  label: 'pH',
                  value: ph == null
                      ? strings.t('noData')
                      : strings.entityState(ph!),
                ),
              ),
              const SizedBox(width: ProductSpacing.sm),
              Expanded(
                child: _AquariumMetric(
                  label: 'EC',
                  value: ec == null
                      ? strings.t('noData')
                      : strings.entityState(ec!),
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

final class _CompactAquariumContent extends StatelessWidget {
  const _CompactAquariumContent({
    required this.temperature,
    required this.ph,
    required this.ec,
    required this.health,
    required this.status,
    required this.interactive,
  });

  final HomeEntity? temperature;
  final HomeEntity? ph;
  final HomeEntity? ec;
  final _AquariumHealth health;
  final String status;
  final bool interactive;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: <Widget>[
        _AquariumHeading(
          health: health,
          status: status,
          interactive: interactive,
        ),
        const SizedBox(height: ProductSpacing.lg),
        Row(
          crossAxisAlignment: CrossAxisAlignment.end,
          children: <Widget>[
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: <Widget>[
                  Text(
                    temperature == null
                        ? strings.t('noData')
                        : strings.entityState(temperature!),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: Theme.of(context).textTheme.headlineLarge?.copyWith(
                      color: Colors.white,
                      fontWeight: FontWeight.w700,
                      letterSpacing: -1.2,
                      fontFeatures: const <ui.FontFeature>[
                        ui.FontFeature.tabularFigures(),
                      ],
                    ),
                  ),
                  Text(
                    strings.t('waterTemperature'),
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: Colors.white.withValues(alpha: 0.68),
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(width: ProductSpacing.sm),
            _CompactReading(
              label: 'pH',
              value: ph == null
                  ? strings.t('noData')
                  : strings.entityState(ph!),
            ),
            const SizedBox(width: ProductSpacing.xs),
            _CompactReading(
              label: 'EC',
              value: ec == null
                  ? strings.t('noData')
                  : strings.entityState(ec!),
            ),
          ],
        ),
      ],
    );
  }
}

final class _AquariumHeading extends StatelessWidget {
  const _AquariumHeading({
    required this.health,
    required this.status,
    required this.interactive,
  });

  final _AquariumHealth health;
  final String status;
  final bool interactive;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final alert = health == _AquariumHealth.alarm;
    return Row(
      children: <Widget>[
        Container(
          width: 46,
          height: 46,
          decoration: BoxDecoration(
            color: Colors.white.withValues(alpha: 0.12),
            borderRadius: BorderRadius.circular(ProductRadius.control),
            border: Border.all(color: Colors.white.withValues(alpha: 0.12)),
          ),
          child: Icon(
            alert ? Icons.warning_rounded : Icons.water_rounded,
            color: Colors.white,
          ),
        ),
        const SizedBox(width: ProductSpacing.sm),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: <Widget>[
              Text(
                strings.t('aquarium'),
                style: Theme.of(context).textTheme.titleLarge?.copyWith(
                  color: Colors.white,
                  fontWeight: FontWeight.w700,
                ),
              ),
              Text(
                status,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                  color: alert
                      ? const Color(0xFFFFC1CA)
                      : Colors.white.withValues(alpha: 0.68),
                ),
              ),
            ],
          ),
        ),
        if (interactive)
          Container(
            width: 42,
            height: 42,
            decoration: BoxDecoration(
              color: Colors.white.withValues(alpha: 0.1),
              shape: BoxShape.circle,
            ),
            child: const Icon(Icons.arrow_forward_rounded, color: Colors.white),
          ),
      ],
    );
  }
}

final class _AquariumMetric extends StatelessWidget {
  const _AquariumMetric({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) => Container(
    constraints: const BoxConstraints(minHeight: 88),
    padding: const EdgeInsets.all(ProductSpacing.md),
    decoration: BoxDecoration(
      color: Colors.white.withValues(alpha: 0.08),
      borderRadius: BorderRadius.circular(ProductRadius.control),
      border: Border.all(color: Colors.white.withValues(alpha: 0.1)),
    ),
    child: Column(
      mainAxisAlignment: MainAxisAlignment.center,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: <Widget>[
        Text(
          label,
          style: Theme.of(context).textTheme.labelMedium?.copyWith(
            color: Colors.white.withValues(alpha: 0.62),
          ),
        ),
        const SizedBox(height: 4),
        Text(
          value,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: Theme.of(context).textTheme.titleLarge?.copyWith(
            color: Colors.white,
            fontWeight: FontWeight.w700,
            fontFeatures: const <ui.FontFeature>[
              ui.FontFeature.tabularFigures(),
            ],
          ),
        ),
      ],
    ),
  );
}

final class _CompactReading extends StatelessWidget {
  const _CompactReading({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) => Container(
    width: 68,
    padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 9),
    decoration: BoxDecoration(
      color: Colors.white.withValues(alpha: 0.08),
      borderRadius: BorderRadius.circular(ProductRadius.control),
      border: Border.all(color: Colors.white.withValues(alpha: 0.1)),
    ),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: <Widget>[
        Text(
          label,
          style: Theme.of(context).textTheme.labelSmall?.copyWith(
            color: Colors.white.withValues(alpha: 0.62),
          ),
        ),
        Text(
          value,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: Theme.of(context).textTheme.titleSmall?.copyWith(
            color: Colors.white,
            fontWeight: FontWeight.w700,
            fontFeatures: const <ui.FontFeature>[
              ui.FontFeature.tabularFigures(),
            ],
          ),
        ),
      ],
    ),
  );
}

final class _FavoritesSection extends StatelessWidget {
  const _FavoritesSection({
    required this.snapshot,
    required this.controller,
    required this.onOpenDevices,
    required this.onCustomize,
  });

  final HomeSnapshot snapshot;
  final HomeControlController controller;
  final VoidCallback? onOpenDevices;
  final VoidCallback? onCustomize;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final favorites = snapshot.entities
        .where(
          (entity) => controller.dashboard.favorites.contains(entity.id.value),
        )
        .toList(growable: false);
    return _SectionSurface(
      title: strings.t('quickControls'),
      subtitle: strings.t('quickControlsDescription'),
      icon: Icons.bolt_rounded,
      actionLabel: strings.t('customize'),
      onAction: onCustomize,
      child: favorites.isEmpty
          ? _PremiumEmptyState(
              icon: Icons.touch_app_rounded,
              title: strings.t('noQuickControlsTitle'),
              description: strings.t('noFavorites'),
              actionLabel: strings.t('chooseDevices'),
              onAction: onOpenDevices,
            )
          : _ResponsiveEntityGrid(entities: favorites, controller: controller),
    );
  }
}

final class _AreasSection extends StatelessWidget {
  const _AreasSection({required this.snapshot, required this.onOpenRooms});

  final HomeSnapshot snapshot;
  final VoidCallback? onOpenRooms;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final areas = snapshot.areas
        .where((area) => snapshot.entitiesForArea(area.id).isNotEmpty)
        .toList(growable: false);
    return _SectionSurface(
      title: strings.t('areasOverview'),
      subtitle: strings.t('areasOverviewDescription'),
      icon: Icons.grid_view_rounded,
      actionLabel: strings.t('seeAll'),
      onAction: onOpenRooms,
      child: areas.isEmpty
          ? _PremiumEmptyState(
              icon: Icons.meeting_room_outlined,
              title: strings.t('noRoomsTitle'),
              description: strings.t('noAreas'),
            )
          : LayoutBuilder(
              builder: (context, constraints) {
                final columns = constraints.maxWidth >= 960
                    ? 4
                    : constraints.maxWidth >= 600
                    ? 3
                    : constraints.maxWidth >= 340
                    ? 2
                    : 1;
                final width =
                    (constraints.maxWidth - ProductSpacing.sm * (columns - 1)) /
                    columns;
                return Wrap(
                  spacing: ProductSpacing.sm,
                  runSpacing: ProductSpacing.sm,
                  children: <Widget>[
                    for (final area in areas.take(8))
                      SizedBox(
                        width: width,
                        child: _AreaSummary(
                          area: area,
                          snapshot: snapshot,
                          onTap: onOpenRooms,
                        ),
                      ),
                  ],
                );
              },
            ),
    );
  }
}

final class _AreaSummary extends StatelessWidget {
  const _AreaSummary({
    required this.area,
    required this.snapshot,
    required this.onTap,
  });

  final HomeArea area;
  final HomeSnapshot snapshot;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final scheme = Theme.of(context).colorScheme;
    final entities = snapshot.entitiesForArea(area.id);
    final active = entities
        .where((entity) => entity.booleanValue == true)
        .length;
    final unavailable = entities.where((entity) => !entity.available).length;
    final accent = unavailable > 0 ? scheme.tertiary : scheme.primary;
    return Semantics(
      button: onTap != null,
      label:
          '${area.name}, ${strings.withValue('itemsCount', entities.length)}',
      child: Material(
        color: scheme.surfaceContainer,
        borderRadius: BorderRadius.circular(ProductRadius.card),
        child: InkWell(
          onTap: onTap,
          borderRadius: BorderRadius.circular(ProductRadius.card),
          child: Padding(
            padding: const EdgeInsets.all(ProductSpacing.md),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                Row(
                  children: <Widget>[
                    Container(
                      width: 42,
                      height: 42,
                      decoration: BoxDecoration(
                        color: accent.withValues(alpha: 0.12),
                        borderRadius: BorderRadius.circular(
                          ProductRadius.control,
                        ),
                      ),
                      child: Icon(
                        Icons.meeting_room_rounded,
                        size: 22,
                        color: accent,
                      ),
                    ),
                    const Spacer(),
                    Container(
                      width: 8,
                      height: 8,
                      decoration: BoxDecoration(
                        color: accent,
                        shape: BoxShape.circle,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: ProductSpacing.sm),
                Text(
                  area.name,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.w700,
                  ),
                ),
                const SizedBox(height: 3),
                Text(
                  active > 0
                      ? strings.withValue('activeNow', active)
                      : strings.withValue('itemsCount', entities.length),
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: scheme.onSurfaceVariant,
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

final class _DashboardSections extends StatelessWidget {
  const _DashboardSections({
    required this.sections,
    required this.snapshot,
    required this.controller,
    required this.onOpenRooms,
    required this.onOpenDevices,
    required this.onCustomize,
  });

  final List<String> sections;
  final HomeSnapshot snapshot;
  final HomeControlController controller;
  final VoidCallback? onOpenRooms;
  final VoidCallback? onOpenDevices;
  final VoidCallback? onCustomize;

  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, constraints) {
      final twoColumns = constraints.maxWidth >= 600;
      final compactWidth = twoColumns
          ? (constraints.maxWidth - ProductSpacing.md) / 2
          : constraints.maxWidth;
      return Wrap(
        spacing: ProductSpacing.md,
        runSpacing: ProductSpacing.lg,
        children: <Widget>[
          for (final section in sections)
            SizedBox(
              key: ValueKey<String>(
                'dashboard-section-$section-${controller.dashboard.largeCards.contains(section) ? 'large' : 'compact'}',
              ),
              width:
                  !twoColumns ||
                      controller.dashboard.largeCards.contains(section)
                  ? constraints.maxWidth
                  : compactWidth,
              child: switch (section) {
                'aquarium' => AquariumDashboardCard(
                  snapshot: snapshot,
                  controller: controller,
                ),
                'favorites' => _FavoritesSection(
                  snapshot: snapshot,
                  controller: controller,
                  onOpenDevices: onOpenDevices,
                  onCustomize: onCustomize,
                ),
                'areas' => _AreasSection(
                  snapshot: snapshot,
                  onOpenRooms: onOpenRooms,
                ),
                'activity' => _ActivitySection(snapshot: snapshot),
                _ => const SizedBox.shrink(),
              },
            ),
        ],
      );
    },
  );
}

final class _ActivitySection extends StatelessWidget {
  const _ActivitySection({required this.snapshot});

  final HomeSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final recent =
        snapshot.entities
            .where(
              (entity) => entity.changedAt != null || entity.updatedAt != null,
            )
            .toList()
          ..sort((a, b) {
            final left = a.changedAt ?? a.updatedAt!;
            final right = b.changedAt ?? b.updatedAt!;
            return right.compareTo(left);
          });
    return _SectionSurface(
      title: strings.t('recentChanges'),
      subtitle: strings.t('recentChangesDescription'),
      icon: Icons.history_toggle_off_rounded,
      child: recent.isEmpty
          ? _PremiumEmptyState(
              icon: Icons.history_rounded,
              title: strings.t('noRecentChangesTitle'),
              description: strings.t('noRecentChanges'),
            )
          : Column(
              children: <Widget>[
                for (var index = 0; index < recent.take(5).length; index++)
                  _ActivityRow(
                    entity: recent[index],
                    last: index == recent.take(5).length - 1,
                  ),
              ],
            ),
    );
  }
}

final class _ActivityRow extends StatelessWidget {
  const _ActivityRow({required this.entity, required this.last});

  final HomeEntity entity;
  final bool last;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final scheme = Theme.of(context).colorScheme;
    final changedAt = entity.changedAt ?? entity.updatedAt;
    final textScale = MediaQuery.textScalerOf(context).scale(1);
    final trailingTime = Text(
      strings.relativeTime(changedAt),
      maxLines: 1,
      overflow: TextOverflow.ellipsis,
      style: Theme.of(
        context,
      ).textTheme.labelSmall?.copyWith(color: scheme.onSurfaceVariant),
    );
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: <Widget>[
        SizedBox(
          width: 28,
          child: Column(
            children: <Widget>[
              Container(
                width: 10,
                height: 10,
                margin: const EdgeInsets.only(top: 18),
                decoration: BoxDecoration(
                  color: entity.available ? scheme.primary : scheme.error,
                  shape: BoxShape.circle,
                  border: Border.all(color: scheme.surface, width: 2),
                ),
              ),
              if (!last)
                Container(width: 1, height: 46, color: scheme.outlineVariant),
            ],
          ),
        ),
        const SizedBox(width: ProductSpacing.xs),
        Expanded(
          child: Padding(
            padding: const EdgeInsets.symmetric(vertical: 10),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                Container(
                  width: 40,
                  height: 40,
                  decoration: BoxDecoration(
                    color: scheme.surfaceContainerHighest,
                    borderRadius: BorderRadius.circular(ProductRadius.control),
                  ),
                  child: Icon(iconForEntity(entity.type), size: 21),
                ),
                const SizedBox(width: ProductSpacing.sm),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: <Widget>[
                      Text(
                        entity.name,
                        maxLines: textScale > 1.5 ? 2 : 1,
                        overflow: TextOverflow.ellipsis,
                        style: Theme.of(context).textTheme.titleSmall,
                      ),
                      Text(
                        strings.entityState(entity),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: Theme.of(context).textTheme.bodySmall?.copyWith(
                          color: scheme.onSurfaceVariant,
                        ),
                      ),
                      if (textScale > 1.35) ...<Widget>[
                        const SizedBox(height: 2),
                        trailingTime,
                      ],
                    ],
                  ),
                ),
                if (textScale <= 1.35) ...<Widget>[
                  const SizedBox(width: ProductSpacing.xs),
                  Flexible(child: trailingTime),
                ],
              ],
            ),
          ),
        ),
      ],
    );
  }
}

final class _SectionSurface extends StatelessWidget {
  const _SectionSurface({
    required this.title,
    required this.icon,
    required this.child,
    this.subtitle,
    this.actionLabel,
    this.onAction,
    this.accent,
    this.badge,
  });

  final String title;
  final String? subtitle;
  final IconData icon;
  final Widget child;
  final String? actionLabel;
  final VoidCallback? onAction;
  final Color? accent;
  final String? badge;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final resolvedAccent = accent ?? scheme.primary;
    return Container(
      padding: const EdgeInsets.all(ProductSpacing.lg),
      decoration: BoxDecoration(
        color: scheme.surfaceContainerLow,
        borderRadius: BorderRadius.circular(ProductRadius.hero),
        border: Border.all(
          color: scheme.outlineVariant.withValues(alpha: 0.55),
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          LayoutBuilder(
            builder: (context, constraints) {
              final textScale = MediaQuery.textScalerOf(context).scale(1);
              final showLabel = constraints.maxWidth >= 460 && textScale <= 1.3;
              return Row(
                children: <Widget>[
                  Container(
                    width: 44,
                    height: 44,
                    decoration: BoxDecoration(
                      color: resolvedAccent.withValues(alpha: 0.12),
                      borderRadius: BorderRadius.circular(
                        ProductRadius.control,
                      ),
                    ),
                    child: Icon(icon, size: 22, color: resolvedAccent),
                  ),
                  const SizedBox(width: ProductSpacing.sm),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: <Widget>[
                        Row(
                          children: <Widget>[
                            Flexible(
                              child: Text(
                                title,
                                maxLines: textScale > 1.3 ? 2 : 1,
                                overflow: TextOverflow.ellipsis,
                                style: Theme.of(context).textTheme.titleLarge
                                    ?.copyWith(fontWeight: FontWeight.w700),
                              ),
                            ),
                            if (badge != null) ...<Widget>[
                              const SizedBox(width: ProductSpacing.xs),
                              Container(
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 8,
                                  vertical: 3,
                                ),
                                decoration: BoxDecoration(
                                  color: resolvedAccent.withValues(alpha: 0.13),
                                  borderRadius: BorderRadius.circular(999),
                                ),
                                child: Text(
                                  badge!,
                                  style: Theme.of(context).textTheme.labelSmall
                                      ?.copyWith(
                                        color: resolvedAccent,
                                        fontWeight: FontWeight.w800,
                                      ),
                                ),
                              ),
                            ],
                          ],
                        ),
                        if (subtitle != null)
                          Text(
                            subtitle!,
                            maxLines: 2,
                            overflow: TextOverflow.ellipsis,
                            style: Theme.of(context).textTheme.bodySmall
                                ?.copyWith(color: scheme.onSurfaceVariant),
                          ),
                      ],
                    ),
                  ),
                  if (onAction != null)
                    showLabel
                        ? TextButton.icon(
                            onPressed: onAction,
                            icon: const Icon(Icons.arrow_forward_rounded),
                            label: Text(actionLabel!),
                          )
                        : IconButton(
                            tooltip: actionLabel,
                            onPressed: onAction,
                            icon: const Icon(Icons.arrow_forward_rounded),
                          ),
                ],
              );
            },
          ),
          const SizedBox(height: ProductSpacing.lg),
          child,
        ],
      ),
    );
  }
}

final class _PremiumEmptyState extends StatelessWidget {
  const _PremiumEmptyState({
    required this.icon,
    required this.title,
    required this.description,
    this.actionLabel,
    this.onAction,
  });

  final IconData icon;
  final String title;
  final String description;
  final String? actionLabel;
  final VoidCallback? onAction;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(ProductSpacing.lg),
      decoration: BoxDecoration(
        color: scheme.surfaceContainer,
        borderRadius: BorderRadius.circular(ProductRadius.card),
      ),
      child: Column(
        children: <Widget>[
          Container(
            width: 52,
            height: 52,
            decoration: BoxDecoration(
              color: scheme.primaryContainer,
              shape: BoxShape.circle,
            ),
            child: Icon(icon, color: scheme.onPrimaryContainer),
          ),
          const SizedBox(height: ProductSpacing.sm),
          Text(
            title,
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.titleMedium,
          ),
          const SizedBox(height: 4),
          Text(
            description,
            textAlign: TextAlign.center,
            style: Theme.of(
              context,
            ).textTheme.bodySmall?.copyWith(color: scheme.onSurfaceVariant),
          ),
          if (onAction != null) ...<Widget>[
            const SizedBox(height: ProductSpacing.md),
            FilledButton.tonalIcon(
              onPressed: onAction,
              icon: const Icon(Icons.add_rounded),
              label: Text(actionLabel!),
            ),
          ],
        ],
      ),
    );
  }
}

final class _SectionCard extends StatelessWidget {
  const _SectionCard({
    required this.title,
    required this.icon,
    required this.child,
  });

  final String title;
  final IconData icon;
  final Widget child;

  @override
  Widget build(BuildContext context) =>
      _SectionSurface(title: title, icon: icon, child: child);
}

final class _ResponsiveEntityGrid extends StatelessWidget {
  const _ResponsiveEntityGrid({
    required this.entities,
    required this.controller,
  });

  final List<HomeEntity> entities;
  final HomeControlController controller;

  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, constraints) {
      final columns = constraints.maxWidth >= 760 ? 2 : 1;
      final width = columns == 1
          ? constraints.maxWidth
          : (constraints.maxWidth - ProductSpacing.sm) / 2;
      return Wrap(
        spacing: ProductSpacing.sm,
        runSpacing: ProductSpacing.sm,
        children: <Widget>[
          for (final entity in entities)
            SizedBox(
              width: width,
              child: EntityCard(entity: entity, controller: controller),
            ),
        ],
      );
    },
  );
}

Future<void> showAquariumDetails(
  BuildContext context,
  HomeControlController controller,
) async {
  await Navigator.of(context).push<void>(
    MaterialPageRoute<void>(
      builder: (_) => AquariumDetailsPage(controller: controller),
    ),
  );
}

final class AquariumDetailsPage extends StatelessWidget {
  const AquariumDetailsPage({required this.controller, super.key});

  final HomeControlController controller;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final snapshot = controller.snapshot!;
    final overview = AquariumOverview.fromSnapshot(snapshot);
    final grouped = <AquariumSemanticRole, List<HomeEntity>>{};
    for (final entity in overview.entities) {
      grouped
          .putIfAbsent(AquariumSemantics.classify(entity), () => <HomeEntity>[])
          .add(entity);
    }
    const order = <AquariumSemanticRole>[
      AquariumSemanticRole.temperature,
      AquariumSemanticRole.ph,
      AquariumSemanticRole.ec,
      AquariumSemanticRole.waterLevel,
      AquariumSemanticRole.leak,
      AquariumSemanticRole.alarm,
      AquariumSemanticRole.frontLight,
      AquariumSemanticRole.rearLight,
      AquariumSemanticRole.filter,
      AquariumSemanticRole.aeration,
      AquariumSemanticRole.heater,
      AquariumSemanticRole.targetTemperature,
      AquariumSemanticRole.topUp,
      AquariumSemanticRole.co2,
      AquariumSemanticRole.dosing,
      AquariumSemanticRole.feeder,
      AquariumSemanticRole.firmwareUpdate,
    ];
    return Scaffold(
      appBar: AppBar(title: Text(strings.t('aquarium'))),
      body: ListView(
        padding: const EdgeInsets.fromLTRB(20, 12, 20, 80),
        children: <Widget>[
          AquariumDashboardCard(
            snapshot: snapshot,
            controller: controller,
            interactive: false,
          ),
          const SizedBox(height: ProductSpacing.lg),
          for (final role in order)
            for (final entity in grouped[role] ?? const <HomeEntity>[])
              Padding(
                padding: const EdgeInsets.only(bottom: ProductSpacing.sm),
                child: EntityCard(entity: entity, controller: controller),
              ),
          const SizedBox(height: ProductSpacing.md),
          _SectionCard(
            title: strings.t('diagnostics'),
            icon: Icons.monitor_heart_rounded,
            child: Column(
              children: <Widget>[
                for (final device in overview.devices)
                  ListTile(
                    contentPadding: EdgeInsets.zero,
                    leading: Icon(
                      device.available
                          ? Icons.check_circle_rounded
                          : Icons.error_rounded,
                    ),
                    title: Text(device.name),
                    subtitle: Text(
                      '${device.model} · ${device.softwareVersion}',
                    ),
                    trailing: Text(strings.relativeTime(device.lastSeen)),
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
