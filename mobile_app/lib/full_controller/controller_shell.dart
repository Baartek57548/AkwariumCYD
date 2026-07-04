import 'dart:async';

import 'package:flutter/material.dart';

import '../display_refresh_rate.dart';
import 'controller_api.dart';
import 'controller_session.dart';
import 'views/automation_view.dart';
import 'views/charts_view.dart';
import 'views/dashboard_view.dart';
import 'views/diagnostics_view.dart';
import 'views/logs_view.dart';
import 'views/relays_view.dart';
import 'views/schedules_view.dart';
import 'views/settings_view.dart';
import 'views/system_view.dart';

typedef RunControllerAction =
    Future<ControllerActionResult> Function(
      String name, {
      Map<String, Object?> payload,
      String? confirmation,
      bool refreshAfter,
    });

class ControllerShell extends StatefulWidget {
  const ControllerShell({super.key, required this.session});

  final ControllerSession session;

  @override
  State<ControllerShell> createState() => _ControllerShellState();
}

class _ControllerShellState extends State<ControllerShell> {
  _ControllerSection _section = _ControllerSection.dashboard;

  ControllerSession get session => widget.session;

  @override
  void initState() {
    super.initState();
    session.addListener(_onSessionChanged);
    unawaited(session.connect());
  }

  void _onSessionChanged() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    session.removeListener(_onSessionChanged);
    session.dispose();
    super.dispose();
  }

  Future<bool> _ensureAdmin() async {
    if (session.isAdmin) return true;
    final pin = await showDialog<String>(
      context: context,
      barrierDismissible: false,
      builder: (_) => const _AdminPinDialog(),
    );
    if (pin == null || !mounted) return false;
    try {
      final result = await session.login(pin);
      if (mounted) _showMessage(result.message, success: true);
      return true;
    } on ControllerApiException catch (error) {
      if (mounted) _showMessage(error.message, success: false);
      return false;
    }
  }

  Future<ControllerActionResult> _runAction(
    String name, {
    Map<String, Object?> payload = const {},
    String? confirmation,
    bool refreshAfter = true,
  }) async {
    if (!await _ensureAdmin()) {
      throw const ControllerApiException(
        code: 'admin_cancelled',
        message: 'Operacja została anulowana.',
      );
    }
    if (confirmation != null && mounted) {
      final accepted = await showDialog<bool>(
        context: context,
        builder: (context) => AlertDialog(
          title: const Text('Potwierdzenie operacji'),
          content: Text(confirmation),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context, false),
              child: const Text('Anuluj'),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(context, true),
              child: const Text('Potwierdź'),
            ),
          ],
        ),
      );
      if (accepted != true) {
        throw const ControllerApiException(
          code: 'action_cancelled',
          message: 'Operacja została anulowana.',
        );
      }
    }
    try {
      final result = await session.action(
        name,
        payload: payload,
        refreshAfter: refreshAfter,
      );
      if (mounted) _showMessage(result.message, success: result.success);
      return result;
    } on ControllerApiException catch (error) {
      if (mounted && error.code != 'action_cancelled') {
        _showMessage(error.message, success: false);
      }
      rethrow;
    }
  }

  void _showMessage(String message, {required bool success}) {
    final colors = Theme.of(context).colorScheme;
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(
        SnackBar(
          backgroundColor: success ? colors.primary : colors.error,
          content: Text(message),
        ),
      );
  }

  Widget _currentView() {
    return switch (_section) {
      _ControllerSection.dashboard => DashboardView(
        session: session,
        runAction: _runAction,
      ),
      _ControllerSection.charts => ChartsView(session: session),
      _ControllerSection.relays => RelaysView(
        session: session,
        runAction: _runAction,
        ensureAdmin: _ensureAdmin,
      ),
      _ControllerSection.automation => AutomationView(
        session: session,
        runAction: _runAction,
      ),
      _ControllerSection.schedules => SchedulesView(
        session: session,
        runAction: _runAction,
      ),
      _ControllerSection.system => SystemView(
        session: session,
        runAction: _runAction,
        ensureAdmin: _ensureAdmin,
      ),
      _ControllerSection.logs => LogsView(
        session: session,
        runAction: _runAction,
        ensureAdmin: _ensureAdmin,
      ),
      _ControllerSection.diagnostics => DiagnosticsView(
        session: session,
        ensureAdmin: _ensureAdmin,
      ),
      _ControllerSection.settings => SettingsView(
        session: session,
        runAction: _runAction,
        ensureAdmin: _ensureAdmin,
      ),
    };
  }

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.sizeOf(context).width;
    final refreshProfile = DisplayRefreshRateScope.of(context);
    final extended = width >= 1100;
    final showRail = width >= 760;
    return Scaffold(
      appBar: AppBar(
        title: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(_section.label),
            Text(
              session.connected
                  ? '${session.displayName} · połączono'
                  : '${session.displayName} · rozłączono',
              style: Theme.of(context).textTheme.labelSmall,
            ),
          ],
        ),
        actions: [
          if (width >= 620)
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 4),
              child: Tooltip(
                message: refreshProfile.description,
                child: Chip(
                  avatar: const Icon(Icons.speed_rounded, size: 17),
                  label: Text('${refreshProfile.roundedHertz} Hz'),
                ),
              ),
            ),
          if (session.isDevelopment)
            const Padding(
              padding: EdgeInsets.symmetric(horizontal: 8),
              child: Chip(
                avatar: Icon(Icons.science_outlined, size: 18),
                label: Text('DEV'),
              ),
            ),
          IconButton(
            onPressed: session.busy
                ? null
                : () => unawaited(session.refresh(includeHistory: true)),
            tooltip: 'Odśwież dane',
            icon: session.busy
                ? const SizedBox.square(
                    dimension: 20,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : const Icon(Icons.refresh_rounded),
          ),
          IconButton(
            onPressed: session.isAdmin
                ? () {
                    session.logout();
                    _showMessage('Wylogowano administratora.', success: true);
                  }
                : () => unawaited(_ensureAdmin()),
            tooltip: session.isAdmin
                ? 'Wyloguj administratora'
                : 'Zaloguj administratora',
            icon: Icon(
              session.isAdmin
                  ? Icons.lock_open_rounded
                  : Icons.lock_outline_rounded,
            ),
          ),
        ],
      ),
      drawer: showRail
          ? null
          : _MobileDrawer(
              selected: _section,
              onSelected: (value) {
                setState(() => _section = value);
                Navigator.pop(context);
              },
            ),
      body: Row(
        children: [
          if (showRail)
            NavigationRail(
              extended: extended,
              selectedIndex: _section.index,
              onDestinationSelected: (index) {
                setState(() => _section = _ControllerSection.values[index]);
              },
              labelType: extended
                  ? NavigationRailLabelType.none
                  : NavigationRailLabelType.selected,
              destinations: [
                for (final item in _ControllerSection.values)
                  NavigationRailDestination(
                    icon: Icon(item.icon),
                    selectedIcon: Icon(item.selectedIcon),
                    label: Text(item.label),
                  ),
              ],
            ),
          if (showRail) const VerticalDivider(width: 1),
          Expanded(
            child: session.status.isEmpty
                ? _ConnectionFailure(
                    message: session.error,
                    retry: () => session.connect(),
                  )
                : Stack(
                    children: [
                      Positioned.fill(
                        child: AnimatedSwitcher(
                          duration: refreshProfile.transitionDuration,
                          reverseDuration:
                              refreshProfile.shortAnimationDuration,
                          switchInCurve: Curves.easeOutCubic,
                          switchOutCurve: Curves.easeInCubic,
                          transitionBuilder: (child, animation) {
                            final offset = Tween<Offset>(
                              begin: const Offset(0.015, 0),
                              end: Offset.zero,
                            ).animate(animation);
                            return FadeTransition(
                              opacity: animation,
                              child: SlideTransition(
                                position: offset,
                                child: child,
                              ),
                            );
                          },
                          child: KeyedSubtree(
                            key: ValueKey(_section),
                            child: _currentView(),
                          ),
                        ),
                      ),
                      if (!session.connected && session.error != null)
                        Positioned(
                          left: 12,
                          right: 12,
                          bottom: 12,
                          child: MaterialBanner(
                            content: Text(session.error!),
                            leading: const Icon(Icons.cloud_off_rounded),
                            actions: [
                              TextButton(
                                onPressed: () => session.connect(),
                                child: const Text('Połącz ponownie'),
                              ),
                            ],
                          ),
                        ),
                    ],
                  ),
          ),
        ],
      ),
    );
  }
}

