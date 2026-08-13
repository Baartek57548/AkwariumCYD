import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:flutter/material.dart';
import 'package:home_entities/home_entities.dart';

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
  SourceScopedId? _selectedArea;
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
    final entities = snapshot.entities
        .where((entity) {
          final areaMatches =
              _selectedArea == null || entity.areaId == _selectedArea;
          final queryMatches =
              normalized.isEmpty ||
              entity.name.toLowerCase().contains(normalized) ||
              entity.id.localId.toLowerCase().contains(normalized) ||
              strings
                  .entityType(entity.type)
                  .toLowerCase()
                  .contains(normalized);
          return areaMatches && queryMatches;
        })
        .toList(growable: false);
    return CustomScrollView(
      key: const PageStorageKey<String>('rooms-page'),
      slivers: <Widget>[
        SliverPadding(
          padding: const EdgeInsets.fromLTRB(20, 20, 20, 8),
          sliver: SliverToBoxAdapter(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                Text(
                  strings.t('rooms'),
                  style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                    fontWeight: FontWeight.w900,
                  ),
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
                const SizedBox(height: ProductSpacing.md),
                SingleChildScrollView(
                  scrollDirection: Axis.horizontal,
                  child: Row(
                    children: <Widget>[
                      ChoiceChip(
                        label: Text(strings.t('all')),
                        selected: _selectedArea == null,
                        onSelected: (_) => setState(() => _selectedArea = null),
                      ),
                      for (final area in snapshot.areas) ...<Widget>[
                        const SizedBox(width: ProductSpacing.xs),
                        ChoiceChip(
                          label: Text(area.name),
                          selected: _selectedArea == area.id,
                          onSelected: (_) =>
                              setState(() => _selectedArea = area.id),
                        ),
                      ],
                    ],
                  ),
                ),
              ],
            ),
          ),
        ),
        if (entities.isEmpty)
          SliverFillRemaining(
            hasScrollBody: false,
            child: _CatalogEmpty(
              icon: Icons.search_off_rounded,
              text: strings.t(
                normalized.isNotEmpty
                    ? 'noResults'
                    : _selectedArea != null
                    ? 'noEntitiesInArea'
                    : 'noAreas',
              ),
            ),
          )
        else
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 8, 20, 100),
            sliver: SliverLayoutBuilder(
              builder: (context, constraints) {
                final columns = constraints.crossAxisExtent >= 900
                    ? 3
                    : constraints.crossAxisExtent >= 580
                    ? 2
                    : 1;
                return SliverGrid(
                  gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                    crossAxisCount: columns,
                    mainAxisExtent: 104,
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
    );
  }
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
                Text(
                  strings.t('devices'),
                  style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                    fontWeight: FontWeight.w900,
                  ),
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
            child: _CatalogEmpty(
              icon: Icons.devices_other_rounded,
              text: strings.t(normalized.isEmpty ? 'noDevices' : 'noResults'),
            ),
          )
        else
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 8, 20, 100),
            sliver: SliverList.separated(
              itemCount: devices.length,
              separatorBuilder: (_, _) =>
                  const SizedBox(height: ProductSpacing.sm),
              itemBuilder: (context, index) => _DeviceCard(
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

final class _DeviceCard extends ExpansionTile {
  _DeviceCard({
    required HomeDevice device,
    required HomeSnapshot snapshot,
    required HomeControlController controller,
  }) : super(
         key: PageStorageKey<String>(device.id.value),
         leading: CircleAvatar(
           child: Icon(
             device.isAquariumController
                 ? Icons.water_rounded
                 : Icons.memory_rounded,
           ),
         ),
         title: Text(device.name),
         subtitle: Text(
           <String>[
             snapshot.sourceName,
             device.manufacturer,
             device.model,
           ].where((value) => value.isNotEmpty).join(' · '),
         ),
         trailing: Icon(
           device.available
               ? Icons.check_circle_rounded
               : Icons.error_outline_rounded,
           color: device.available ? ProductColors.success : null,
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
}

final class _CatalogEmpty extends StatelessWidget {
  const _CatalogEmpty({required this.icon, required this.text});

  final IconData icon;
  final String text;

  @override
  Widget build(BuildContext context) => Center(
    child: Padding(
      padding: const EdgeInsets.all(ProductSpacing.xl),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: <Widget>[
          Icon(
            icon,
            size: 52,
            color: Theme.of(context).colorScheme.onSurfaceVariant,
          ),
          const SizedBox(height: ProductSpacing.md),
          Text(text, textAlign: TextAlign.center),
        ],
      ),
    ),
  );
}
