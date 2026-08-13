import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:aquacyd_protocol/aquacyd_protocol.dart';
import 'package:flutter/material.dart';
import 'package:home_entities/home_entities.dart';

import 'controller.dart';
import 'entity_widgets.dart';
import 'strings.dart';

final class HomeDashboardPage extends StatelessWidget {
  const HomeDashboardPage({required this.controller, super.key});

  final HomeControlController controller;

  @override
  Widget build(BuildContext context) {
    final snapshot = controller.snapshot!;
    final strings = HomeControlStrings.of(context);
    final visible = controller.dashboard.order.where(
      (id) => !controller.dashboard.hidden.contains(id),
    );
    return RefreshIndicator(
      onRefresh: () async => controller.refresh(announce: true),
      child: CustomScrollView(
        key: const PageStorageKey<String>('home-dashboard'),
        slivers: <Widget>[
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 22, 20, 8),
            sliver: SliverToBoxAdapter(
              child: _DashboardHeader(snapshot: snapshot),
            ),
          ),
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 10, 20, 10),
            sliver: SliverToBoxAdapter(
              child: _DashboardSections(
                sections: visible.toList(growable: false),
                snapshot: snapshot,
                controller: controller,
              ),
            ),
          ),
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 16, 20, 110),
            sliver: SliverToBoxAdapter(
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
          ),
        ],
      ),
    );
  }
}

final class _DashboardHeader extends StatelessWidget {
  const _DashboardHeader({required this.snapshot});

