import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../design_system.dart';
import 'connection_health.dart';
import 'connection_health_bar.dart';
import 'controller_api.dart';
import 'controller_session.dart';
import 'data_access.dart';
import 'widgets.dart';
import 'views/automation_center_view.dart';
import 'views/control_hub_view.dart';
import 'views/dashboard_view.dart';
import 'views/insights_center_view.dart';
import 'views/settings_hub_view.dart';

typedef RunControllerAction =
    Future<ControllerActionResult> Function(
      String name, {
      Map<String, Object?> payload,
      String? confirmation,
      bool refreshAfter,
    });

class ControllerShell extends StatefulWidget {
  const ControllerShell({
    super.key,
    required this.session,
    this.onOpenConnection,
    this.disposeSession = true,
  });

  final ControllerSession session;
  final VoidCallback? onOpenConnection;
  final bool disposeSession;

  @override
  State<ControllerShell> createState() => _ControllerShellState();
}

class _ControllerShellState extends State<ControllerShell>
    with WidgetsBindingObserver {
  _ControllerSection _section = _ControllerSection.dashboard;
  bool _actionPipelineBusy = false;

  ControllerSession get session => widget.session;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    if (widget.disposeSession) _activateSessionForCurrentLifecycle();
  }

  @override
  void didUpdateWidget(ControllerShell oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (identical(oldWidget.session, session)) return;
    if (oldWidget.disposeSession) oldWidget.session.dispose();
    if (widget.disposeSession) _activateSessionForCurrentLifecycle();
  }

  void _activateSessionForCurrentLifecycle() {
    final lifecycleState = WidgetsBinding.instance.lifecycleState;
    final resumed =
        lifecycleState == null || lifecycleState == AppLifecycleState.resumed;
    session.setAppActive(resumed);
    if (resumed) unawaited(session.connect());
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (widget.disposeSession) {
      session.setAppActive(state == AppLifecycleState.resumed);
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    if (widget.disposeSession) session.dispose();
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
    if (_actionPipelineBusy) {
      const message =
          'Inna operacja oczekuje na potwierdzenie albo jest już wykonywana.';
      if (mounted) _showMessage(message, success: false);
      throw ControllerApiException(
        code: 'action_in_progress',
        message: message,
      );
    }
    _actionPipelineBusy = true;
    try {
      if (!session.canIssueCommands) {
        final message =
            session.commandBlockReason ??
            'Sterownik nie jest gotowy do wykonania polecenia.';
        if (mounted) _showMessage(message, success: false);
        throw ControllerApiException(
          code: 'controller_unavailable',
          message: message,
        );
      }
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
      final result = await session.action(
        name,
        payload: payload,
        refreshAfter: refreshAfter,
      );
      if (mounted) _showMessage(result.message, success: result.success);
      return result;
    } on ControllerApiException catch (error) {
      if (mounted &&
          error.code != 'action_cancelled' &&
          error.code != 'admin_cancelled') {
        _showMessage(error.message, success: false);
      }
      rethrow;
    } finally {
      _actionPipelineBusy = false;
    }
  }

  void _showMessage(String message, {required bool success}) {
    final colors = Theme.of(context).colorScheme;
    unawaited(_performHapticFeedback(success));
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(
        SnackBar(
          backgroundColor: success ? colors.primary : colors.error,
          behavior: SnackBarBehavior.floating,
          content: Row(
            children: [
              Icon(
                success ? Icons.check_circle_rounded : Icons.error_rounded,
                color: success ? colors.onPrimary : colors.onError,
              ),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  message,
                  style: TextStyle(
                    color: success ? colors.onPrimary : colors.onError,
                  ),
                ),
              ),
            ],
          ),
        ),
      );
  }

  Future<void> _performHapticFeedback(bool success) async {
    try {
      await (success
          ? HapticFeedback.lightImpact()
          : HapticFeedback.mediumImpact());
    } on MissingPluginException {
      // Haptyka nie jest dostępna na każdej platformie uruchomieniowej.
    } on PlatformException {
      // Brak haptyki nie może zmienić wyniku operacji sterownika.
    }
  }

  void _openCharts() {
    if (_section == _ControllerSection.insights) return;
    setState(() => _section = _ControllerSection.insights);
  }

  void _openControls() {
    if (_section == _ControllerSection.control) return;
    setState(() => _section = _ControllerSection.control);
  }

  List<Widget> _sectionViews() {
    return [
      _ActiveSessionView(
        key: ValueKey((_ControllerSection.dashboard, session)),
        session: session,
        active: _section == _ControllerSection.dashboard,
        builder: (context, session) => DashboardView(
          session: session,
          onOpenCharts: _openCharts,
          onOpenControls: _openControls,
        ),
      ),
      _ActiveSessionView(
        key: ValueKey((_ControllerSection.control, session)),
        session: session,
        active: _section == _ControllerSection.control,
        builder: (context, session) => ControlHubView(
          session: session,
          runAction: _runAction,
          ensureAdmin: _ensureAdmin,
        ),
      ),
      _ActiveSessionView(
        key: ValueKey((_ControllerSection.automation, session)),
        session: session,
        active: _section == _ControllerSection.automation,
        builder: (context, session) => session.isLegacyBluetooth
            ? const _CapabilityUnavailable(
                icon: Icons.auto_mode_rounded,
                title: 'Automatyka wymaga pełnego API',
                message:
                    'BLE v1 udostępnia tylko telemetrię i podstawowe polecenia. '
                    'Połącz sterownik przez Wi‑Fi albo zaktualizuj firmware do BLE v2.',
              )
            : AutomationCenterView(session: session, runAction: _runAction),
      ),
      _ActiveSessionView(
        key: ValueKey((_ControllerSection.insights, session)),
        session: session,
        active: _section == _ControllerSection.insights,
        builder: (context, session) => session.isLegacyBluetooth
            ? const _CapabilityUnavailable(
                icon: Icons.monitor_heart_rounded,
                title: 'Historia jest niedostępna przez BLE v1',
                message:
                    'Wykresy, archiwa SD i dziennik zdarzeń wymagają połączenia '
                    'Wi‑Fi albo protokołu BLE v2.',
              )
            : InsightsCenterView(
                session: session,
                runAction: _runAction,
                ensureAdmin: _ensureAdmin,
              ),
      ),
      _ActiveSessionView(
        key: ValueKey((_ControllerSection.settings, session)),
        session: session,
        active: _section == _ControllerSection.settings,
        builder: (context, session) => SettingsHubView(
          session: session,
          runAction: _runAction,
          ensureAdmin: _ensureAdmin,
          onOpenConnection: widget.onOpenConnection,
        ),
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
        final openConnection = widget.onOpenConnection;
        if (openConnection != null) {
          openConnection();
        } else if (mounted && Navigator.of(context).canPop()) {
          Navigator.of(context).pop();
        }
    }
  }

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.sizeOf(context).width;
    final showRail = width >= 760;
    final extendedRail = width >= 1100;
    final textScale = MediaQuery.textScalerOf(context).scale(1);
    final toolbarHeight = (72 + (textScale - 1) * 18).clamp(72, 108).toDouble();
    final navigationHeight = (72 + (textScale - 1) * 18)
        .clamp(72, 110)
        .toDouble();
    return Scaffold(
      appBar: AppBar(
        toolbarHeight: toolbarHeight,
        titleSpacing: 16,
        title: ListenableBuilder(
          listenable: session,
          builder: (context, _) => _ControllerHeader(session: session),
        ),
        actions: [
          ListenableBuilder(
            listenable: session,
            builder: (context, _) => PopupMenuButton<_HeaderAction>(
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
          ),
          ListenableBuilder(
            listenable: session,
            builder: (context, _) {
              final colors = Theme.of(context).colorScheme;
              final online = session.connectionHealth.isOnline;
              final icon = online
                  ? switch (session.sessionKind) {
                      ControllerSessionKind.bluetooth =>
                        Icons.bluetooth_connected_rounded,
                      ControllerSessionKind.wifi => Icons.wifi_rounded,
                      ControllerSessionKind.offline => Icons.history_rounded,
                      ControllerSessionKind.development =>
                        Icons.science_rounded,
                    }
                  : Icons.link_off_rounded;
              return IconButton(
                key: const Key('connection-center-button'),
                tooltip: 'Połączenia Wi‑Fi i Bluetooth',
                onPressed: widget.onOpenConnection,
                icon: Badge(
                  backgroundColor: online
                      ? context.statusColors.success
                      : colors.outline,
                  smallSize: 9,
                  child: Icon(icon),
                ),
              );
            },
          ),
          const SizedBox(width: 4),
        ],
      ),
      body: Column(
        children: [
          ControllerConnectionHealthBar(session: session),
          Expanded(
            child: Row(
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
                  child: ListenableBuilder(
                    listenable: session,
                    child: IndexedStack(
                      index: _section.index,
                      children: _sectionViews(),
                    ),
                    builder: (context, child) {
                      final health = session.connectionHealth;
                      if (!health.hasTelemetry && session.status.isEmpty) {
                        return _ConnectionState(
                          message: session.error,
                          phase: health.phase,
                          retry: session.connect,
                        );
                      }
                      return Stack(
                        children: [
                          Positioned.fill(child: child!),
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
                          if (!health.isOnline && session.error != null)
                            Positioned(
                              left: 12,
                              right: 12,
                              bottom: 12,
                              child: MaterialBanner(
                                content: Text(session.error!),
                                leading: const Icon(Icons.cloud_off_rounded),
                                actions: [
                                  TextButton(
                                    onPressed: session.busy
                                        ? null
                                        : () => unawaited(session.connect()),
                                    child: const Text('Połącz ponownie'),
                                  ),
                                ],
                              ),
                            ),
                        ],
                      );
                    },
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
              height: navigationHeight,
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

typedef _SessionViewBuilder =
    Widget Function(BuildContext context, ControllerSession session);

class _ActiveSessionView extends StatefulWidget {
  const _ActiveSessionView({
    super.key,
    required this.session,
    required this.active,
    required this.builder,
  });

  final ControllerSession session;
  final bool active;
  final _SessionViewBuilder builder;

  @override
  State<_ActiveSessionView> createState() => _ActiveSessionViewState();
}

class _ActiveSessionViewState extends State<_ActiveSessionView> {
  late bool _hasBeenActivated;

  @override
  void initState() {
    super.initState();
    _hasBeenActivated = widget.active && widget.session.status.isNotEmpty;
    if (widget.active) widget.session.addListener(_onSessionChanged);
  }

  @override
  void didUpdateWidget(_ActiveSessionView oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.active) {
      oldWidget.session.removeListener(_onSessionChanged);
    }
    if (widget.active) {
      widget.session.addListener(_onSessionChanged);
      if (widget.session.status.isNotEmpty) _hasBeenActivated = true;
    }
  }

  void _onSessionChanged() {
    if (!mounted || !widget.active) return;
    setState(() {
      if (widget.session.status.isNotEmpty) _hasBeenActivated = true;
    });
  }

  @override
  void dispose() {
    if (widget.active) widget.session.removeListener(_onSessionChanged);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!_hasBeenActivated) return const SizedBox.shrink();
    return widget.builder(context, widget.session);
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
      ControllerSessionKind.offline => (Icons.history_rounded, 'Offline'),
      ControllerSessionKind.development => (Icons.science_outlined, 'DEV'),
      ControllerSessionKind.wifi => (Icons.wifi_rounded, 'Wi‑Fi'),
    };
    final colors = Theme.of(context).colorScheme;
    final phase = session.connectionPhase;
    final connectionIcon = switch (phase) {
      ControllerConnectionPhase.connecting => Icons.sync_rounded,
      ControllerConnectionPhase.online => transportIcon,
      ControllerConnectionPhase.reconnecting =>
        Icons.wifi_protected_setup_rounded,
      ControllerConnectionPhase.offline => Icons.cloud_off_rounded,
    };
    final connectionLabel = switch (phase) {
      ControllerConnectionPhase.connecting => 'Łączenie',
      ControllerConnectionPhase.online => transportLabel,
      ControllerConnectionPhase.reconnecting => 'Ponawianie',
      ControllerConnectionPhase.offline => 'Offline',
    };
    final connectionTone = switch (phase) {
      ControllerConnectionPhase.connecting => colors.tertiary,
      ControllerConnectionPhase.online => colors.primary,
      ControllerConnectionPhase.reconnecting => colors.tertiary,
      ControllerConnectionPhase.offline =>
        session.isOfflineMode ? colors.outline : colors.error,
    };
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
              label: switch (phase) {
                ControllerConnectionPhase.connecting =>
                  'Łączenie ze sterownikiem',
                ControllerConnectionPhase.online =>
                  'Połączenie aktywne: $transportLabel',
                ControllerConnectionPhase.reconnecting =>
                  'Trwa ponawianie połączenia',
                ControllerConnectionPhase.offline => 'Sterownik jest offline',
              },
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
                        Icon(connectionIcon, size: 20, color: connectionTone),
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
  dashboard('Centrum', Icons.dashboard_outlined, Icons.dashboard_rounded),
  control('Steruj', Icons.tune_outlined, Icons.tune_rounded),
  automation('Auto', Icons.auto_mode_outlined, Icons.auto_mode_rounded),
  insights(
    'Historia',
    Icons.monitor_heart_outlined,
    Icons.monitor_heart_rounded,
  ),
  settings('System', Icons.settings_outlined, Icons.settings_rounded);

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
    required this.phase,
    required this.retry,
  });

  final String? message;
  final ControllerConnectionPhase phase;
  final Future<void> Function() retry;

  @override
  Widget build(BuildContext context) {
    if (phase == ControllerConnectionPhase.connecting ||
        phase == ControllerConnectionPhase.reconnecting) {
      return const StatePanel.loading(
        title: 'Łączenie ze sterownikiem',
        message:
            'Pobieramy bieżący stan urządzenia. W razie utraty sieci połączenie zostanie ponowione automatycznie.',
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

class _CapabilityUnavailable extends StatelessWidget {
  const _CapabilityUnavailable({
    required this.icon,
    required this.title,
    required this.message,
  });

  final IconData icon;
  final String title;
  final String message;

  @override
  Widget build(BuildContext context) {
    return StatePanel.empty(icon: icon, title: title, message: message);
  }
}
