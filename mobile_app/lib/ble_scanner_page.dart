import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';

import 'connectivity/ble_controller_transport.dart';
import 'full_controller/ble_remote_api.dart';
import 'full_controller/controller_session.dart';
import 'full_controller/controller_shell.dart';
import 'full_controller/widgets.dart';

class BleScannerPage extends StatefulWidget {
  const BleScannerPage({super.key});

  @override
  State<BleScannerPage> createState() => _BleScannerPageState();
}

class _BleScannerPageState extends State<BleScannerPage> {
  final Map<String, DiscoveredDevice> _devices = {};
  StreamSubscription<DiscoveredDevice>? _scanSubscription;
  Timer? _scanTimeout;
  bool _scanning = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _startScan());
  }

  Future<void> _startScan() async {
    await _stopScan();
    if (!mounted) {
      return;
    }
    setState(() {
      _devices.clear();
      _error = null;
      _scanning = true;
    });
    try {
      await BleControllerEnvironment.instance.ensurePermissions();
      _scanSubscription = BleControllerEnvironment.instance
          .scanForControllers()
          .listen(
            (device) {
              if (!mounted) {
                return;
              }
              setState(() => _devices[device.id] = device);
            },
            onError: (Object error) {
              if (mounted) {
                setState(() {
                  _error = 'Skanowanie BLE nie powiodło się: $error';
                  _scanning = false;
                });
              }
            },
          );
      _scanTimeout = Timer(const Duration(seconds: 12), _stopScan);
    } catch (error) {
      if (mounted) {
        setState(() {
          _error = error.toString();
          _scanning = false;
        });
      }
    }
  }

  Future<void> _stopScan() async {
    _scanTimeout?.cancel();
    _scanTimeout = null;
    await _scanSubscription?.cancel();
    _scanSubscription = null;
    if (mounted && _scanning) {
      setState(() => _scanning = false);
    }
  }

  Future<void> _openDevice(DiscoveredDevice device) async {
    await _stopScan();
    if (!mounted) {
      return;
    }
    final name = device.name.trim().isEmpty
        ? 'AquaCYD BLE'
        : device.name.trim();
    await Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) {
          final transport = BleControllerTransport(
            deviceId: device.id,
            deviceName: name,
          );
          return ControllerShell(
            session: ControllerSession.bluetooth(BleRemoteApi(transport)),
          );
        },
      ),
    );
  }

  @override
  void dispose() {
    _scanTimeout?.cancel();
    _scanSubscription?.cancel();
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
            onPressed: _scanning ? null : _startScan,
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
                                trailing: const Icon(
                                  Icons.chevron_right_rounded,
                                ),
                                onTap: () => _openDevice(device),
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
