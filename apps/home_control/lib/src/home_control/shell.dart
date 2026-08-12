import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:flutter/material.dart';

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

    final destinations = <_ShellDestination>[
      _ShellDestination(
        label: strings.t('dashboard'),
        icon: Icons.space_dashboard_outlined,
        selectedIcon: Icons.space_dashboard_rounded,
        page: HomeDashboardPage(controller: widget.controller),
      ),
      _ShellDestination(
        label: strings.t('rooms'),
        icon: Icons.meeting_room_outlined,
        selectedIcon: Icons.meeting_room_rounded,
        page: RoomsPage(controller: widget.controller),
      ),
      _ShellDestination(
        label: strings.t('devices'),
        icon: Icons.devices_other_outlined,
        selectedIcon: Icons.devices_other_rounded,
        page: DevicesPage(controller: widget.controller),
      ),
      _ShellDestination(
        label: strings.t('automations'),
        icon: Icons.account_tree_outlined,
        selectedIcon: Icons.account_tree_rounded,
        page: AutomationsPage(controller: widget.controller),
      ),
      _ShellDestination(
        label: strings.t('updates'),
        icon: Icons.system_update_outlined,
        selectedIcon: Icons.system_update_rounded,
        page: UpdatesPage(controller: widget.controller),
      ),
      _ShellDestination(
        label: strings.t('settings'),
        icon: Icons.settings_outlined,
        selectedIcon: Icons.settings_rounded,
        page: SettingsPage(controller: widget.controller),
      ),
    ];

    return LayoutBuilder(
      builder: (context, constraints) {
        final useRail = constraints.maxWidth >= 840;
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
          ),
          body: useRail
              ? Row(
                  children: <Widget>[
                    SafeArea(
                      top: false,
                      child: NavigationRail(
                        extended: extendedRail,
                        selectedIndex: _selectedIndex,
                        labelType: extendedRail
                            ? NavigationRailLabelType.none
                            : NavigationRailLabelType.all,
                        onDestinationSelected: _selectDestination,
                        leading: Padding(
                          padding: const EdgeInsets.symmetric(vertical: 12),
                          child: HomeControlMark(size: extendedRail ? 52 : 44),
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
                      ),
                    ),
                    const VerticalDivider(width: 1),
                    Expanded(child: content),
                  ],
                )
              : content,
          bottomNavigationBar: useRail
              ? null
              : NavigationBar(
                  selectedIndex: _selectedIndex,
                  onDestinationSelected: _selectDestination,
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
      },
    );
  }

  void _selectDestination(int value) {
    if (_selectedIndex == value) return;
    setState(() => _selectedIndex = value);
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
  const _HomeControlAppBar({required this.title, required this.controller});

  final String title;
  final HomeControlController controller;

  @override
  Size get preferredSize => const Size.fromHeight(kToolbarHeight);

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final sourceName = switch (controller.activeSourceKind) {
      null => strings.t('unknown'),
      final kind => strings.sourceName(kind),
    };
    return AppBar(
      titleSpacing: ProductSpacing.md,
      title: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: <Widget>[
          Text(title, maxLines: 1, overflow: TextOverflow.ellipsis),
          Text(
            sourceName,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: Theme.of(context).textTheme.labelSmall,
          ),
        ],
      ),
      actions: <Widget>[
        Semantics(
          button: true,
          label: strings.t('refresh'),
          child: IconButton(
            tooltip: strings.t('refresh'),
            onPressed: controller.refreshing
                ? null
                : () => controller.refresh(announce: true),
            icon: controller.refreshing
                ? const SizedBox.square(
                    dimension: 22,
                    child: CircularProgressIndicator(strokeWidth: 2.5),
                  )
                : const Icon(Icons.refresh_rounded),
          ),
        ),
        const SizedBox(width: ProductSpacing.xs),
      ],
    );
  }
}

final class _ShellDestination {
  const _ShellDestination({
    required this.label,
    required this.icon,
    required this.selectedIcon,
    required this.page,
  });

  final String label;
  final IconData icon;
  final IconData selectedIcon;
  final Widget page;
}
