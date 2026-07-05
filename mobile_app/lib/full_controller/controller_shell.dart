import 'dart:async';

import 'package:flutter/material.dart';

import '../display_refresh_rate.dart';
import 'controller_api.dart';
import 'controller_session.dart';
import 'data_access.dart';
import 'views/charts_view.dart';
import 'views/control_hub_view.dart';
import 'views/dashboard_view.dart';
import 'views/schedules_view.dart';
import 'views/settings_hub_view.dart';

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

  void _openCharts() {
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (context) => Scaffold(
          appBar: AppBar(title: const Text('Historia i wykresy')),
          body: AnimatedBuilder(
            animation: session,
            builder: (context, _) => ChartsView(session: session),
          ),
        ),
      ),
    );
  }

  Widget _currentView() {
    return switch (_section) {
      _ControllerSection.dashboard => DashboardView(
        session: session,
        onOpenCharts: _openCharts,
      ),
      _ControllerSection.control => ControlHubView(
        session: session,
        runAction: _runAction,
        ensureAdmin: _ensureAdmin,
      ),
      _ControllerSection.schedules => SchedulesView(
        session: session,
        runAction: _runAction,
      ),
      _ControllerSection.settings => SettingsHubView(
        session: session,
        runAction: _runAction,
        ensureAdmin: _ensureAdmin,
      ),
    };
  }

  void _selectSection(int index) {
    final next = _ControllerSection.values[index];
    if (next == _section) return;
    setState(() => _section = next);
  }

  Future<void> _handleMenu(_HeaderAction action) async {
    switch (action) {
      case _HeaderAction.refresh:
        await session.refresh(includeHistory: true);
      case _HeaderAction.admin:
        if (session.isAdmin) {
          session.logout();
          if (mounted) {
            _showMessage('Wylogowano administratora.', success: true);
          }
        } else {
          await _ensureAdmin();
        }
      case _HeaderAction.connection:
        if (mounted) Navigator.of(context).pop();
    }
  }

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.sizeOf(context).width;
    final showRail = width >= 760;
    final extendedRail = width >= 1100;
    final refreshProfile = DisplayRefreshRateScope.of(context);
    return Scaffold(
      appBar: AppBar(
        automaticallyImplyLeading: false,
        toolbarHeight: 76,
        titleSpacing: 16,
        title: _ControllerHeader(session: session),
        actions: [
          PopupMenuButton<_HeaderAction>(
            tooltip: 'Menu urządzenia',
            onSelected: (value) => unawaited(_handleMenu(value)),
            itemBuilder: (context) => [
              const PopupMenuItem(
                value: _HeaderAction.refresh,
                child: ListTile(
                  leading: Icon(Icons.refresh_rounded),
                  title: Text('Odśwież dane'),
                  contentPadding: EdgeInsets.zero,
                ),
              ),
              PopupMenuItem(
                value: _HeaderAction.admin,
                child: ListTile(
                  leading: Icon(
                    session.isAdmin
                        ? Icons.lock_open_rounded
                        : Icons.lock_outline_rounded,
                  ),
                  title: Text(
                    session.isAdmin
                        ? 'Wyloguj administratora'
                        : 'Zaloguj administratora',
                  ),
                  contentPadding: EdgeInsets.zero,
                ),
              ),
              const PopupMenuItem(
                value: _HeaderAction.connection,
                child: ListTile(
                  leading: Icon(Icons.swap_horiz_rounded),
                  title: Text('Zmień połączenie'),
                  contentPadding: EdgeInsets.zero,
                ),
              ),
            ],
          ),
          const SizedBox(width: 4),
        ],
      ),
      body: Row(
        children: [
          if (showRail)
            NavigationRail(
              extended: extendedRail,
              selectedIndex: _section.index,
              onDestinationSelected: _selectSection,
              labelType: extendedRail
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
                    retry: session.connect,
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
                          transitionBuilder: (child, animation) =>
                              FadeTransition(opacity: animation, child: child),
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
                                onPressed: session.connect,
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
      bottomNavigationBar: showRail
          ? null
          : NavigationBar(
              selectedIndex: _section.index,
              onDestinationSelected: _selectSection,
              destinations: [
                for (final item in _ControllerSection.values)
                  NavigationDestination(
                    icon: Icon(item.icon),
                    selectedIcon: Icon(item.selectedIcon),
                    label: item.label,
                  ),
              ],
            ),
    );
  }
}

class _ControllerHeader extends StatelessWidget {
  const _ControllerHeader({required this.session});

  final ControllerSession session;

  @override
  Widget build(BuildContext context) {
    final status = session.status;
    final width = MediaQuery.sizeOf(context).width;
    final deviceName = status.text('device', 'cydAkwarium');
    final address =
        session.baseUri?.host ??
        status.section('network').text('ip', 'akwarium.local');
    final (icon, label) = switch (session.sessionKind) {
      ControllerSessionKind.bluetooth => (Icons.bluetooth_rounded, 'BLE'),
      ControllerSessionKind.development => (Icons.science_outlined, 'DEV'),
      ControllerSessionKind.wifi => (Icons.wifi_rounded, 'Wi‑Fi'),
    };
    final colors = Theme.of(context).colorScheme;
    return Row(
      children: [
        ClipRRect(
          borderRadius: BorderRadius.circular(5),
          child: Image.asset(
            'assets/branding/aquacyd-control-icon.png',
            width: 46,
            height: 46,
            fit: BoxFit.cover,
          ),
        ),
        const SizedBox(width: 12),
        Expanded(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                deviceName,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  fontSize: 18,
                  fontWeight: FontWeight.w800,
                ),
              ),
              Text(
                address,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: Theme.of(
                  context,
                ).textTheme.bodySmall?.copyWith(color: colors.onSurfaceVariant),
              ),
            ],
          ),
        ),
        DecoratedBox(
          decoration: BoxDecoration(
            border: Border.all(color: colors.outlineVariant),
            borderRadius: BorderRadius.circular(5),
          ),
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(
                  session.connected ? icon : Icons.cloud_off_rounded,
                  size: 20,
                  color: session.connected ? null : colors.error,
                ),
                if (width >= 340) ...[
                  const SizedBox(width: 7),
                  Text(
                    session.connected ? label : 'Offline',
                    style: const TextStyle(fontWeight: FontWeight.w700),
                  ),
                ],
              ],
            ),
          ),
        ),
      ],
    );
  }
}

enum _ControllerSection {
  dashboard('Pulpit', Icons.speed_outlined, Icons.speed_rounded),
  control('Sterowanie', Icons.tune_outlined, Icons.tune_rounded),
  schedules(
    'Harmonogram',
    Icons.calendar_month_outlined,
    Icons.calendar_month_rounded,
  ),
  settings('Ustawienia', Icons.settings_outlined, Icons.settings_rounded);

  const _ControllerSection(this.label, this.icon, this.selectedIcon);

  final String label;
  final IconData icon;
  final IconData selectedIcon;
}

enum _HeaderAction { refresh, admin, connection }

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
