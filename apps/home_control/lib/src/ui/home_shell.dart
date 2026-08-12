import 'package:flutter/material.dart';

import '../state/aquacyd_controller.dart';
import 'pages/alarms_page.dart';
import 'pages/automation_page.dart';
import 'pages/control_page.dart';
import 'pages/dashboard_page.dart';
import 'pages/history_page.dart';
import 'pages/system_page.dart';
import 'widgets/common.dart';

class HomeShell extends StatefulWidget {
  const HomeShell({required this.controller, super.key});

  final AquaCydController controller;

  @override
  State<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends State<HomeShell> {
  var _selectedIndex = 0;

  static const _destinations = <_Destination>[
    _Destination(
      'Start',
      Icons.space_dashboard_outlined,
      Icons.space_dashboard,
    ),
    _Destination('Sterowanie', Icons.tune_outlined, Icons.tune),
    _Destination('Historia', Icons.show_chart_outlined, Icons.show_chart),
    _Destination('Alarmy', Icons.shield_outlined, Icons.shield),
    _Destination('Automatyka', Icons.schedule_outlined, Icons.schedule),
    _Destination('System', Icons.settings_outlined, Icons.settings),
  ];

  @override
  Widget build(BuildContext context) {
    final operationMessage = widget.controller.takeOperationMessage();
    if (operationMessage != null) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (!mounted) {
          return;
        }
        ScaffoldMessenger.of(context)
          ..hideCurrentSnackBar()
          ..showSnackBar(
            SnackBar(
              content: Text(operationMessage),
              behavior: SnackBarBehavior.floating,
            ),
          );
      });
    }

    final pages = <Widget>[
      DashboardPage(
        controller: widget.controller,
        showAlarms: () => _select(3),
      ),
      ControlPage(controller: widget.controller),
      HistoryPage(controller: widget.controller),
      AlarmsPage(controller: widget.controller),
      AutomationPage(controller: widget.controller),
      SystemPage(controller: widget.controller),
    ];

    return LayoutBuilder(
      builder: (context, constraints) {
        final wide = constraints.maxWidth >= 860;
        return Scaffold(
          appBar: AppBar(
            title: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                const Text('AquaCYD Home'),
                if (wide)
                  Text(
                    widget.controller.config?.locationName ?? 'Home Assistant',
                    style: Theme.of(context).textTheme.labelMedium?.copyWith(
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                    ),
                  ),
              ],
            ),
            actions: <Widget>[
              ConnectionBadge(status: widget.controller.socketStatus),
              const SizedBox(width: 8),
              IconButton(
                tooltip: 'Odśwież',
                onPressed: widget.controller.refreshing
                    ? null
                    : () => widget.controller.refresh(showMessage: true),
                icon: widget.controller.refreshing
                    ? const SizedBox(
                        width: 20,
                        height: 20,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : const Icon(Icons.refresh_rounded),
              ),
              const SizedBox(width: 8),
            ],
          ),
          body: Row(
            children: <Widget>[
              if (wide)
                NavigationRail(
                  selectedIndex: _selectedIndex,
                  labelType: NavigationRailLabelType.all,
                  onDestinationSelected: _select,
                  leading: Padding(
                    padding: const EdgeInsets.only(bottom: 24),
                    child: Container(
                      width: 42,
                      height: 42,
                      decoration: BoxDecoration(
                        color: Theme.of(context).colorScheme.primaryContainer,
                        borderRadius: BorderRadius.circular(14),
                      ),
                      child: Icon(
                        Icons.water_rounded,
                        color: Theme.of(context).colorScheme.onPrimaryContainer,
                      ),
                    ),
                  ),
                  destinations: [
                    for (final destination in _destinations)
                      NavigationRailDestination(
                        icon: Icon(destination.icon),
                        selectedIcon: Icon(destination.selectedIcon),
                        label: Text(destination.label),
                      ),
                  ],
                ),
              Expanded(
                child: Column(
                  children: <Widget>[
                    if (widget.controller.errorMessage != null)
                      Padding(
                        padding: const EdgeInsets.fromLTRB(18, 8, 18, 0),
                        child: InlineError(
                          message: widget.controller.errorMessage!,
                          onDismiss: widget.controller.clearError,
                        ),
                      ),
                    Expanded(
                      child: IndexedStack(
                        index: _selectedIndex,
                        children: pages,
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
          bottomNavigationBar: wide
              ? null
              : NavigationBar(
                  selectedIndex: _selectedIndex,
                  labelBehavior:
                      NavigationDestinationLabelBehavior.onlyShowSelected,
                  onDestinationSelected: _select,
                  destinations: [
                    for (final destination in _destinations)
                      NavigationDestination(
                        icon: Icon(destination.icon),
                        selectedIcon: Icon(destination.selectedIcon),
                        label: destination.label,
                      ),
                  ],
                ),
        );
      },
    );
  }

  void _select(int value) {
    if (value >= 0 && value < _destinations.length) {
      setState(() => _selectedIndex = value);
    }
  }
}

class _Destination {
  const _Destination(this.label, this.icon, this.selectedIcon);

  final String label;
  final IconData icon;
  final IconData selectedIcon;
}