enum _ControllerSection {
  dashboard('Pulpit', Icons.dashboard_outlined, Icons.dashboard_rounded),
  charts('Wykresy', Icons.show_chart_rounded, Icons.area_chart_rounded),
  relays('Przekaźniki', Icons.cable_outlined, Icons.cable_rounded),
  automation('Automatyka', Icons.auto_mode_outlined, Icons.auto_mode_rounded),
  schedules('Harmonogramy', Icons.schedule_outlined, Icons.schedule_rounded),
  system(
    'Zasilanie i OTA',
    Icons.battery_charging_full_outlined,
    Icons.battery_charging_full_rounded,
  ),
  logs('Logi', Icons.receipt_long_outlined, Icons.receipt_long_rounded),
  diagnostics(
    'Diagnostyka',
    Icons.monitor_heart_outlined,
    Icons.monitor_heart_rounded,
  ),
  settings('Ustawienia', Icons.settings_outlined, Icons.settings_rounded);

  const _ControllerSection(this.label, this.icon, this.selectedIcon);

  final String label;
  final IconData icon;
  final IconData selectedIcon;
}

class _MobileDrawer extends StatelessWidget {
  const _MobileDrawer({required this.selected, required this.onSelected});

  final _ControllerSection selected;
  final ValueChanged<_ControllerSection> onSelected;

