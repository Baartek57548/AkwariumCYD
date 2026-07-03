import 'package:flutter/material.dart';

import '../controller_api.dart';
import '../controller_session.dart';
import '../data_access.dart';
import '../widgets.dart';

class DiagnosticsView extends StatefulWidget {
  const DiagnosticsView({
    super.key,
    required this.session,
    required this.ensureAdmin,
  });

  final ControllerSession session;
  final Future<bool> Function() ensureAdmin;

  @override
  State<DiagnosticsView> createState() => _DiagnosticsViewState();
}

class _DiagnosticsViewState extends State<DiagnosticsView> {
  bool scanning = false;
  String? message;

  Future<void> _scan() async {
    if (!await widget.ensureAdmin()) return;
    setState(() {
      scanning = true;
      message = 'Skanowanie I²C i OneWire…';
    });
    try {
      await widget.session.scanBuses();
      if (mounted) setState(() => message = 'Skan magistral zakończony.');
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => message = error.message);
    } finally {
      if (mounted) setState(() => scanning = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final status = widget.session.status;
    final sensors = status.section('sensors');
    final system = status.section('system');
    final diagnostics = widget.session.diagnostics;
    final devices = diagnostics.list('devices');
    final oneWire = diagnostics.section('oneWire');
    final uart = diagnostics.section('uart');
    return ControllerPageBody(
      children: [
        SectionHeader(
          title: 'Diagnostyka sprzętu',
          description:
              'Stan czujników, pamięci i aktywne skanowanie magistral sterownika.',
          trailing: FilledButton.icon(
            onPressed: scanning ? null : _scan,
            icon: scanning
                ? const SizedBox.square(
                    dimension: 18,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : const Icon(Icons.radar_rounded),
            label: const Text('Skanuj magistrale'),
          ),
        ),
        ResponsiveGrid(
          children: [
            MetricTile(
              icon: Icons.memory_rounded,
              label: 'Wolny heap',
              value: formatBytes(
                system.integer('freeHeap', status.integer('heap_free')),
              ),
              detail:
                  'Największy blok ${formatBytes(system.integer('largestHeap', status.integer('heap_largest')))}',
            ),
            MetricTile(
              icon: Icons.sd_storage_rounded,
              label: 'Karta SD',
              value: status.flag('sd_mounted') ? 'ONLINE' : 'BRAK',
              detail: '${formatBytes(status.integer('sd_free_bytes'))} wolne',
            ),
            MetricTile(
              icon: Icons.hub_rounded,
              label: 'MCP23017',
              value: sensors.flag('mcp_ok') ? 'ONLINE' : 'BRAK',
              detail: sensors.flag('mcp_valid')
                  ? 'Odczyty wiarygodne'
                  : 'Brak aktualnego odczytu',
            ),
            MetricTile(
              icon: Icons.timer_outlined,
              label: 'Czas pracy',
              value: formatUptime(system.integer('uptime')),
              detail: 'Reset reason ${system.text('resetReason', '—')}',
            ),
          ],
        ),
        const SectionHeader(title: 'Czujniki'),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                _SensorRow(
                  label: 'Temperatura',
                  valid: sensors.flag('temp_valid'),
                  value: '${sensors.number('temp_c').toStringAsFixed(2)} °C',
                ),
                _SensorRow(
                  label: 'pH',
                  valid: sensors.flag('ph_valid'),
                  value: sensors.number('ph').toStringAsFixed(3),
                ),
                _SensorRow(
                  label: 'EC',
                  valid: sensors.flag('ec_valid'),
                  value: '${sensors.number('ec').toStringAsFixed(1)} µS/cm',
                ),
                _SensorRow(
                  label: 'LDR',
                  valid: sensors.flag('ldr_valid'),
                  value: '${sensors.integer('ldr')}',
                ),
                _SensorRow(
                  label: 'Poziom wody',
                  valid: sensors.flag('water_level_valid'),
                  value: sensors.flag('water_level_high') ? 'HIGH / OK' : 'LOW',
                ),
                _SensorRow(
                  label: 'Wyciek',
                  valid: sensors.flag('leak_valid'),
                  value: sensors.flag('leak_detected') ? 'WYKRYTO' : 'BRAK',
                ),
                _SensorRow(
                  label: 'Przepływ',
                  valid: sensors.flag('flow_valid'),
                  value: sensors.flag('flow_active') ? 'AKTYWNY' : 'BRAK',
                ),
              ],
            ),
          ),
        ),
        if (diagnostics.isNotEmpty) ...[
          const SectionHeader(title: 'I²C'),
          Card(
            child: Column(
              children: [
                ListTile(
                  leading: const Icon(Icons.settings_input_component_rounded),
                  title: Text(
                    'SDA GPIO ${diagnostics.integer('sda')} · SCL GPIO ${diagnostics.integer('scl')}',
                  ),
                  subtitle: Text(
                    '${diagnostics.integer('frequencyHz')} Hz · skan ${diagnostics.integer('scanMs')} ms',
                  ),
                  trailing: Chip(label: Text('${devices.length} urządzeń')),
                ),
                const Divider(height: 1),
                for (final item in devices)
                  Builder(
                    builder: (context) {
                      final device = jsonMap(item);
                      return ListTile(
                        leading: CircleAvatar(
                          child: Text(device.text('hex', '?')),
                        ),
                        title: Text(_deviceType(device.text('type'))),
                        subtitle: Text(
                          device.flag('configured')
                              ? 'Skonfigurowane w firmware'
                              : 'Wykryte, nieprzypisane',
                        ),
                        trailing: Icon(
                          device.flag('configured')
                              ? Icons.check_circle_rounded
                              : Icons.help_outline_rounded,
                        ),
                      );
                    },
                  ),
              ],
            ),
          ),
          const SectionHeader(title: 'UART'),
          Card(
            child: Column(
              children: [
                for (final item in uart.list('ports'))
                  Builder(
                    builder: (context) {
                      final port = jsonMap(item);
                      return ListTile(
                        leading: const Icon(Icons.terminal_rounded),
                        title: Text(
                          'UART${port.integer('port')} · ${port.text('role', 'port')}',
                        ),
                        subtitle: Text(
                          'TX GPIO ${port.integer('tx')} · RX GPIO ${port.integer('rx')}',
                        ),
                        trailing: Text(
                          '${port.integer('baud')} ${port.text('format')}',
                        ),
                      );
                    },
                  ),
              ],
            ),
          ),
          const SectionHeader(title: 'OneWire'),
          Card(
            child: Column(
              children: [
                ListTile(
                  leading: const Icon(Icons.device_thermostat_rounded),
                  title: Text('DATA GPIO ${oneWire.integer('dataPin')}'),
                  subtitle: Text('Skan ${oneWire.integer('scanMs')} ms'),
                  trailing: Chip(
                    label: Text('${oneWire.list('devices').length} urządzeń'),
                  ),
                ),
                const Divider(height: 1),
                for (final item in oneWire.list('devices'))
                  Builder(
                    builder: (context) {
                      final device = jsonMap(item);
                      return ListTile(
                        leading: Icon(
                          device.flag('crcValid')
                              ? Icons.check_circle_rounded
                              : Icons.error_rounded,
                        ),
                        title: Text(
                          device.text('type', 'unknown').toUpperCase(),
                        ),
                        subtitle: Text(
                          'ROM ${device.text('rom')} · rodzina 0x${device.integer('family').toRadixString(16).padLeft(2, '0')}',
                        ),
                        trailing: Text(
                          device.flag('crcValid') ? 'CRC OK' : 'CRC ERROR',
                        ),
                      );
                    },
                  ),
              ],
            ),
          ),
        ],
        if (message != null)
          Padding(
            padding: const EdgeInsets.only(top: 10),
            child: Text(message!),
          ),
      ],
    );
  }

  String _deviceType(String type) => switch (type) {
    'mcp23017' => 'MCP23017 — ekspander GPIO',
    'ads1115' => 'ADS1115 — przetwornik ADC',
    'oled' => 'Wyświetlacz OLED',
    'rtc_or_imu' => 'RTC / IMU',
    _ => type.isEmpty ? 'Nieznane urządzenie' : type,
  };
}

class _SensorRow extends StatelessWidget {
  const _SensorRow({
    required this.label,
    required this.valid,
    required this.value,
  });

  final String label;
  final bool valid;
  final String value;

  @override
  Widget build(BuildContext context) {
    return ListTile(
      contentPadding: EdgeInsets.zero,
      leading: Icon(
        valid ? Icons.check_circle_rounded : Icons.cancel_outlined,
        color: valid
            ? Theme.of(context).colorScheme.primary
            : Theme.of(context).colorScheme.error,
      ),
      title: Text(label),
      trailing: Text(
        valid ? value : 'BRAK DANYCH',
        style: const TextStyle(fontWeight: FontWeight.w700),
      ),
    );
  }
}
