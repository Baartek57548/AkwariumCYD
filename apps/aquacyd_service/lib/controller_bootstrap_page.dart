import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import 'app_settings.dart';
import 'ble_scanner_page.dart';
import 'controller_page.dart';
import 'controller_preferences.dart';
import 'controller_runtime_services.dart';
import 'controller_snapshot_cache.dart';
import 'design_system.dart';
import 'full_controller/connection_health.dart';
import 'full_controller/controller_api.dart';
import 'full_controller/controller_session.dart';
import 'full_controller/controller_shell.dart';
import 'full_controller/firmware_release_service.dart';
import 'full_controller/status_decoder.dart';
import 'full_controller/wifi_connect_page.dart';
import 'onboarding/onboarding_page.dart';

typedef WifiSessionBuilder =
    ControllerSession Function(
      Uri address,
      Map<String, dynamic>? initialStatus,
      DateTime? cachedAt,
    );

ControllerSession _defaultWifiSessionBuilder(
  Uri address,
  Map<String, dynamic>? initialStatus,
  DateTime? cachedAt,
) {
  return ControllerSession.wifi(
    ControllerApi(
      address,
      requestDeadline: const Duration(seconds: 5),
      maximumReadAttempts: 1,
    ),
    initialStatus: initialStatus,
    cachedAt: cachedAt,
    firmwareReleaseRepository: GitHubFirmwareReleaseService(),
  );
}

/// Tylko zweryfikowany, pełny status może wyczyścić wcześniej zapisane alarmy.
///
/// BLE v1 celowo pozostaje snapshotem częściowym: nie przenosi wszystkich pól
/// bezpieczeństwa (np. zatrzaśniętego timeoutu dolewania). BLE v2, Wi-Fi i DEV
/// przechodzą tę samą walidację kompletności co produkcyjny endpoint HTTP.
@visibleForTesting
bool isCompleteControllerRuntimeStatus(
  Map<String, dynamic> status,
  ControllerSessionKind sessionKind,
) {
  if (sessionKind == ControllerSessionKind.offline) return false;
  if (sessionKind == ControllerSessionKind.bluetooth) {
    final mode = status['mode']?.toString().trim().toUpperCase() ?? '';
    final rawBle = status['ble'];
    final ble = rawBle is Map ? rawBle : const <Object?, Object?>{};
    final rawProtocol =
        ble['protocolVersion'] ??
        ble['protocol_version'] ??
        status['bleProtocolVersion'];
    final protocolVersion = rawProtocol is num
        ? rawProtocol.toInt()
        : int.tryParse(rawProtocol?.toString() ?? '');
    if (mode == 'BLE_V1' ||
        mode == 'BLE1' ||
        mode == 'BLE_LEGACY' ||
        (protocolVersion != null && protocolVersion <= 1)) {
      return false;
    }
  }
  try {
    decodeControllerStatus(status);
    return true;
  } on FormatException {
    return false;
  }
}

/// Offline-first host centrum dowodzenia.
///
/// Pierwsza klatka zawsze pokazuje kompletny interfejs z ostatnim bezpiecznie
/// zapisanym stanem. Jeżeli istnieje zapamiętany adres Wi-Fi, sesja sieciowa
/// przejmuje ten sam snapshot i ponawia połączenie w tle bez blokowania UI.
class ControllerBootstrapPage extends StatefulWidget {
  const ControllerBootstrapPage({
    super.key,
    this.brandName = 'AquaCYD Control',
    this.showDevelopment = false,
    this.showLegacyWebView = false,
    this.preferences,
    this.snapshotCache,
    this.wifiSessionBuilder,
    this.enableOnboarding = kReleaseMode,
    this.runtimeServices,
    this.enableRuntimeServices = kReleaseMode,
  });

  final String brandName;
  final bool showDevelopment;
  final bool showLegacyWebView;
  final ControllerPreferences? preferences;
  final ControllerSnapshotCache? snapshotCache;
  final WifiSessionBuilder? wifiSessionBuilder;
  final bool enableOnboarding;
  final ControllerRuntimeServices? runtimeServices;
  final bool enableRuntimeServices;

  @override
  State<ControllerBootstrapPage> createState() =>
      _ControllerBootstrapPageState();
}