  @override
  Widget build(BuildContext context) {
    return NavigationDrawer(
      selectedIndex: selected.index,
      onDestinationSelected: (index) =>
          onSelected(_ControllerSection.values[index]),
      children: [
        const Padding(
          padding: EdgeInsets.fromLTRB(28, 24, 16, 12),
          child: Text(
            'cydAkwarium',
            style: TextStyle(fontSize: 24, fontWeight: FontWeight.w800),
          ),
        ),
        for (final item in _ControllerSection.values)
          NavigationDrawerDestination(
            icon: Icon(item.icon),
            selectedIcon: Icon(item.selectedIcon),
            label: Text(item.label),
          ),
      ],
    );
  }
}

class _AdminPinDialog extends StatefulWidget {
  const _AdminPinDialog();

  @override
  State<_AdminPinDialog> createState() => _AdminPinDialogState();
}

class _AdminPinDialogState extends State<_AdminPinDialog> {
  final TextEditingController controller = TextEditingController();
  String? error;

  @override
  void dispose() {
    controller.dispose();
    super.dispose();
  }

  void submit() {
    final value = controller.text.trim();
    if (!RegExp(r'^\d{4,8}$').hasMatch(value)) {
      setState(() => error = 'PIN musi zawierać od 4 do 8 cyfr.');
      return;
    }
    Navigator.pop(context, value);
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('Logowanie administratora'),
      content: TextField(
        controller: controller,
        autofocus: true,
        obscureText: true,
        keyboardType: TextInputType.number,
        maxLength: 8,
        decoration: InputDecoration(labelText: 'PIN', errorText: error),
        onSubmitted: (_) => submit(),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: const Text('Anuluj'),
        ),
        FilledButton(onPressed: submit, child: const Text('Zaloguj')),
      ],
    );
  }
}

class _ConnectionFailure extends StatelessWidget {
  const _ConnectionFailure({required this.message, required this.retry});

  final String? message;
  final Future<void> Function() retry;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.cloud_off_rounded, size: 72),
            const SizedBox(height: 16),
            Text(
              message ?? 'Łączenie ze sterownikiem…',
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 18),
            FilledButton.icon(
              onPressed: retry,
              icon: const Icon(Icons.refresh_rounded),
              label: const Text('Spróbuj ponownie'),
            ),
          ],
        ),
      ),
    );
  }
}
