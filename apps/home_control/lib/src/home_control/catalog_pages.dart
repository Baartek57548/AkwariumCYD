import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:flutter/material.dart';
import 'package:home_entities/home_entities.dart';

import '../design/components.dart';
import 'controller.dart';
import 'entity_widgets.dart';
import 'strings.dart';

final class RoomsPage extends StatefulWidget {
  const RoomsPage({required this.controller, super.key});

  final HomeControlController controller;

  @override
  State<RoomsPage> createState() => _RoomsPageState();
}

final class _RoomsPageState extends State<RoomsPage> {
  final TextEditingController _searchController = TextEditingController();
  String _query = '';

  @override
  void dispose() {
    _searchController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final snapshot = widget.controller.snapshot!;
    final strings = HomeControlStrings.of(context);
    final normalized = _query.trim().toLowerCase();
    final rooms =
        <RoomSummary>[
            for (final area in snapshot.areas)
              RoomSummary(
                area: area,
                entities: snapshot.entitiesForArea(area.id),
                devices: snapshot.devices
                    .where((device) => device.areaId == area.id)
                    .toList(growable: false),
              ),
          ]
          ..removeWhere((room) => !room.matches(normalized, strings))
          ..sort(
            (a, b) =>
                a.area.name.toLowerCase().compareTo(b.area.name.toLowerCase()),
          );
    return CustomScrollView(
      key: const PageStorageKey<String>('rooms-page'),
      slivers: <Widget>[
        SliverPadding(
          padding: const EdgeInsets.fromLTRB(20, 20, 20, 8),
          sliver: SliverToBoxAdapter(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                _CatalogSummary(
                  icon: Icons.meeting_room_rounded,
                  text: strings.withValues('roomsSummary', <String, Object>{
                    'rooms': snapshot.areas.length,
                    'items': snapshot.entities.length,
                  }),
                ),
                const SizedBox(height: ProductSpacing.md),
                TextField(
                  controller: _searchController,
                  onChanged: (value) => setState(() => _query = value),
                  decoration: InputDecoration(
                    labelText: strings.t('search'),
                    hintText: strings.t('searchHint'),
                    prefixIcon: const Icon(Icons.search_rounded),
                    suffixIcon: _query.isEmpty
                        ? null
                        : IconButton(
                            tooltip: strings.t('dismiss'),
                            onPressed: () {
                              _searchController.clear();
                              setState(() => _query = '');
                            },
                            icon: const Icon(Icons.close_rounded),
                          ),
                  ),
                ),
              ],
            ),
          ),
        ),
        if (rooms.isEmpty)
          SliverFillRemaining(
            hasScrollBody: false,
            child: HomeEmptyState(
              icon: snapshot.areas.isEmpty
                  ? Icons.meeting_room_outlined
                  : Icons.search_off_rounded,
              title: strings.t(
                snapshot.areas.isEmpty ? 'noRoomsTitle' : 'noResults',
              ),
              message: strings.t(
                snapshot.areas.isEmpty ? 'noAreas' : 'searchEmptyHint',
              ),
            ),
          )
        else
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 8, 20, 100),
            sliver: SliverLayoutBuilder(
              builder: (context, constraints) {
                final textScale = MediaQuery.textScalerOf(context).scale(1);
                final columns =
                    constraints.crossAxisExtent >=
                        ProductLayout.threeColumnBreakpoint
                    ? 3
                    : constraints.crossAxisExtent >=
                          ProductLayout.twoColumnBreakpoint
                    ? 2
                    : 1;
                Widget roomCard(int index) => RoomCard(
                  room: rooms[index],
                  onTap: () => Navigator.of(context).push<void>(
                    MaterialPageRoute<void>(
                      builder: (_) => RoomDetailsPage(
                        areaId: rooms[index].area.id,
                        controller: widget.controller,
                      ),
                    ),
                  ),
                );
                if (columns == 1) {
                  return SliverList.separated(
                    itemCount: rooms.length,
                    separatorBuilder: (_, _) =>
                        const SizedBox(height: ProductSpacing.sm),
                    itemBuilder: (context, index) => roomCard(index),
                  );
                }
                return SliverGrid(
                  gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                    crossAxisCount: columns,
                    mainAxisExtent: 224 + ((textScale - 1).clamp(0, 2) * 120),
                    crossAxisSpacing: ProductSpacing.sm,
                    mainAxisSpacing: ProductSpacing.sm,
                  ),
                  delegate: SliverChildBuilderDelegate(
                    (context, index) => roomCard(index),
                    childCount: rooms.length,
                  ),
                );
              },
            ),
          ),
      ],
    );
  }
}

