import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';

import 'connectivity/ble_controller_transport.dart';
import 'full_controller/ble_remote_api.dart';
import 'full_controller/controller_session.dart';
import 'full_controller/controller_shell.dart';
import 'full_controller/widgets.dart';

typedef BlePermissionRequester = Future<void> Function();
typedef BleScanStarter = Stream<DiscoveredDevice> Function();
typedef BleDevicePageBuilder = Widget Function(DiscoveredDevice device);

class BleScannerPage extends StatefulWidget {
  const BleScannerPage({
    super.key,
    this.permissionRequester,
    this.scanStarter,
    this.devicePageBuilder,
    this.returnSession = false,
    this.initialStatus,
    this.cachedAt,
  });

  final BlePermissionRequester? permissionRequester;
  final BleScanStarter? scanStarter;
  final BleDevicePageBuilder? devicePageBuilder;
  final bool returnSession;
  final Map<String, dynamic>? initialStatus;
  final DateTime? cachedAt;

  @override
  State<BleScannerPage> createState() => _BleScannerPageState();
}

class _BleScannerPageState extends State<BleScannerPage> {
  final Map<String, DiscoveredDevice> _devices = {};
  final Map<String, DiscoveredDevice> _pendingDevices = {};
  StreamSubscription<DiscoveredDevice>? _scanSubscription;
  Timer? _scanTimeout;
  Timer? _scanRenderTimer;
  bool _scanning = false;
  bool _scanStarting = false;
  String? _openingDeviceId;
  String? _error;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) {
        unawaited(_startScan());
      }
    });
  }

  Future<void> _startScan() async {
    if (_scanStarting || _scanning || _openingDeviceId != null) {
      return;
    }
    _scanStarting = true;
    try {
      await _stopScan(flushPending: false);
      if (!mounted) {
        return;
      }
      setState(() {
        _devices.clear();
        _pendingDevices.clear();
        _error = null;
        _scanning = true;
      });

      final requestPermissions =
          widget.permissionRequester ??
          BleControllerEnvironment.instance.ensurePermissions;
      final startScan =
          widget.scanStarter ??
          BleControllerEnvironment.instance.scanForControllers;
      await requestPermissions();
      if (!mounted) {
        return;
      }
      _scanSubscription = startScan().listen(
        (device) {
          if (!mounted) {
            return;
          }
          _pendingDevices[device.id] = device;
          _scheduleDeviceRender();
        },
        onError: (Object error) {
          if (!mounted) {
            return;
          }
          _cancelPendingRender(clearPending: true);
          _scanTimeout?.cancel();
          _scanTimeout = null;
          setState(() {
            _error = 'Skanowanie BLE nie powiodło się: $error';
            _scanning = false;
          });
        },
      );
      _scanTimeout = Timer(
        const Duration(seconds: 12),
        () => unawaited(_stopScan()),
      );
    } catch (error) {
      if (mounted) {
        _cancelPendingRender(clearPending: true);
        setState(() {
          _error = error.toString();
          _scanning = false;
        });
      }
    } finally {
      if (mounted) {
        setState(() => _scanStarting = false);
      } else {
        _scanStarting = false;
      }
    }
  }

  void _scheduleDeviceRender() {
    if (_scanRenderTimer != null) {
      return;
    }
    _scanRenderTimer = Timer(
      const Duration(milliseconds: 300),
      _flushPendingDevices,
    );
  }

  void _flushPendingDevices() {
    _scanRenderTimer = null;
    if (!mounted || _pendingDevices.isEmpty) {
      return;
    }
    final updates = Map<String, DiscoveredDevice>.of(_pendingDevices);
    _pendingDevices.clear();
    setState(() => _devices.addAll(updates));
  }

  void _cancelPendingRender({required bool clearPending}) {
    _scanRenderTimer?.cancel();
    _scanRenderTimer = null;
    if (clearPending) {
      _pendingDevices.clear();
    }
  }

  Future<void> _stopScan({bool flushPending = true}) async {
    _scanTimeout?.cancel();
    _scanTimeout = null;
    final subscription = _scanSubscription;
    _scanSubscription = null;
    if (subscription != null) {
      try {
        await subscription.cancel();
      } on Object {
        // Zatrzymanie skanowania pozostaje idempotentne po błędzie platformy.
      }
    }
    _scanRenderTimer?.cancel();
    _scanRenderTimer = null;
    if (flushPending) {
      _flushPendingDevices();
    } else {
      _pendingDevices.clear();
    }
    if (mounted && _scanning) {
      setState(() => _scanning = false);
    }
  }

  Future<void> _openDevice(DiscoveredDevice device) async {
    if (_openingDeviceId != null || !mounted) {
      return;
    }
    setState(() => _openingDeviceId = device.id);
    try {
      await _stopScan();
      if (!mounted) {
        return;
      }
      final name = device.name.trim().isEmpty
          ? 'AquaCYD BLE'
          : device.name.trim();
      final pageBuilder = widget.devicePageBuilder;
      if (pageBuilder != null) {
        await Navigator.of(
          context,
        ).push(MaterialPageRoute<void>(builder: (_) => pageBuilder(device)));
        return;
      }

      final transport = BleControllerTransport(
        deviceId: device.id,
        deviceName: name,
      );
      final session = ControllerSession.bluetooth(
        BleRemoteApi(transport),
        initialStatus: widget.initialStatus,
        cachedAt: widget.cachedAt,
      );
      if (widget.returnSession) {
        Navigator.of(context).pop<ControllerSession>(session);
        return;
      }
      await Navigator.of(context).push(
        MaterialPageRoute<void>(
          builder: (_) => ControllerShell(session: session),
        ),
      );
    } catch (error) {
      if (mounted) {
        setState(() {
          _error = 'Nie udało się otworzyć sterownika BLE: $error';
        });
      }
    } finally {
      if (mounted) {
        setState(() => _openingDeviceId = null);
      }
    }
  }

  @override
  void dispose() {
    _scanTimeout?.cancel();
    _scanRenderTimer?.cancel();
    _pendingDevices.clear();
    final subscription = _scanSubscription;
    _scanSubscription = null;
    if (subscription != null) {
      unawaited(subscription.cancel().catchError((Object _) {}));
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final devices = _devices.values.toList()
      ..sort((left, right) => right.rssi.compareTo(left.rssi));
    return Scaffold(
      appBar: AppBar(
        title: const Text('Sterowniki BLE'),
        actions: [
          IconButton(
            onPressed: _scanning || _scanStarting || _openingDeviceId != null
                ? null
                : _startScan,
            icon: const Icon(Icons.refresh),
            tooltip: 'Skanuj ponownie',
          ),
        ],
      ),
      body: Column(
        children: [
          if (_scanning)
            const LinearProgressIndicator(
              minHeight: 3,
              semanticsLabel: 'Skanowanie urządzeń Bluetooth',
            ),
          Expanded(
            child: _error != null
                ? StatePanel.error(
                    title: 'Skanowanie nie powiodło się',
                    message: _error!,
                    icon: Icons.bluetooth_disabled_rounded,
                    actionLabel: 'Spróbuj ponownie',
                    onAction: _startScan,
                  )
                : devices.isEmpty
                ? _scanning
                      ? const StatePanel.loading(
                          title: 'Szukanie sterownika',
                          message:
                              'Trzymaj telefon blisko włączonego sterownika AquaCYD.',
                        )
                      : StatePanel.empty(
                          title: 'Nie znaleziono sterownika',
                          message:
                              'Sprawdź, czy firmware BLE jest aktywne, a Bluetooth w telefonie włączony.',
                          icon: Icons.bluetooth_disabled_rounded,
                          actionLabel: 'Skanuj ponownie',
                          onAction: _startScan,
                        )
                : SafeArea(
                    top: false,
                    child: Center(
                      child: ConstrainedBox(
                        constraints: const BoxConstraints(maxWidth: 720),
                        child: ListView.separated(
                          keyboardDismissBehavior:
                              ScrollViewKeyboardDismissBehavior.onDrag,
                          padding: const EdgeInsets.fromLTRB(12, 12, 12, 24),
                          itemCount: devices.length,
                          separatorBuilder: (_, _) => const SizedBox(height: 8),
                          itemBuilder: (context, index) {
                            final device = devices[index];
                            final name = device.name.trim().isEmpty
                                ? 'AquaCYD BLE'
                                : device.name.trim();
                            return Card(
                              clipBehavior: Clip.antiAlias,
                              child: ListTile(
                                contentPadding: const EdgeInsets.symmetric(
                                  horizontal: 16,
                                  vertical: 6,
                                ),
                                leading: ExcludeSemantics(
                                  child: Container(
                                    width: 48,
                                    height: 48,
                                    decoration: BoxDecoration(
                                      color: Theme.of(
                                        context,
                                      ).colorScheme.primaryContainer,
                                      borderRadius: BorderRadius.circular(5),
                                    ),
                                    child: Icon(
                                      Icons.bluetooth_rounded,
                                      color: Theme.of(
                                        context,
                                      ).colorScheme.primary,
                                    ),
                                  ),
                                ),
                                title: Text(
                                  name,
                                  maxLines: 1,
                                  overflow: TextOverflow.ellipsis,
                                  style: const TextStyle(
                                    fontWeight: FontWeight.w700,
                                  ),
                                ),
                                subtitle: Text(
                                  '${_signalLabel(device.rssi)} · ${device.rssi} dBm\n${device.id}',
                                  maxLines: 2,
                                  overflow: TextOverflow.ellipsis,
                                ),
                                isThreeLine: true,
                                trailing: _openingDeviceId == device.id
                                    ? const SizedBox.square(
                                        dimension: 20,
                                        child: CircularProgressIndicator(
                                          strokeWidth: 2,
                                        ),
                                      )
                                    : const Icon(Icons.chevron_right_rounded),
                                onTap: _openingDeviceId == null
                                    ? () => _openDevice(device)
                                    : null,
                              ),
                            );
                          },
                        ),
                      ),
                    ),
                  ),
          ),
        ],
      ),
    );
  }

  String _signalLabel(int rssi) {
    if (rssi >= -60) return 'Silny sygnał';
    if (rssi >= -75) return 'Średni sygnał';
    return 'Słaby sygnał';
  }
}