class _ControllerBootstrapPageState extends State<ControllerBootstrapPage>
    with WidgetsBindingObserver {
  static const Duration _snapshotWriteInterval = Duration(seconds: 20);

  late final ControllerPreferences _preferences;
  late final ControllerSnapshotCache _snapshotCache;
  late final WifiSessionBuilder _wifiSessionBuilder;
  ControllerRuntimeServices? _runtimeServices;
  bool _ownsRuntimeServices = false;
  late ControllerSession _session;

  ControllerSnapshot? _snapshot;
  ControllerSnapshot? _pendingSnapshot;
  Uri? _savedAddress;
  Timer? _snapshotTimer;
  Future<bool>? _snapshotWriteOperation;
  DateTime? _lastSnapshotWriteAt;
  DateTime? _lastObservedLiveUpdate;
  String? _storageWarning;
  int _sessionGeneration = 0;
  int? _pendingSnapshotGeneration;
  int _initializationToken = 0;
  bool _autoReconnect = true;
  bool _initializing = true;
  bool _connectionCenterOpen = false;
  bool _onboardingOpen = false;

  @override
  void initState() {
    super.initState();
    _preferences = widget.preferences ?? ControllerPreferences();
    _snapshotCache = widget.snapshotCache ?? ControllerSnapshotCache();
    _wifiSessionBuilder =
        widget.wifiSessionBuilder ?? _defaultWifiSessionBuilder;
    _runtimeServices = widget.runtimeServices;
    if (_runtimeServices == null && widget.enableRuntimeServices) {
      _runtimeServices = ControllerRuntimeServices();
      _ownsRuntimeServices = true;
    }
    final runtimeServices = _runtimeServices;
    if (runtimeServices != null) unawaited(runtimeServices.initialize());
    _session = ControllerSession.offline();
    _session.addListener(_onSessionChanged);
    WidgetsBinding.instance.addObserver(this);
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) unawaited(_initialize());
    });
  }

  Future<void> _initialize() async {
    final token = ++_initializationToken;
    ControllerSnapshot? snapshot;
    Uri? savedAddress;
    var autoReconnect = true;
    String? warning;

    try {
      snapshot = await _snapshotCache.load();
    } on Object {
      warning = 'Nie udało się odczytać ostatnio zapisanego stanu.';
    }
    try {
      savedAddress = await _preferences.loadSavedAddress();
      autoReconnect = await _preferences.loadAutoReconnect();
    } on Object {
      warning =
          'Nie udało się odczytać ustawień połączenia. '
          'Możesz połączyć sterownik ręcznie.';
    }

    if (!mounted || token != _initializationToken) return;

    ControllerSession nextSession;
    try {
      nextSession = savedAddress != null && autoReconnect
          ? _wifiSessionBuilder(
              savedAddress,
              snapshot?.status,
              snapshot?.savedAt,
            )
          : ControllerSession.offline(
              cachedStatus: snapshot?.status,
              cachedAt: snapshot?.savedAt,
            );
    } on Object {
      warning =
          'Automatyczne połączenie nie mogło zostać uruchomione. '
          'Aplikacja pozostaje dostępna offline.';
      nextSession = ControllerSession.offline(
        cachedStatus: snapshot?.status,
        cachedAt: snapshot?.savedAt,
      );
    }

    _installSession(
      nextSession,
      snapshot: snapshot,
      savedAddress: savedAddress,
      autoReconnect: autoReconnect,
      warning: warning,
      initializing: false,
    );
    if (widget.enableOnboarding &&
        !AppSettings.onboardingCompletedNotifier.value) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) unawaited(_openOnboarding());
      });
    }
  }

  Future<void> _openOnboarding() async {
    if (_onboardingOpen ||
        !mounted ||
        AppSettings.onboardingCompletedNotifier.value) {
      return;
    }
    _onboardingOpen = true;
    OnboardingConnectionChoice? choice;
    try {
      choice = await Navigator.of(context).push<OnboardingConnectionChoice>(
        MaterialPageRoute<OnboardingConnectionChoice>(
          fullscreenDialog: true,
          builder: (_) => const OnboardingPage(),
        ),
      );
    } finally {
      _onboardingOpen = false;
    }
    if (!mounted || choice == null) return;
    try {
      await AppSettings.completeOnboarding();
    } on Object {
      if (mounted) {
        _showMessage(
          'Nie udało się zapisać zakończenia przewodnika.',
          isError: true,
        );
      }
    }
    if (!mounted) return;
    switch (choice) {
      case OnboardingConnectionChoice.wifi:
        await _connectWifi();
      case OnboardingConnectionChoice.bluetooth:
        await _connectBluetooth();
      case OnboardingConnectionChoice.offline:
        await _switchOffline();
    }
  }

  void _installSession(
    ControllerSession nextSession, {
    ControllerSnapshot? snapshot,
    Uri? savedAddress,
    bool? autoReconnect,
    String? warning,
    bool? initializing,
  }) {
    if (!mounted) {
      nextSession.dispose();
      return;
    }

    final previousSession = _session;
    previousSession.removeListener(_onSessionChanged);
    previousSession.dispose();
    _snapshotTimer?.cancel();
    _snapshotTimer = null;
    _pendingSnapshot = null;
    _pendingSnapshotGeneration = null;
    _lastObservedLiveUpdate = null;
    _sessionGeneration += 1;
    _session = nextSession;
    _session.addListener(_onSessionChanged);
    final lifecycleState = WidgetsBinding.instance.lifecycleState;
    final resumed =
        lifecycleState == null || lifecycleState == AppLifecycleState.resumed;
    _session.setAppActive(resumed);
    if (resumed) unawaited(_session.connect());

    setState(() {
      if (snapshot != null) _snapshot = snapshot;
      if (savedAddress != null) _savedAddress = savedAddress;
      if (autoReconnect != null) _autoReconnect = autoReconnect;
      _storageWarning = warning;
      if (initializing != null) _initializing = initializing;
    });
  }

  void _onSessionChanged() {
    final session = _session;
    if (!session.connected || session.status.isEmpty) return;
    final liveUpdate = session.lastUpdate;
    if (liveUpdate == null || liveUpdate == _lastObservedLiveUpdate) return;

    _lastObservedLiveUpdate = liveUpdate;
    final completeSnapshot =
        session.connected &&
        session.connectionPhase == ControllerConnectionPhase.online &&
        isCompleteControllerRuntimeStatus(session.status, session.sessionKind);
    _runtimeServices?.observeStatus(
      status: session.status,
      sessionKind: session.sessionKind,
      observedAt: liveUpdate,
      completeSnapshot: completeSnapshot,
    );
    _pendingSnapshot = ControllerSnapshot(
      status: session.status,
      savedAt: liveUpdate,
    );
    _pendingSnapshotGeneration = _sessionGeneration;
    _scheduleSnapshotWrite();
  }

  void _scheduleSnapshotWrite() {
    if (_snapshotWriteOperation != null || _pendingSnapshot == null) return;
    _snapshotTimer?.cancel();

    final lastWrite = _lastSnapshotWriteAt;
    if (lastWrite == null) {
      unawaited(_flushSnapshot());
      return;
    }

    final elapsed = DateTime.now().difference(lastWrite);
    final remaining = _snapshotWriteInterval - elapsed;
    if (remaining <= Duration.zero) {
      unawaited(_flushSnapshot());
      return;
    }
    _snapshotTimer = Timer(remaining, () {
      _snapshotTimer = null;
      unawaited(_flushSnapshot());
    });
  }

  Future<bool> _flushSnapshot({bool force = false}) async {
    if (force) {
      _snapshotTimer?.cancel();
      _snapshotTimer = null;
    }

    final activeWrite = _snapshotWriteOperation;
    if (activeWrite != null) {
      await activeWrite;
      return force ? _flushSnapshot(force: true) : true;
    }

    final snapshot = _pendingSnapshot;
    final generation = _pendingSnapshotGeneration;
    if (snapshot == null || generation == null) return true;
    if (generation != _sessionGeneration) {
      _pendingSnapshot = null;
      _pendingSnapshotGeneration = null;
      return false;
    }

    final lastWrite = _lastSnapshotWriteAt;
    if (!force && lastWrite != null) {
      final elapsed = DateTime.now().difference(lastWrite);
      if (elapsed < _snapshotWriteInterval) {
        _scheduleSnapshotWrite();
        return true;
      }
    }

    _pendingSnapshot = null;
    _pendingSnapshotGeneration = null;
    late final Future<bool> operation;
    operation = _writeSnapshot(snapshot, generation).whenComplete(() {
      if (identical(_snapshotWriteOperation, operation)) {
        _snapshotWriteOperation = null;
      }
    });
    _snapshotWriteOperation = operation;
    final saved = await operation;
    if (_pendingSnapshot != null) _scheduleSnapshotWrite();
    return saved;
  }

  Future<bool> _writeSnapshot(
    ControllerSnapshot snapshot,
    int generation,
  ) async {
    final saved = await _snapshotCache.save(
      snapshot.status,
      savedAt: snapshot.savedAt,
    );
    if (!mounted || generation != _sessionGeneration) return saved;

    setState(() {
      _snapshot = snapshot;
      _lastSnapshotWriteAt = DateTime.now();
      _storageWarning = saved
          ? null
          : 'Nie udało się zapisać lokalnej kopii danych.';
    });
    return saved;
  }

  Future<bool> _persistCurrentSnapshot() async {
    final session = _session;
    if (!session.connected || session.status.isEmpty) {
      return _flushSnapshot(force: true);
    }

    final savedAt = session.lastUpdate ?? DateTime.now();
    _lastObservedLiveUpdate = savedAt;
    _pendingSnapshot = ControllerSnapshot(
      status: session.status,
      savedAt: savedAt,
    );
    _pendingSnapshotGeneration = _sessionGeneration;
    return _flushSnapshot(force: true);
  }

  Future<void> _reloadPreferences() async {
    try {
      final address = await _preferences.loadSavedAddress();
      final autoReconnect = await _preferences.loadAutoReconnect();
      if (!mounted) return;
      setState(() {
        _savedAddress = address;
        _autoReconnect = autoReconnect;
      });
    } on Object {
      if (mounted) {
        _showMessage(
          'Nie udało się odczytać ustawień automatycznego połączenia.',
          isError: true,
        );
      }
    }
  }

  Future<bool> _setAutoReconnect(bool enabled) async {
    try {
      await _preferences.saveAutoReconnect(enabled);
      if (!mounted) return false;
      _initializationToken += 1;
      setState(() {
        _autoReconnect = enabled;
        _initializing = false;
      });
      if (_session.sessionKind == ControllerSessionKind.wifi) {
        _session.setAutomaticReconnect(enabled);
      } else if (enabled && _session.isOfflineMode && _savedAddress != null) {
        try {
          final nextSession = _wifiSessionBuilder(
            _savedAddress!,
            _snapshot?.status,
            _snapshot?.savedAt,
          );
          _installSession(
            nextSession,
            autoReconnect: true,
            initializing: false,
          );
        } on Object {
          _showMessage(
            'Ustawienie zapisano, ale nie udało się uruchomić połączenia '
            'Wi-Fi. Użyj przycisku Wi-Fi, aby spróbować ręcznie.',
            isError: true,
          );
        }
      }
      return true;
    } on Object {
      if (mounted) {
        _showMessage(
          'Nie udało się zapisać ustawienia automatycznego połączenia.',
          isError: true,
        );
      }
      return false;
    }
  }

  Future<void> _openConnectionCenter() async {
    if (_connectionCenterOpen || !mounted) return;
    _connectionCenterOpen = true;
    await _reloadPreferences();
    if (!mounted) {
      _connectionCenterOpen = false;
      return;
    }

    _ConnectionChoice? choice;
    try {
      choice = await showModalBottomSheet<_ConnectionChoice>(
        context: context,
        isScrollControlled: true,
        useSafeArea: true,
        showDragHandle: true,
        builder: (_) => _ConnectionCenterSheet(
          brandName: widget.brandName,
          session: _session,
          snapshot: _snapshot,
          savedAddress: _savedAddress,
          autoReconnect: _autoReconnect,
          initializing: _initializing,
          storageWarning: _storageWarning,
          showDevelopment: widget.showDevelopment,
          showLegacyWebView: widget.showLegacyWebView,
          onAutoReconnectChanged: _setAutoReconnect,
        ),
      );
    } finally {
      _connectionCenterOpen = false;
    }
    if (!mounted || choice == null) return;

    _initializationToken += 1;
    switch (choice) {
      case _ConnectionChoice.wifi:
        await _connectWifi();
      case _ConnectionChoice.bluetooth:
        await _connectBluetooth();
      case _ConnectionChoice.offline:
        await _switchOffline();
      case _ConnectionChoice.development:
        await _switchSession(ControllerSession.development());
      case _ConnectionChoice.legacy:
        await Navigator.of(
          context,
        ).push(MaterialPageRoute<void>(builder: (_) => const ControllerPage()));
    }
  }

  Future<void> _connectWifi() async {
    final nextSession = await Navigator.of(context).push<ControllerSession>(
      MaterialPageRoute<ControllerSession>(
        builder: (_) => const WifiConnectPage(returnSession: true),
      ),
    );
    if (nextSession == null) return;
    await _reloadPreferences();
    await _switchSession(nextSession);
    if (mounted) _showMessage('Połączono ze sterownikiem przez Wi-Fi.');
  }

  Future<void> _connectBluetooth() async {
    final nextSession = await Navigator.of(context).push<ControllerSession>(
      MaterialPageRoute<ControllerSession>(
        builder: (_) => BleScannerPage(
          returnSession: true,
          initialStatus: _session.status,
          cachedAt: _session.lastUpdate,
        ),
      ),
    );
    if (nextSession == null) return;
    await _switchSession(nextSession);
    if (mounted) _showMessage('Wybrano połączenie Bluetooth.');
  }

  Future<void> _switchOffline() async {
    final source = _session;
    final persisted = await _persistCurrentSnapshot();
    if (!mounted) return;

    final localSnapshot = source.connected && source.status.isNotEmpty
        ? ControllerSnapshot(
            status: source.status,
            savedAt: source.lastUpdate ?? DateTime.now(),
          )
        : _snapshot;
    _installSession(
      ControllerSession.offline(
        cachedStatus: localSnapshot?.status,
        cachedAt: localSnapshot?.savedAt,
      ),
      snapshot: localSnapshot,
      initializing: false,
      warning: persisted
          ? null
          : 'Tryb offline działa, ale zapis lokalnej kopii nie powiódł się.',
    );
  }

  Future<void> _switchSession(ControllerSession nextSession) async {
    await _persistCurrentSnapshot();
    if (!mounted) {
      nextSession.dispose();
      return;
    }
    _installSession(nextSession, initializing: false);
  }

  void _showMessage(String message, {bool isError = false}) {
    if (!mounted) return;
    final colors = Theme.of(context).colorScheme;
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(
        SnackBar(
          content: Text(message),
          behavior: SnackBarBehavior.floating,
          backgroundColor: isError ? colors.error : null,
          showCloseIcon: true,
        ),
      );
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    _session.setAppActive(state == AppLifecycleState.resumed);
    if (state != AppLifecycleState.resumed) {
      unawaited(_persistCurrentSnapshot());
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _session.removeListener(_onSessionChanged);
    _snapshotTimer?.cancel();
    _snapshotTimer = null;
    unawaited(_persistCurrentSnapshot());
    _session.dispose();
    if (_ownsRuntimeServices) _runtimeServices?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ControllerShell(
      key: ValueKey(_session),
      session: _session,
      onOpenConnection: () => unawaited(_openConnectionCenter()),
      disposeSession: false,
      runtimeServices: _runtimeServices,
    );
  }
}

