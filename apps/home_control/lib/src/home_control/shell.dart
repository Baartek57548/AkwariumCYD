import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:flutter/material.dart';
import 'package:home_entities/home_entities.dart';

import 'catalog_pages.dart';
import 'controller.dart';
import 'dashboard.dart';
import 'entity_widgets.dart';
import 'operations_pages.dart';
import 'strings.dart';

final class HomeControlShell extends StatefulWidget {
  const HomeControlShell({required this.controller, super.key});

  final HomeControlController controller;

  @override
  State<HomeControlShell> createState() => _HomeControlShellState();
}

final class _HomeControlShellState extends State<HomeControlShell> {
  int _selectedIndex = 0;

  @override
  Widget build(BuildContext context) {
    WidgetsBinding.instance.addPostFrameCallback((_) => _showPendingNotice());
    final strings = HomeControlStrings.of(context);
    final snapshot = widget.controller.snapshot;
    if (snapshot == null) return const SizedBox.shrink();
    final availableUpdates = snapshot.updates
        .where(
          (update) =>
              update.phase == HomeUpdatePhase.available || update.mandatory,
        )
        .length;

    final destinations = <_ShellDestination>[
      _ShellDestination(
        label: strings.t('dashboard'),
        compactLabel: strings.t('navDashboard'),
        icon: Icons.space_dashboard_outlined,
        selectedIcon: Icons.space_dashboard_rounded,
        page: HomeDashboardPage(
          controller: widget.controller,
          onOpenRooms: () => _selectDestination(1),
          onOpenDevices: () => _selectDestination(2),
          onOpenUpdates: () => _selectDestination(4),
          onCustomize: _openDashboardEditor,
        ),
      ),
      _ShellDestination(
        label: strings.t('rooms'),
        compactLabel: strings.t('navRooms'),
        icon: Icons.meeting_room_outlined,
        selectedIcon: Icons.meeting_room_rounded,
        page: RoomsPage(controller: widget.controller),
      ),
      _ShellDestination(
        label: strings.t('devices'),
        compactLabel: strings.t('navDevices'),
        icon: Icons.devices_other_outlined,
        selectedIcon: Icons.devices_other_rounded,
        page: DevicesPage(controller: widget.controller),
      ),
      _ShellDestination(
        label: strings.t('automations'),
        compactLabel: strings.t('navAutomations'),
        icon: Icons.account_tree_outlined,
        selectedIcon: Icons.account_tree_rounded,
        page: AutomationsPage(controller: widget.controller),
      ),
      _ShellDestination(
        label: strings.t('updates'),
        compactLabel: strings.t('navUpdates'),
        icon: Icons.system_update_outlined,
        selectedIcon: Icons.system_update_rounded,
        page: UpdatesPage(controller: widget.controller),
        badgeCount: availableUpdates,
      ),
      _ShellDestination(
        label: strings.t('settings'),
        compactLabel: strings.t('navSettings'),
        icon: Icons.settings_outlined,
        selectedIcon: Icons.settings_rounded,
        page: SettingsPage(controller: widget.controller),
      ),
    ];

    return LayoutBuilder(
      builder: (context, constraints) {
        final useRail = constraints.maxWidth >= 720;
        final useFullRail = constraints.maxWidth >= 960;
        final extendedRail = constraints.maxWidth >= 1180;
        final content = Column(
          children: <Widget>[
            SourceStatusBanner(
              snapshot: snapshot,
              failureKey: widget.controller.failure?.messageKey,
              onDismiss: widget.controller.clearFailure,
            ),
            Expanded(
              child: IndexedStack(
                index: _selectedIndex,
                children: destinations
                    .map((destination) => destination.page)
                    .toList(growable: false),
              ),
            ),
          ],
        );

        return Scaffold(
          appBar: _HomeControlAppBar(
            title: destinations[_selectedIndex].label,
            controller: widget.controller,
            showBrand: !useRail,
          ),
          body: useRail
              ? Row(
                  children: <Widget>[
                    SafeArea(
                      top: false,
                      child: SizedBox(
                        width: extendedRail ? 240 : 96,
                        child: NavigationRail(
                          scrollable: useFullRail,
                          extended: extendedRail,
                          minWidth: 72,
                          selectedIndex: useFullRail
                              ? _selectedIndex
                              : _selectedIndex <= 2
                              ? _selectedIndex
                              : 3,
                          labelType: extendedRail
                              ? NavigationRailLabelType.none
                              : NavigationRailLabelType.selected,
                          onDestinationSelected: (value) {
                            if (useFullRail || value <= 2) {
                              _selectDestination(value);
                            } else {
                              _showMoreDestinations(context, destinations);
                            }
                          },
                          leading: Padding(
                            padding: const EdgeInsets.symmetric(vertical: 10),
                            child: HomeControlMark(
                              size: extendedRail ? 48 : 40,
                            ),
                          ),
                          destinations: <NavigationRailDestination>[
                            for (final destination
                                in useFullRail
                                    ? destinations
                                    : destinations.take(3))
                              NavigationRailDestination(
                                icon: _DestinationIcon(
                                  icon: destination.icon,
                                  badgeCount: destination.badgeCount,
                                  tooltip: destination.label,
                                ),
                                selectedIcon: _DestinationIcon(
                                  icon: destination.selectedIcon,
                                  badgeCount: destination.badgeCount,
                                  tooltip: destination.label,
                                ),
                                label: Text(
                                  extendedRail
                                      ? destination.label
                                      : destination.compactLabel,
                                  maxLines: 1,
                                  overflow: TextOverflow.ellipsis,
                                ),
                              ),
                            if (!useFullRail)
                              NavigationRailDestination(
                                icon: _DestinationIcon(
                                  icon: Icons.more_horiz_rounded,
                                  badgeCount: availableUpdates,
                                  tooltip: strings.t('more'),
                                ),
                                selectedIcon: _DestinationIcon(
                                  icon: Icons.more_rounded,
                                  badgeCount: availableUpdates,
                                  tooltip: strings.t('more'),
                                ),
                                label: Text(
                                  strings.t('more'),
                                  maxLines: 1,
                                  overflow: TextOverflow.ellipsis,
                                ),
                              ),
                          ],
                        ),
                      ),
                    ),
                    const VerticalDivider(width: 1),
                    Expanded(child: content),
                  ],
                )
              : content,
          bottomNavigationBar: useRail
              ? null
              : DecoratedBox(
                  decoration: BoxDecoration(
                    border: Border(
                      top: BorderSide(
                        color: Theme.of(context).colorScheme.outlineVariant,
                      ),
                    ),
                  ),
                  child: NavigationBar(
                    selectedIndex: _selectedIndex <= 2 ? _selectedIndex : 3,
                    onDestinationSelected: (value) {
                      if (value <= 2) {
                        _selectDestination(value);
                      } else {
                        _showMoreDestinations(context, destinations);
                      }
                    },
                    destinations: <NavigationDestination>[
                      for (final destination in destinations.take(3))
                        NavigationDestination(
                          icon: _DestinationIcon(
                            icon: destination.icon,
                            badgeCount: destination.badgeCount,
                          ),
                          selectedIcon: _DestinationIcon(
                            icon: destination.selectedIcon,
                            badgeCount: destination.badgeCount,
                          ),
                          label: destination.compactLabel,
                        ),
                      NavigationDestination(
                        icon: _DestinationIcon(
                          icon: Icons.more_horiz_rounded,
                          badgeCount: availableUpdates,
                        ),
                        selectedIcon: _DestinationIcon(
                          icon: Icons.more_rounded,
                          badgeCount: availableUpdates,
                        ),
                        label: strings.t('more'),
                      ),
                    ],
                  ),
                ),
        );
      },
    );
  }

