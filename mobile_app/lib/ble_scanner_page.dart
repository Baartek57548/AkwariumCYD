import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';

import 'connectivity/ble_controller_transport.dart';
import 'native_dashboard_page.dart';

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
        ? 'cydAkwarium BLE'
        : device.name.trim();
    await Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => NativeDashboardPage(
          transport: BleControllerTransport(
            deviceId: device.id,
            deviceName: name,
          ),
        ),
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
          if (_scanning) const LinearProgressIndicator(),
          if (_error != null)
            MaterialBanner(
              content: Text(_error!),
              leading: const Icon(Icons.bluetooth_disabled_rounded),
              actions: [
                TextButton(onPressed: _startScan, child: const Text('PONÓW')),
              ],
            ),
          Expanded(
            child: devices.isEmpty
                ? _EmptyScannerState(scanning: _scanning, onRetry: _startScan)
                : ListView.separated(
                    padding: const EdgeInsets.all(12),
                    itemCount: devices.length,
                    separatorBuilder: (_, _) => const SizedBox(height: 8),
                    itemBuilder: (context, index) {
                      final device = devices[index];
                      final name = device.name.trim().isEmpty
                          ? 'cydAkwarium BLE'
                          : device.name.trim();
                      return Card(
                        child: ListTile(
                          leading: const CircleAvatar(
                            child: Icon(Icons.bluetooth_rounded),
                          ),
                          title: Text(name),
                          subtitle: Text(
                            '${device.id}\nSygnał: ${device.rssi} dBm',
                          ),
                          isThreeLine: true,
                          trailing: const Icon(Icons.chevron_right_rounded),
                          onTap: () => _openDevice(device),
                        ),
                      );
                    },
                  ),
          ),
        ],
      ),
    );
  }
}

class _EmptyScannerState extends StatelessWidget {
  const _EmptyScannerState({required this.scanning, required this.onRetry});

  final bool scanning;
  final VoidCallback onRetry;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(28),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              scanning ? Icons.bluetooth_searching : Icons.bluetooth_disabled,
              size: 70,
              color: Theme.of(context).colorScheme.primary,
            ),
            const SizedBox(height: 18),
            Text(
              scanning ? 'Szukanie sterownika…' : 'Nie znaleziono sterownika',
              style: Theme.of(context).textTheme.titleLarge,
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 8),
            const Text(
              'Włącz sterownik z firmware BLE i trzymaj telefon w jego pobliżu.',
              textAlign: TextAlign.center,
            ),
            if (!scanning) ...[
              const SizedBox(height: 20),
              FilledButton.icon(
                onPressed: onRetry,
                icon: const Icon(Icons.refresh),
                label: const Text('Skanuj ponownie'),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
