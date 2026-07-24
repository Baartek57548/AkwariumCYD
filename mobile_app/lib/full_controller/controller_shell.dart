import 'dart:async';

import 'package:flutter/material.dart';

import 'controller_api.dart';
import 'controller_session.dart';
import 'data_access.dart';
import 'widgets.dart';
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

  List<Widget> _sectionViews() {
    return [
      DashboardView(session: session, onOpenCharts: _openCharts),
      ControlHubView(
        session: session,
        runAction: _runAction,
        ensureAdmin: _ensureAdmin,
      ),
      SchedulesView(session: session, runAction: _runAction),
      SettingsHubView(
        session: session,
        runAction: _runAction,
        ensureAdmin: _ensureAdmin,
      ),
    ];
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
    final textScale = MediaQuery.textScalerOf(context).scale(1);
    final toolbarHeight = (72 + (textScale - 1) * 18).clamp(72, 92).toDouble();
    return Scaffold(
      appBar: AppBar(
        toolbarHeight: toolbarHeight,
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
                ? _ConnectionState(
                    message: session.error,
                    loading: session.error == null,
                    retry: session.connect,
                  )
                : Stack(
                    children: [
                      Positioned.fill(
                        child: IndexedStack(
                          index: _section.index,
                          children: _sectionViews(),
                        ),
                      ),
                      if (session.busy)
                        const Positioned(
                          left: 0,
                          right: 0,
                          top: 0,
                          child: LinearProgressIndicator(
                            minHeight: 3,
                            semanticsLabel: 'Aktualizowanie danych',
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
    final deviceName = status.text('device', 'cydAkwarium');
    final address =
        session.baseUri?.host ??
        status.section('network').text('ip', 'akwarium.local');
    final (transportIcon, transportLabel) = switch (session.sessionKind) {
      ControllerSessionKind.bluetooth => (Icons.bluetooth_rounded, 'BLE'),
      ControllerSessionKind.development => (Icons.science_outlined, 'DEV'),
      ControllerSessionKind.wifi => (Icons.wifi_rounded, 'Wi‑Fi'),
    };
    final connecting = status.isEmpty && session.error == null;
    final connectionIcon = connecting
        ? Icons.sync_rounded
        : session.connected
        ? transportIcon
        : Icons.cloud_off_rounded;
    final connectionLabel = connecting
        ? 'Łączenie'
        : session.connected
        ? transportLabel
        : 'Offline';
    final colors = Theme.of(context).colorScheme;
    return LayoutBuilder(
      builder: (context, constraints) {
        final textScale = MediaQuery.textScalerOf(context).scale(1);
        final showLogo = constraints.maxWidth >= 250 && textScale <= 1.6;
        final showAddress = constraints.maxWidth >= 210 && textScale <= 1.4;
        final showConnectionLabel =
            constraints.maxWidth >= 330 && textScale <= 1.2;
        return Row(
          children: [
            if (showLogo) ...[
              ExcludeSemantics(
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(5),
                  child: Image.asset(
                    'assets/branding/aquacyd-control-icon.png',
                    width: 44,
                    height: 44,
                    fit: BoxFit.cover,
                  ),
                ),
              ),
              const SizedBox(width: 10),
            ],
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
                  if (showAddress)
                    Text(
                      address,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: colors.onSurfaceVariant,
                      ),
                    ),
                ],
              ),
            ),
            const SizedBox(width: 8),
            Semantics(
              label: connecting
                  ? 'Łączenie ze sterownikiem'
                  : session.connected
                  ? 'Połączenie aktywne: $transportLabel'
                  : 'Sterownik jest offline',
              liveRegion: true,
              child: ExcludeSemantics(
                child: DecoratedBox(
                  decoration: BoxDecoration(
                    border: Border.all(color: colors.outlineVariant),
                    borderRadius: BorderRadius.circular(5),
                  ),
                  child: Padding(
                    padding: EdgeInsets.symmetric(
                      horizontal: showConnectionLabel ? 10 : 8,
                      vertical: 8,
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(
                          connectionIcon,
                          size: 20,
                          color: connecting
                              ? colors.primary
                              : session.connected
                              ? null
                              : colors.error,
                        ),
                        if (showConnectionLabel) ...[
                          const SizedBox(width: 7),
                          Text(
                            connectionLabel,
                            style: const TextStyle(fontWeight: FontWeight.w700),
                          ),
                        ],
                      ],
                    ),
                  ),
                ),
              ),
            ),
          ],
        );
      },
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

class _ConnectionState extends StatelessWidget {
  const _ConnectionState({
    required this.message,
    required this.loading,
    required this.retry,
  });

  final String? message;
  final bool loading;
  final Future<void> Function() retry;

  @override
  Widget build(BuildContext context) {
    if (loading) {
      return const StatePanel.loading(
        title: 'Łączenie ze sterownikiem',
        message: 'Pobieramy bieżący stan urządzenia. To może potrwać chwilę.',
      );
    }
    return StatePanel.error(
      title: 'Nie udało się połączyć',
      message: message ?? 'Sterownik nie odpowiedział.',
      icon: Icons.cloud_off_rounded,
      actionLabel: 'Spróbuj ponownie',
      onAction: retry,
    );
  }
}