  void _selectDestination(int value) {
    if (_selectedIndex == value) return;
    setState(() => _selectedIndex = value);
  }

  Future<void> _openDashboardEditor() async {
    await Navigator.of(context).push<void>(
      MaterialPageRoute<void>(
        builder: (_) => DashboardEditorPage(controller: widget.controller),
      ),
    );
  }

  Future<void> _showMoreDestinations(
    BuildContext context,
    List<_ShellDestination> destinations,
  ) async {
    final selected = await showModalBottomSheet<int>(
      context: context,
      showDragHandle: true,
      builder: (context) => SafeArea(
        child: ListView(
          shrinkWrap: true,
          padding: const EdgeInsets.fromLTRB(16, 0, 16, 20),
          children: <Widget>[
            Padding(
              padding: const EdgeInsets.fromLTRB(8, 4, 8, 12),
              child: Text(
                HomeControlStrings.of(context).t('more'),
                style: Theme.of(context).textTheme.titleLarge,
              ),
            ),
            for (var index = 3; index < destinations.length; index++)
              Padding(
                padding: const EdgeInsets.only(bottom: 6),
                child: ListTile(
                  selected: _selectedIndex == index,
                  leading: _DestinationIcon(
                    icon: _selectedIndex == index
                        ? destinations[index].selectedIcon
                        : destinations[index].icon,
                    badgeCount: destinations[index].badgeCount,
                  ),
                  title: Text(destinations[index].label),
                  trailing: _selectedIndex == index
                      ? const Icon(Icons.check_rounded)
                      : const Icon(Icons.chevron_right_rounded),
                  onTap: () => Navigator.of(context).pop(index),
                ),
              ),
          ],
        ),
      ),
    );
    if (selected != null && mounted) _selectDestination(selected);
  }