enum _ConnectionChoice { wifi, bluetooth, offline, development, legacy }

class _ConnectionCenterSheet extends StatefulWidget {
  const _ConnectionCenterSheet({
    required this.brandName,
    required this.session,
    required this.snapshot,
    required this.savedAddress,
    required this.autoReconnect,
    required this.initializing,
    required this.storageWarning,
    required this.showDevelopment,
    required this.showLegacyWebView,
    required this.onAutoReconnectChanged,
  });

  final String brandName;
  final ControllerSession session;
  final ControllerSnapshot? snapshot;
  final Uri? savedAddress;
  final bool autoReconnect;
  final bool initializing;
  final String? storageWarning;
  final bool showDevelopment;
  final bool showLegacyWebView;
  final Future<bool> Function(bool enabled) onAutoReconnectChanged;

  @override
  State<_ConnectionCenterSheet> createState() => _ConnectionCenterSheetState();
}

class _ConnectionCenterSheetState extends State<_ConnectionCenterSheet> {
  late bool _autoReconnect;
  bool _savingAutoReconnect = false;

  @override
  void initState() {
    super.initState();
    _autoReconnect = widget.savedAddress != null && widget.autoReconnect;
  }

  Future<void> _toggleAutoReconnect(bool value) async {
    if (_savingAutoReconnect) return;
    setState(() => _savingAutoReconnect = true);
    final saved = await widget.onAutoReconnectChanged(value);
    if (!mounted) return;
    setState(() {
      if (saved) _autoReconnect = value;
      _savingAutoReconnect = false;
    });
  }