final class RoomSummary {
  const RoomSummary({
    required this.area,
    required this.entities,
    required this.devices,
  });

  final HomeArea area;
  final List<HomeEntity> entities;
  final List<HomeDevice> devices;

  int get onlineDevices => devices.where((device) => device.available).length;

  int get offlineDevices => devices.length - onlineDevices;

  int get activeEntities => entities
      .where(
        (entity) =>
            entity.available &&
            (entity.booleanValue == true ||
                <String>{
                  'playing',
                  'heating',
                  'cooling',
                  'open',
                  'unlocked',
                }.contains(entity.state?.toString().toLowerCase())),
      )
      .length;

  List<HomeEntity> get primaryMetrics => entities
      .where(
        (entity) =>
            entity.available &&
            entity.numericValue != null &&
            entity.type == HomeEntityType.sensor,
      )
      .take(2)
      .toList(growable: false);

  bool matches(String query, HomeControlStrings strings) {
    if (query.isEmpty || area.name.toLowerCase().contains(query)) return true;
    return entities.any(
      (entity) =>
          entity.name.toLowerCase().contains(query) ||
          entity.id.localId.toLowerCase().contains(query) ||
          strings.entityType(entity.type).toLowerCase().contains(query),
    );
  }
}

final class RoomCard extends StatelessWidget {
  const RoomCard({required this.room, required this.onTap, super.key});