  void _showPendingNotice() {
    if (!mounted) return;
    final key = widget.controller.takeNoticeKey();
    if (key == null) return;
    final messenger = ScaffoldMessenger.maybeOf(context);
    if (messenger == null) return;
    messenger
      ..hideCurrentSnackBar()
      ..showSnackBar(
        SnackBar(
          behavior: SnackBarBehavior.floating,
          content: Text(HomeControlStrings.of(context).t(key)),
        ),
      );
  }
}

final class _HomeControlAppBar extends StatelessWidget
    implements PreferredSizeWidget {
  const _HomeControlAppBar({
    required this.title,
    required this.controller,
    required this.showBrand,
  });

  final String title;
  final HomeControlController controller;
  final bool showBrand;

  @override
  Size get preferredSize => const Size.fromHeight(64);

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final sourceName = switch (controller.activeSourceKind) {
      null => strings.t('unknown'),
      final kind => strings.sourceName(kind),
    };
    final snapshot = controller.snapshot;
    final online = snapshot != null && !snapshot.isOffline;
    final width = MediaQuery.sizeOf(context).width;
    return AppBar(
      toolbarHeight: 64,
      titleSpacing: ProductSpacing.md,
      title: Row(
        children: <Widget>[
          if (showBrand) ...<Widget>[
            const HomeControlMark(size: 34),
            const SizedBox(width: ProductSpacing.sm),
          ],
          Expanded(
            child: Text(title, maxLines: 1, overflow: TextOverflow.ellipsis),
          ),
        ],
      ),
      actions: <Widget>[
        if (width >= 540) _SourcePill(label: sourceName, online: online),
        const SizedBox(width: ProductSpacing.xs),
        IconButton.filledTonal(
          tooltip: strings.t('refresh'),
          onPressed: controller.refreshing
              ? null
              : () => controller.refresh(announce: true),
          icon: controller.refreshing
              ? const SizedBox.square(
                  dimension: 20,
                  child: CircularProgressIndicator(strokeWidth: 2.4),
                )
              : const Icon(Icons.refresh_rounded),
        ),
        const SizedBox(width: ProductSpacing.sm),
      ],
      bottom: PreferredSize(
        preferredSize: const Size.fromHeight(1),
        child: Divider(
          height: 1,
          color: Theme.of(
            context,
          ).colorScheme.outlineVariant.withValues(alpha: 0.7),
        ),
      ),
    );
  }
}

final class _SourcePill extends StatelessWidget {
  const _SourcePill({required this.label, required this.online});

  final String label;
  final bool online;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final color = online ? scheme.primary : scheme.error;
    return Container(
      constraints: const BoxConstraints(maxWidth: 220),
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: scheme.surfaceContainer,
        borderRadius: BorderRadius.circular(999),
        border: Border.all(color: scheme.outlineVariant),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: <Widget>[
          Container(
            width: 8,
            height: 8,
            decoration: BoxDecoration(color: color, shape: BoxShape.circle),
          ),
          const SizedBox(width: 8),
          Flexible(
            child: Text(
              label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: Theme.of(context).textTheme.labelMedium,
            ),
          ),
        ],
      ),
    );
  }
}

final class _DestinationIcon extends StatelessWidget {
  const _DestinationIcon({
    required this.icon,
    required this.badgeCount,
    this.tooltip,
  });

  final IconData icon;
  final int badgeCount;
  final String? tooltip;

  @override
  Widget build(BuildContext context) {
    final child = Badge(
      isLabelVisible: badgeCount > 0,
      label: Text(badgeCount > 9 ? '9+' : '$badgeCount'),
      child: Icon(icon),
    );
    return tooltip == null ? child : Tooltip(message: tooltip!, child: child);
  }
}

final class _ShellDestination {
  const _ShellDestination({
    required this.label,
    required this.compactLabel,
    required this.icon,
    required this.selectedIcon,
    required this.page,
    this.badgeCount = 0,
  });

  final String label;
  final String compactLabel;
  final IconData icon;
  final IconData selectedIcon;
  final Widget page;
  final int badgeCount;
}