  void _choose(_ConnectionChoice choice) {
    Navigator.of(context).pop(choice);
  }

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final session = widget.session;
    final online = session.connected;
    final statusLabel = online
        ? 'Połączono: ${_transportLabel(session.sessionKind)}'
        : session.connectionPhase == ControllerConnectionPhase.connecting ||
              session.connectionPhase == ControllerConnectionPhase.reconnecting
        ? 'Łączenie w tle'
        : 'Tryb offline';

    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 620),
        child: SingleChildScrollView(
          padding: EdgeInsets.fromLTRB(
            AquaSpacing.md,
            0,
            AquaSpacing.md,
            AquaSpacing.lg + MediaQuery.viewInsetsOf(context).bottom,
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _ConnectionSheetHeader(
                brandName: widget.brandName,
                statusLabel: statusLabel,
                online: online,
              ),
              if (widget.initializing) ...[
                const SizedBox(height: AquaSpacing.sm),
                const LinearProgressIndicator(
                  semanticsLabel: 'Wczytywanie zapisanych połączeń',
                ),
              ],
              const SizedBox(height: AquaSpacing.md),
              _ConnectionOption(
                icon: Icons.wifi_rounded,
                title: 'Wi-Fi',
                subtitle: widget.savedAddress == null
                    ? 'Połącz przez adres lokalny lub IP sterownika'
                    : 'Zapamiętany sterownik: ${widget.savedAddress!.host}',
                selected:
                    session.sessionKind == ControllerSessionKind.wifi &&
                    session.connected,
                onTap: () => _choose(_ConnectionChoice.wifi),
              ),
              const SizedBox(height: AquaSpacing.xs),
              _ConnectionOption(
                icon: Icons.bluetooth_rounded,
                title: 'Bluetooth',
                subtitle:
                    'Bezpośrednie połączenie lokalne bez dostępu do routera',
                selected:
                    session.sessionKind == ControllerSessionKind.bluetooth,
                onTap: () => _choose(_ConnectionChoice.bluetooth),
              ),
              const SizedBox(height: AquaSpacing.xs),
              _ConnectionOption(
                icon: Icons.history_rounded,
                title: 'Pozostań offline',
                subtitle: widget.snapshot == null
                    ? 'Pełny interfejs bez zapisanych pomiarów'
                    : 'Ostatni zapis: ${_ageLabel(widget.snapshot!.savedAt)}',
                selected: session.isOfflineMode,
                onTap: session.isOfflineMode
                    ? null
                    : () => _choose(_ConnectionChoice.offline),
              ),
              const SizedBox(height: AquaSpacing.sm),
              Card(
                margin: EdgeInsets.zero,
                color: colors.surfaceContainerLow,
                child: SwitchListTile.adaptive(
                  secondary: const Icon(Icons.sync_lock_rounded),
                  title: const Text(
                    'Łącz automatycznie przez Wi-Fi',
                    style: TextStyle(fontWeight: FontWeight.w700),
                  ),
                  subtitle: Text(
                    widget.savedAddress == null
                        ? 'Najpierw zapisz sterownik przez połączenie Wi-Fi.'
                        : 'Po włączeniu sieci aplikacja sama ponowi synchronizację.',
                  ),
                  value: _autoReconnect,
                  onChanged: widget.savedAddress == null || _savingAutoReconnect
                      ? null
                      : _toggleAutoReconnect,
                ),
              ),
              if (widget.storageWarning case final warning?) ...[
                const SizedBox(height: AquaSpacing.sm),
                Text(
                  warning,
                  style: TextStyle(color: colors.error),
                  textAlign: TextAlign.center,
                ),
              ],
              if (widget.showDevelopment || widget.showLegacyWebView) ...[
                const SizedBox(height: AquaSpacing.md),
                const Divider(),
                Text(
                  'Narzędzia serwisowe',
                  style: Theme.of(
                    context,
                  ).textTheme.titleSmall?.copyWith(fontWeight: FontWeight.w800),
                ),
                if (widget.showDevelopment)
                  ListTile(
                    leading: const Icon(Icons.science_rounded),
                    title: const Text('Symulator DEV'),
                    onTap: () => _choose(_ConnectionChoice.development),
                  ),
                if (widget.showLegacyWebView)
                  ListTile(
                    leading: const Icon(Icons.language_rounded),
                    title: const Text('Panel WWW zgodności'),
                    onTap: () => _choose(_ConnectionChoice.legacy),
                  ),
              ],
            ],
          ),
        ),
      ),
    );
  }

  static String _transportLabel(ControllerSessionKind kind) => switch (kind) {
    ControllerSessionKind.wifi => 'Wi-Fi',
    ControllerSessionKind.bluetooth => 'Bluetooth',
    ControllerSessionKind.offline => 'offline',
    ControllerSessionKind.development => 'DEV',
  };

  static String _ageLabel(DateTime savedAt) {
    final age = DateTime.now().difference(savedAt.toLocal());
    if (age.isNegative || age.inSeconds < 5) return 'przed chwilą';
    if (age.inMinutes < 1) return '${age.inSeconds} s temu';
    if (age.inHours < 1) return '${age.inMinutes} min temu';
    if (age.inDays < 1) return '${age.inHours} h temu';
    return '${age.inDays} d temu';
  }
}