  final RoomSummary room;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final scheme = Theme.of(context).colorScheme;
    final semantic = room.offlineDevices > 0
        ? '${room.area.name}. ${strings.withValue('roomOfflineDevices', room.offlineDevices)}'
        : '${room.area.name}. ${strings.t('roomHealthy')}';
    return Card(
      clipBehavior: Clip.antiAlias,
      child: Semantics(
        button: true,
        label: semantic,
        hint: strings.t('roomOpenHint'),
        child: InkWell(
          onTap: onTap,
          child: Padding(
            padding: const EdgeInsets.all(ProductSpacing.md),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                Row(
                  children: <Widget>[
                    Container(
                      width: ProductLayout.minimumTouchTarget,
                      height: ProductLayout.minimumTouchTarget,
                      decoration: BoxDecoration(
                        color: scheme.primaryContainer,
                        borderRadius: BorderRadius.circular(
                          ProductRadius.control,
                        ),
                      ),
                      child: Icon(
                        _iconForArea(room.area),
                        color: scheme.onPrimaryContainer,
                      ),
                    ),
                    const SizedBox(width: ProductSpacing.sm),
                    Expanded(
                      child: Text(
                        room.area.name,
                        maxLines: 2,
                        overflow: TextOverflow.ellipsis,
                        style: Theme.of(context).textTheme.titleLarge,
                      ),
                    ),
                    Icon(Icons.arrow_forward_rounded, color: scheme.primary),
                  ],
                ),
                const SizedBox(height: ProductSpacing.md),
                HomeStatusChip(
                  icon: room.offlineDevices > 0
                      ? Icons.error_outline_rounded
                      : Icons.check_circle_outline_rounded,
                  label: room.offlineDevices > 0
                      ? strings.withValue(
                          'roomOfflineDevices',
                          room.offlineDevices,
                        )
                      : strings.t('roomHealthy'),
                  tone: room.offlineDevices > 0
                      ? HomeStatusTone.error
                      : HomeStatusTone.success,
                ),
                const SizedBox(height: ProductSpacing.sm),
                Wrap(
                  spacing: ProductSpacing.xs,
                  runSpacing: ProductSpacing.xs,
                  children: <Widget>[
                    _RoomFact(
                      icon: room.devices.isEmpty
                          ? Icons.widgets_rounded
                          : Icons.devices_other_rounded,
                      label: room.devices.isEmpty
                          ? strings.withValue(
                              'itemsCount',
                              room.entities.length,
                            )
                          : strings.withValues(
                              'roomDevicesOnline',
                              <String, Object>{
                                'online': room.onlineDevices,
                                'all': room.devices.length,
                              },
                            ),
                    ),
                    _RoomFact(
                      icon: Icons.bolt_rounded,
                      label: strings.withValue(
                        'activeEntities',
                        room.activeEntities,
                      ),
                    ),
                    for (final metric in room.primaryMetrics)
                      _RoomFact(
                        icon: iconForEntity(metric.type),
                        label: strings.entityState(metric),
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

final class _RoomFact extends StatelessWidget {
  const _RoomFact({required this.icon, required this.label});

  final IconData icon;
  final String label;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      padding: const EdgeInsets.symmetric(
        horizontal: ProductSpacing.sm,
        vertical: ProductSpacing.xs,
      ),
      decoration: BoxDecoration(
        color: scheme.surfaceContainerHigh,
        borderRadius: BorderRadius.circular(ProductRadius.pill),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: <Widget>[
          Icon(icon, size: ProductIconSize.small),
          const SizedBox(width: ProductSpacing.xxs),
          Text(label, style: Theme.of(context).textTheme.labelMedium),
        ],
      ),
    );
  }
}

final class RoomDetailsPage extends StatefulWidget {
  const RoomDetailsPage({
    required this.areaId,
    required this.controller,
    super.key,
  });

  final SourceScopedId areaId;
  final HomeControlController controller;

  @override
  State<RoomDetailsPage> createState() => _RoomDetailsPageState();
}

final class _RoomDetailsPageState extends State<RoomDetailsPage> {
  final TextEditingController _searchController = TextEditingController();
  String _query = '';

  @override
  void dispose() {
    _searchController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: widget.controller,
    builder: (context, _) {
      final strings = HomeControlStrings.of(context);
      final snapshot = widget.controller.snapshot;
      final area = snapshot?.areas
          .where((candidate) => candidate.id == widget.areaId)
          .firstOrNull;
      if (snapshot == null || area == null) {
        return Scaffold(
          appBar: AppBar(title: Text(strings.t('rooms'))),
          body: HomeEmptyState(
            icon: Icons.meeting_room_outlined,
            title: strings.t('rooms'),
            message: strings.t('roomUnavailable'),
          ),
        );
      }
      final normalized = _query.trim().toLowerCase();
      final entities = snapshot
          .entitiesForArea(area.id)
          .where(
            (entity) =>
                normalized.isEmpty ||
                entity.name.toLowerCase().contains(normalized) ||
                strings
                    .entityType(entity.type)
                    .toLowerCase()
                    .contains(normalized),
          )
          .toList(growable: false);
      final room = RoomSummary(
        area: area,
        entities: snapshot.entitiesForArea(area.id),
        devices: snapshot.devices
            .where((device) => device.areaId == area.id)
            .toList(growable: false),
      );
      return Scaffold(
        appBar: AppBar(title: Text(area.name)),
        body: CustomScrollView(
          slivers: <Widget>[
            SliverPadding(
              padding: const EdgeInsets.fromLTRB(
                ProductLayout.pageHorizontalPadding,
                ProductLayout.pageTopPadding,
                ProductLayout.pageHorizontalPadding,
                ProductSpacing.sm,
              ),
              sliver: SliverToBoxAdapter(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: <Widget>[
                    _RoomDetailSummary(room: room),
                    const SizedBox(height: ProductSpacing.md),
                    TextField(
                      controller: _searchController,
                      onChanged: (value) => setState(() => _query = value),
                      decoration: InputDecoration(
                        labelText: strings.t('search'),
                        hintText: strings.t('searchHint'),
                        prefixIcon: const Icon(Icons.search_rounded),
                        suffixIcon: _query.isEmpty
                            ? null
                            : IconButton(
                                tooltip: strings.t('dismiss'),
                                onPressed: () {
                                  _searchController.clear();
                                  setState(() => _query = '');
                                },
                                icon: const Icon(Icons.close_rounded),
                              ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
            if (entities.isEmpty)
              SliverFillRemaining(
                hasScrollBody: false,
                child: HomeEmptyState(
                  icon: normalized.isEmpty
                      ? Icons.widgets_outlined
                      : Icons.search_off_rounded,
                  title: strings.t(
                    normalized.isEmpty ? 'noEntitiesInArea' : 'noResults',
                  ),
                  message: strings.t(
                    normalized.isEmpty
                        ? 'noEntitiesInAreaHint'
                        : 'searchEmptyHint',
                  ),
                ),
              )
            else
              SliverPadding(
                padding: const EdgeInsets.fromLTRB(
                  ProductLayout.pageHorizontalPadding,
                  ProductSpacing.xs,
                  ProductLayout.pageHorizontalPadding,
                  ProductLayout.pageBottomPadding,
                ),
                sliver: SliverLayoutBuilder(
                  builder: (context, constraints) {
                    final textScale = MediaQuery.textScalerOf(context).scale(1);
                    final columns =
                        constraints.crossAxisExtent >=
                            ProductLayout.threeColumnBreakpoint
                        ? 3
                        : constraints.crossAxisExtent >=
                              ProductLayout.twoColumnBreakpoint
                        ? 2
                        : 1;
                    return SliverGrid(
                      gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                        crossAxisCount: columns,
                        mainAxisExtent:
                            104 + ((textScale - 1).clamp(0, 2) * 44),
                        crossAxisSpacing: ProductSpacing.sm,
                        mainAxisSpacing: ProductSpacing.sm,
                      ),
                      delegate: SliverChildBuilderDelegate(
                        (context, index) => EntityCard(
                          entity: entities[index],
                          controller: widget.controller,
                          compact: true,
                        ),
                        childCount: entities.length,
                      ),
                    );
                  },
                ),
              ),
          ],
        ),
      );
    },
  );
}

final class _RoomDetailSummary extends StatelessWidget {
  const _RoomDetailSummary({required this.room});

  final RoomSummary room;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final scheme = Theme.of(context).colorScheme;
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(ProductSpacing.lg),
      decoration: BoxDecoration(
        color: scheme.surfaceContainerLow,
        borderRadius: BorderRadius.circular(ProductRadius.card),
        border: Border.all(color: scheme.outlineVariant),
      ),
      child: Wrap(
        alignment: WrapAlignment.spaceBetween,
        crossAxisAlignment: WrapCrossAlignment.center,
        spacing: ProductSpacing.lg,
        runSpacing: ProductSpacing.md,
        children: <Widget>[
          Row(
            mainAxisSize: MainAxisSize.min,
            children: <Widget>[
              Icon(
                _iconForArea(room.area),
                size: ProductIconSize.large,
                color: scheme.primary,
              ),
              const SizedBox(width: ProductSpacing.sm),
              Text(
                room.area.name,
                style: Theme.of(context).textTheme.titleLarge,
              ),
            ],
          ),
          _RoomFact(
            icon: room.devices.isEmpty
                ? Icons.widgets_rounded
                : Icons.devices_other_rounded,
            label: room.devices.isEmpty
                ? strings.withValue('itemsCount', room.entities.length)
                : strings.withValues('roomDevicesOnline', <String, Object>{
                    'online': room.onlineDevices,
                    'all': room.devices.length,
                  }),
          ),
          _RoomFact(
            icon: Icons.bolt_rounded,
            label: strings.withValue('activeEntities', room.activeEntities),
          ),
          _RoomFact(
            icon: Icons.widgets_rounded,
            label: strings.withValue('entitiesCount', room.entities.length),
          ),
        ],
      ),
    );
  }
}

IconData _iconForArea(HomeArea area) {
  final source = '${area.icon ?? ''} ${area.name}'.toLowerCase();
  if (source.contains('kuch') || source.contains('kitchen')) {
    return Icons.kitchen_rounded;
  }
  if (source.contains('syp') ||
      source.contains('bed') ||
      source.contains('sleep')) {
    return Icons.bed_rounded;
  }
  if (source.contains('bath') || source.contains('\u0142az')) {
    return Icons.bathtub_rounded;
  }
  if (source.contains('biur') || source.contains('office')) {
    return Icons.desk_rounded;
  }
  if (source.contains('salon') ||
      source.contains('living') ||
      source.contains('sofa')) {
    return Icons.weekend_rounded;
  }
  if (source.contains('aqu') || source.contains('water')) {
    return Icons.water_rounded;
  }
  if (source.contains('garden') || source.contains('ogr')) {
    return Icons.yard_rounded;
  }
  if (source.contains('garage')) return Icons.garage_rounded;
  return Icons.meeting_room_rounded;
}

final class DevicesPage extends StatefulWidget {
  const DevicesPage({required this.controller, super.key});

  final HomeControlController controller;

  @override
  State<DevicesPage> createState() => _DevicesPageState();
}

final class _DevicesPageState extends State<DevicesPage> {
  final TextEditingController _searchController = TextEditingController();
  String _query = '';

  @override
  void dispose() {
    _searchController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final snapshot = widget.controller.snapshot!;
    final strings = HomeControlStrings.of(context);
    final normalized = _query.trim().toLowerCase();
    final devices =
        snapshot.devices
            .where((device) {
              if (normalized.isEmpty) return true;
              return '${device.name} ${device.model} ${device.manufacturer}'
                  .toLowerCase()
                  .contains(normalized);
            })
            .toList(growable: false)
          ..sort(
            (a, b) => a.name.toLowerCase().compareTo(b.name.toLowerCase()),
          );
    return CustomScrollView(
      key: const PageStorageKey<String>('devices-page'),
      slivers: <Widget>[
        SliverPadding(
          padding: const EdgeInsets.fromLTRB(20, 20, 20, 8),
          sliver: SliverToBoxAdapter(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                _CatalogSummary(
                  icon: Icons.devices_other_rounded,
                  text: strings.withValues('devicesSummary', <String, Object>{
                    'online': snapshot.devices
                        .where((device) => device.available)
                        .length,
                    'all': snapshot.devices.length,
                  }),
                ),
                const SizedBox(height: ProductSpacing.md),
                TextField(
                  controller: _searchController,
                  onChanged: (value) => setState(() => _query = value),
                  decoration: InputDecoration(
                    labelText: strings.t('search'),
                    hintText: strings.t('searchHint'),
                    prefixIcon: const Icon(Icons.search_rounded),
                    suffixIcon: _query.isEmpty
                        ? null
                        : IconButton(
                            tooltip: strings.t('dismiss'),
                            onPressed: () {
                              _searchController.clear();
                              setState(() => _query = '');
                            },
                            icon: const Icon(Icons.close_rounded),
                          ),
                  ),
                ),
              ],
            ),
          ),
        ),
        if (devices.isEmpty)
          SliverFillRemaining(
            hasScrollBody: false,
            child: HomeEmptyState(
              icon: Icons.devices_other_rounded,
              title: strings.t(normalized.isEmpty ? 'noDevices' : 'noResults'),
              message: strings.t(
                normalized.isEmpty ? 'noDevicesHint' : 'searchEmptyHint',
              ),
            ),
          )
        else
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 8, 20, 100),
            sliver: SliverList.separated(
              itemCount: devices.length,
              separatorBuilder: (_, _) =>
                  const SizedBox(height: ProductSpacing.sm),
              itemBuilder: (context, index) => DeviceCard(
                device: devices[index],
                snapshot: snapshot,
                controller: widget.controller,
              ),
            ),
          ),
      ],
    );
  }
}

final class DeviceCard extends Card {
  DeviceCard({
    required HomeDevice device,
    required HomeSnapshot snapshot,
    required HomeControlController controller,
    super.key,
  }) : super(
         clipBehavior: Clip.antiAlias,
         child: Builder(
           builder: (context) {
             final scheme = Theme.of(context).colorScheme;
             final color = device.available ? scheme.primary : scheme.error;
             return ExpansionTile(
               key: PageStorageKey<String>(device.id.value),
               leading: Container(
                 width: 48,
                 height: 48,
                 decoration: BoxDecoration(
                   color: color.withValues(alpha: 0.12),
                   borderRadius: BorderRadius.circular(ProductRadius.control),
                 ),
                 child: Icon(
                   device.isAquariumController
                       ? Icons.water_rounded
                       : Icons.memory_rounded,
                   color: color,
                 ),
               ),
               title: Text(
                 device.name,
                 maxLines: 2,
                 overflow: TextOverflow.ellipsis,
               ),
               subtitle: Text(
                 <String>[
                   snapshot.sourceName,
                   device.manufacturer,
                   device.model,
                 ].where((value) => value.isNotEmpty).join(' · '),
                 maxLines: 2,
                 overflow: TextOverflow.ellipsis,
               ),
               trailing: Icon(
                 device.available
                     ? Icons.check_circle_rounded
                     : Icons.error_outline_rounded,
                 color: color,
               ),
               childrenPadding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
               children: <Widget>[
                 for (final entity in snapshot.entitiesForDevice(device.id))
                   Padding(
                     padding: const EdgeInsets.only(top: ProductSpacing.xs),
                     child: EntityCard(
                       entity: entity,
                       controller: controller,
                       compact: true,
                     ),
                   ),
               ],
             );
           },
         ),
       );
}

final class _CatalogSummary extends StatelessWidget {
  const _CatalogSummary({required this.icon, required this.text});

  final IconData icon;
  final String text;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      padding: const EdgeInsets.all(ProductSpacing.md),
      decoration: BoxDecoration(
        color: scheme.surfaceContainerLow,
        borderRadius: BorderRadius.circular(ProductRadius.card),
        border: Border.all(color: scheme.outlineVariant),
      ),
      child: Row(
        children: <Widget>[
          Container(
            width: 44,
            height: 44,
            decoration: BoxDecoration(
              color: scheme.primaryContainer,
              borderRadius: BorderRadius.circular(ProductRadius.control),
            ),
            child: Icon(icon, color: scheme.onPrimaryContainer),
          ),
          const SizedBox(width: ProductSpacing.sm),
          Expanded(
            child: Text(
              text,
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
              style: Theme.of(context).textTheme.titleMedium,
            ),
          ),
        ],
      ),
    );
  }
}