  final HomeSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final online = snapshot.devices.where((device) => device.available).length;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: <Widget>[
        Text(
          strings.t('home'),
          style: Theme.of(context).textTheme.displaySmall?.copyWith(
            fontWeight: FontWeight.w900,
            letterSpacing: -1.2,
          ),
        ),
        const SizedBox(height: ProductSpacing.xs),
        Wrap(
          spacing: ProductSpacing.xs,
          runSpacing: ProductSpacing.xs,
          children: <Widget>[
            Chip(
              avatar: const Icon(Icons.hub_rounded, size: 18),
              label: Text(snapshot.sourceName),
            ),
            Chip(
              avatar: const Icon(Icons.devices_rounded, size: 18),
              label: Text(strings.withValue('onlineDevices', online)),
            ),
            if (snapshot.isStale)
              Chip(
                avatar: const Icon(Icons.schedule_rounded, size: 18),
                label: Text(strings.t('stale')),
              ),
          ],
        ),
      ],
    );
  }
}

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
    final scheme = Theme.of(context).colorScheme;
    if (!overview.present) {
      return _SectionCard(
        title: strings.t('aquarium'),
        icon: Icons.water_rounded,
        child: Text(strings.t('noAquarium')),
      );
    }
    final temperature = overview.byRole(AquariumSemanticRole.temperature);
    final ph = overview.byRole(AquariumSemanticRole.ph);
    final ec = overview.byRole(AquariumSemanticRole.ec);
    final alarm = overview.hasAlarm;
    return Card(
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: interactive
            ? () => showAquariumDetails(context, controller)
            : null,
        child: Ink(
          decoration: BoxDecoration(
            gradient: LinearGradient(
              begin: Alignment.topLeft,
              end: Alignment.bottomRight,
              colors: alarm
                  ? <Color>[scheme.errorContainer, scheme.surfaceContainerLow]
                  : <Color>[
                      scheme.primaryContainer,
                      scheme.surfaceContainerLow,
                    ],
            ),
          ),
          child: Padding(
            padding: const EdgeInsets.all(ProductSpacing.lg),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                Row(
                  children: <Widget>[
                    Container(
                      width: 52,
                      height: 52,
                      decoration: BoxDecoration(
                        color: alarm ? scheme.error : scheme.primary,
                        borderRadius: BorderRadius.circular(17),
                      ),
                      child: const Icon(
                        Icons.water_rounded,
                        color: Colors.white,
                      ),
                    ),
                    const SizedBox(width: ProductSpacing.md),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: <Widget>[
                          Text(
                            strings.t('aquarium'),
                            style: Theme.of(context).textTheme.titleLarge
                                ?.copyWith(fontWeight: FontWeight.w900),
                          ),
                          Text(
                            strings.t(
                              alarm ? 'aquariumAlarm' : 'aquariumHealthy',
                            ),
                            style: TextStyle(
                              color: alarm
                                  ? scheme.error
                                  : scheme.onSurfaceVariant,
                            ),
                          ),
                        ],
                      ),
                    ),
                    if (interactive) const Icon(Icons.chevron_right_rounded),
                  ],
                ),
                const SizedBox(height: ProductSpacing.lg),
                Wrap(
                  spacing: ProductSpacing.md,
                  runSpacing: ProductSpacing.md,
                  children: <Widget>[
                    _AquariumMetric(
                      label: strings.t('waterTemperature'),
                      value: temperature == null
                          ? strings.t('noData')
                          : strings.entityState(temperature),
                    ),
                    if (ph != null)
                      _AquariumMetric(
                        label: 'pH',
                        value: strings.entityState(ph),
                      ),
                    if (ec != null)
                      _AquariumMetric(
                        label: 'EC',
                        value: strings.entityState(ec),
                      ),
                    _AquariumMetric(
                      label: strings.t('connection'),
                      value: snapshot.isOffline
                          ? strings.t('offline')
                          : strings.t('connected'),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

final class _AquariumMetric extends StatelessWidget {
  const _AquariumMetric({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) => ConstrainedBox(
    constraints: const BoxConstraints(minWidth: 130),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: <Widget>[
        Text(
          label,
          style: Theme.of(context).textTheme.labelMedium?.copyWith(
            color: Theme.of(context).colorScheme.onSurfaceVariant,
          ),
        ),
        const SizedBox(height: 2),
        Text(
          value,
          style: Theme.of(
            context,
          ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
        ),
      ],
    ),
  );
}

final class _FavoritesSection extends StatelessWidget {
  const _FavoritesSection({required this.snapshot, required this.controller});

  final HomeSnapshot snapshot;
  final HomeControlController controller;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final favorites = snapshot.entities
        .where(
          (entity) => controller.dashboard.favorites.contains(entity.id.value),
        )
        .toList(growable: false);
    return _SectionCard(
      title: strings.t('favorites'),
      icon: Icons.star_rounded,
      child: favorites.isEmpty
          ? Text(strings.t('noFavorites'))
          : _ResponsiveEntityGrid(entities: favorites, controller: controller),
    );
  }
}

final class _AreasSection extends StatelessWidget {
  const _AreasSection({required this.snapshot});

  final HomeSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    return _SectionCard(
      title: strings.t('areasOverview'),
      icon: Icons.meeting_room_rounded,
      child: snapshot.areas.isEmpty
          ? Text(strings.t('noAreas'))
          : Wrap(
              spacing: ProductSpacing.sm,
              runSpacing: ProductSpacing.sm,
              children: <Widget>[
                for (final area in snapshot.areas)
                  _AreaSummary(area: area, snapshot: snapshot),
              ],
            ),
    );
  }
}

final class _AreaSummary extends StatelessWidget {
  const _AreaSummary({required this.area, required this.snapshot});

  final HomeArea area;
  final HomeSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final entities = snapshot.entitiesForArea(area.id);
    final active = entities
        .where((entity) => entity.booleanValue == true)
        .length;
    return Container(
      width: 180,
      padding: const EdgeInsets.all(ProductSpacing.md),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerHigh,
        borderRadius: BorderRadius.circular(ProductRadius.card),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          const Icon(Icons.meeting_room_rounded),
          const SizedBox(height: ProductSpacing.sm),
          Text(
            area.name,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: Theme.of(
              context,
            ).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w800),
          ),
          Text(strings.withValue('entitiesCount', entities.length)),
          if (active > 0) Text(strings.withValue('activeEntities', active)),
        ],
      ),
    );
  }
}

final class _DashboardSections extends StatelessWidget {
  const _DashboardSections({
    required this.sections,
    required this.snapshot,
    required this.controller,
  });

  final List<String> sections;
  final HomeSnapshot snapshot;
  final HomeControlController controller;

  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, constraints) {
      final twoColumns = constraints.maxWidth >= 640;
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
                ),
                'areas' => _AreasSection(snapshot: snapshot),
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
        snapshot.entities.where((entity) => entity.updatedAt != null).toList()
          ..sort((a, b) => b.updatedAt!.compareTo(a.updatedAt!));
    return _SectionCard(
      title: strings.t('recentActivity'),
      icon: Icons.history_rounded,
      child: Column(
        children: <Widget>[
          for (final entity in recent.take(5))
            ListTile(
              contentPadding: EdgeInsets.zero,
              leading: Icon(iconForEntity(entity.type)),
              title: Text(entity.name),
              subtitle: Text(strings.entityState(entity)),
              trailing: Text(strings.relativeTime(entity.updatedAt)),
            ),
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
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: <Widget>[
      Row(
        children: <Widget>[
          Icon(icon, size: 22),
          const SizedBox(width: ProductSpacing.xs),
          Expanded(
            child: Text(
              title,
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
              style: Theme.of(
                context,
              ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
            ),
          ),
        ],
      ),
      const SizedBox(height: ProductSpacing.sm),
      child,
    ],
  );
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