class _ConnectionSheetHeader extends StatelessWidget {
  const _ConnectionSheetHeader({
    required this.brandName,
    required this.statusLabel,
    required this.online,
  });

  final String brandName;
  final String statusLabel;
  final bool online;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final statusColors = context.statusColors;
    final identity = Row(
      children: [
        Container(
          width: 46,
          height: 46,
          decoration: BoxDecoration(
            color: colors.primaryContainer,
            borderRadius: BorderRadius.circular(AquaRadius.control),
          ),
          child: Icon(Icons.hub_rounded, color: colors.onPrimaryContainer),
        ),
        const SizedBox(width: AquaSpacing.sm),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Połączenia',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: Theme.of(
                  context,
                ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w900),
              ),
              Text(
                brandName,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(color: colors.onSurfaceVariant),
              ),
            ],
          ),
        ),
      ],
    );
    final status = Chip(
      avatar: Icon(
        online ? Icons.check_circle_rounded : Icons.history_rounded,
        size: 18,
        color: online ? statusColors.success : colors.onSurfaceVariant,
      ),
      label: Text(statusLabel),
    );

    return LayoutBuilder(
      builder: (context, constraints) {
        if (constraints.maxWidth < 460 ||
            MediaQuery.textScalerOf(context).scale(1) > 1.3) {
          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              identity,
              const SizedBox(height: AquaSpacing.xs),
              Align(alignment: Alignment.centerLeft, child: status),
            ],
          );
        }
        return Row(
          children: [
            Expanded(child: identity),
            const SizedBox(width: AquaSpacing.sm),
            status,
          ],
        );
      },
    );
  }
}

class _ConnectionOption extends StatelessWidget {
  const _ConnectionOption({
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.selected,
    required this.onTap,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final bool selected;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Card(
      margin: EdgeInsets.zero,
      clipBehavior: Clip.antiAlias,
      color: selected ? colors.primaryContainer : null,
      child: ListTile(
        contentPadding: const EdgeInsets.symmetric(
          horizontal: AquaSpacing.md,
          vertical: AquaSpacing.xs,
        ),
        leading: Icon(
          icon,
          color: selected ? colors.onPrimaryContainer : colors.primary,
        ),
        title: Text(title, style: const TextStyle(fontWeight: FontWeight.w800)),
        subtitle: Text(subtitle),
        trailing: selected
            ? Icon(Icons.check_circle_rounded, color: colors.onPrimaryContainer)
            : const Icon(Icons.chevron_right_rounded),
        onTap: onTap,
      ),
    );
  }
}
