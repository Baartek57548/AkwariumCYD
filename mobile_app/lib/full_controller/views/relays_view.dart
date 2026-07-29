import 'dart:convert';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../controller_api.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../data_access.dart';
import '../widgets.dart';

class RelaysView extends StatefulWidget {
  const RelaysView({
    super.key,
    required this.session,
    required this.runAction,
    required this.ensureAdmin,
  });

  final ControllerSession session;
  final RunControllerAction runAction;
  final Future<bool> Function() ensureAdmin;

  @override
  State<RelaysView> createState() => _RelaysViewState();
}

class _RelaysViewState extends State<RelaysView> {
  late List<_RelayDefinition> relays;
  bool activeLow = true;
  int startDelay = 500;
  String storage = 'sd';
  bool saving = false;
  int? testingChannel;
  String? message;

  static const functions = <String, String>{
    'none': 'Brak / nieużywany',
    'main_light': 'Świetlówka przednia',
    'plant_light': 'Świetlówka tylna',
    'filter': 'Filtr',
    'aeration': 'Napowietrzanie',
    'heater': 'Grzałka',
    'co2': 'Elektrozawór CO₂',
    'water_dosing': 'Dolewka ATO',
    'feeder': 'Karmnik',
    'circulation_pump': 'Pompa obiegowa',
    'uv_lamp': 'Lampa UV',
    'reserve': 'Rezerwa',
    'custom': 'Własna funkcja',
  };

  @override
  void initState() {
    super.initState();
    relays = _firmwareDefaults();
    storage = widget.session.status.flag('sd_mounted') ? 'sd' : 'internal';
  }

  List<String> get warnings {
    final result = <String>[];
    for (final function in const ['filter', 'heater', 'co2', 'water_dosing']) {
      if (relays.where((relay) => relay.function == function).length > 1) {
        result.add(
          'Funkcja ${functions[function]} jest przypisana do kilku kanałów.',
        );
      }
    }
    for (final relay in relays) {
      if (const {'heater', 'co2', 'water_dosing'}.contains(relay.function) &&
          relay.safeState != 'off') {
        result.add(
          'CH${relay.channel} ${relay.label}: bezpieczny stan powinien być OFF.',
        );
      }
    }
    return result;
  }

  Future<void> _testRelay(int channel) async {
    setState(() => testingChannel = channel);
    try {
      await widget.runAction(
        'test_relay',
        payload: {'channel': channel, 'state': true, 'duration': 3},
        refreshAfter: false,
        confirmation:
            'Kanał CH$channel zostanie fizycznie włączony na maksymalnie 3 sekundy. Kontynuować?',
      );
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => message = error.message);
    } finally {
      if (mounted) setState(() => testingChannel = null);
    }
  }

  Future<void> _save() async {
    if (warnings.isNotEmpty) {
      final proceed = await showDialog<bool>(
        context: context,
        builder: (context) => AlertDialog(
          title: const Text('Mapa zawiera ostrzeżenia'),
          content: Text('${warnings.join('\n')}\n\nZapisać mimo ostrzeżeń?'),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context, false),
              child: const Text('Anuluj'),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(context, true),
              child: const Text('Zapisz'),
            ),
          ],
        ),
      );
      if (proceed != true) return;
    }
    setState(() => saving = true);
    try {
      final result = await widget.runAction(
        'save_relays',
        payload: {'data': jsonEncode(_profileJson())},
      );
      if (mounted) setState(() => message = result.message);
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => message = error.message);
    } finally {
      if (mounted) setState(() => saving = false);
    }
  }

  Future<void> _export() async {
    final bytes = utf8.encode(
      const JsonEncoder.withIndent('  ').convert(_profileJson()),
    );
    final path = await FilePicker.saveFile(
      dialogTitle: 'Eksportuj mapę przekaźników',
      fileName: 'relays.json',
      bytes: bytes,
    );
    if (mounted) {
      setState(
        () => message = path == null
            ? 'Eksport anulowany.'
            : 'Mapa została wyeksportowana.',
      );
    }
  }

  Future<void> _import() async {
    try {
      final selected = await FilePicker.pickFiles(
        type: FileType.custom,
        allowedExtensions: const ['json'],
        withData: true,
      );
      final bytes = selected?.files.singleOrNull?.bytes;
      if (bytes == null) return;
      if (bytes.length > 4096) {
        throw const FormatException('Profil przekracza limit 4096 B firmware.');
      }
      final decoded = jsonDecode(utf8.decode(bytes));
      if (decoded is! Map<String, dynamic>) {
        throw const FormatException('Plik nie jest obiektem JSON.');
      }
      final board = jsonMap(decoded['relayBoard']);
      final rawRelays = jsonList(decoded['relays']);
      if (rawRelays.length != 8) {
        throw const FormatException(
          'Profil musi zawierać dokładnie 8 kanałów.',
        );
      }
      final imported = rawRelays
          .map((item) => _RelayDefinition.fromJson(jsonMap(item)))
          .toList();
      final channels = imported.map((item) => item.channel).toSet();
      if (channels.length != 8 ||
          !List.generate(8, (index) => index + 1).every(channels.contains)) {
        throw const FormatException(
          'Numery kanałów muszą obejmować CH1–CH8 bez duplikatów.',
        );
      }
      imported.sort((a, b) => a.channel.compareTo(b.channel));
      setState(() {
        relays = imported;
        activeLow = board.flag('activeLow', true);
        startDelay = board.integer('startDelay', 500).clamp(0, 5000);
        storage = board.text('storage', storage);
        message = 'Profil został zaimportowany i oczekuje na zapis.';
      });
    } on Object catch (error) {
      if (mounted) {
        setState(() => message = 'Nie udało się zaimportować profilu: $error');
      }
    }
  }

  Map<String, dynamic> _profileJson() => {
    'relayBoard': {
      'enabled': true,
      'channels': 8,
      'expander': 'MCP23017',
      'i2cAddress': '0x20',
      'activeLow': activeLow,
      'startDelay': startDelay,
      'storage': storage,
    },
    'relays': relays.map((relay) => relay.toJson()).toList(),
  };

  @override
  Widget build(BuildContext context) {
    final status = widget.session.status;
    final runtimeRelays = status.section('relays');
    final modules = status.section('modules');
    return ControllerPageBody(
      children: [
        const SectionHeader(
          title: 'Mapa przekaźników MCP23017',
          description:
              'Pełny odpowiednik kreatora 8CH z walidacją stanów awaryjnych.',
        ),
        ResponsiveGrid(
          children: [
            MetricTile(
              icon: Icons.hub_rounded,
              label: 'Ekspander I²C',
              value: status.section('sensors').flag('mcp_ok')
                  ? 'ONLINE'
                  : 'BRAK',
              detail: 'MCP23017 · 0x20',
            ),
            MetricTile(
              icon: Icons.view_module_rounded,
              label: 'Kanały',
              value: '8',
              detail: 'Kompletna mapa firmware',
            ),
            MetricTile(
              icon: Icons.power_rounded,
              label: 'Aktywne wyjścia',
              value:
                  '${[runtimeRelays.flag('light'), runtimeRelays.flag('plantLight'), runtimeRelays.flag('pump'), runtimeRelays.flag('heater'), runtimeRelays.flag('co2'), runtimeRelays.flag('aeration'), modules.flag('water_dosing_on')].where((value) => value).length} / 8',
              detail: 'Ostatnia telemetria',
            ),
            MetricTile(
              icon: Icons.health_and_safety_rounded,
              label: 'Walidacja',
              value: warnings.isEmpty ? 'GOTOWE' : '${warnings.length} UWAG',
              detail: warnings.isEmpty
                  ? 'Stany awaryjne poprawne'
                  : warnings.first,
              tone: warnings.isEmpty
                  ? null
                  : Theme.of(context).colorScheme.error,
            ),
          ],
        ),
        const SizedBox(height: 12),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                LabeledSwitch(
                  label: 'Aktywny stan niski',
                  subtitle:
                      'Standardowa konfiguracja modułów przekaźnikowych MCP23017.',
                  value: activeLow,
                  onChanged: (value) => setState(() => activeLow = value),
                ),
                Row(
                  children: [
                    const Expanded(child: Text('Opóźnienie startu wyjść')),
                    Text(
                      '$startDelay ms',
                      style: const TextStyle(fontWeight: FontWeight.w700),
                    ),
                  ],
                ),
                Slider(
                  value: startDelay.toDouble(),
                  min: 0,
                  max: 5000,
                  divisions: 50,
                  label: '$startDelay ms',
                  onChanged: (value) =>
                      setState(() => startDelay = value.round()),
                ),
              ],
            ),
          ),
        ),
        const SectionHeader(title: 'Kanały CH1–CH8'),
        for (var index = 0; index < relays.length; index++) ...[
          _RelayEditor(
            relay: relays[index],
            functions: functions,
            testing: testingChannel == relays[index].channel,
            onChanged: (value) => setState(() => relays[index] = value),
            onTest: () => _testRelay(relays[index].channel),
          ),
          const SizedBox(height: 8),
        ],
        if (warnings.isNotEmpty)
          StatusBanner(
            icon: Icons.warning_rounded,
            title: 'Uwagi do profilu',
            message: warnings.join(' '),
            isError: true,
          ),
        const SizedBox(height: 12),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            SaveButton(
              onPressed: _save,
              label: 'Zapisz profil na sterowniku',
              busy: saving,
            ),
            OutlinedButton.icon(
              onPressed: _export,
              icon: const Icon(Icons.download_rounded),
              label: const Text('Eksportuj JSON'),
            ),
            OutlinedButton.icon(
              onPressed: _import,
              icon: const Icon(Icons.upload_file_rounded),
              label: const Text('Importuj JSON'),
            ),
            TextButton.icon(
              onPressed: () => setState(() {
                relays = _firmwareDefaults();
                message = 'Przywrócono domyślną mapę firmware.';
              }),
              icon: const Icon(Icons.restore_rounded),
              label: const Text('Mapa domyślna'),
            ),
          ],
        ),
        if (message != null)
          Padding(
            padding: const EdgeInsets.only(top: 10),
            child: Text(message!),
          ),
      ],
    );
  }
}

class _RelayEditor extends StatelessWidget {
  const _RelayEditor({
    required this.relay,
    required this.functions,
    required this.testing,
    required this.onChanged,
    required this.onTest,
  });

  final _RelayDefinition relay;
  final Map<String, String> functions;
  final bool testing;
  final ValueChanged<_RelayDefinition> onChanged;
  final VoidCallback onTest;

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: EdgeInsets.zero,
      child: ExpansionTile(
        leading: CircleAvatar(child: Text('${relay.channel}')),
        title: Text(
          relay.label,
          style: const TextStyle(fontWeight: FontWeight.w700),
        ),
        subtitle: Text(
          '${functions[relay.function] ?? relay.function} · awaria ${relay.safeState.toUpperCase()}',
        ),
        childrenPadding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
        children: [
          DropdownButtonFormField<String>(
            initialValue: functions.containsKey(relay.function)
                ? relay.function
                : 'custom',
            decoration: const InputDecoration(
              labelText: 'Funkcja kanału',
              border: OutlineInputBorder(),
            ),
            items: [
              for (final entry in functions.entries)
                DropdownMenuItem(value: entry.key, child: Text(entry.value)),
            ],
            onChanged: (value) {
              final function = value ?? relay.function;
              onChanged(
                relay.copyWith(
                  function: function,
                  label: functions[function] ?? relay.label,
                ),
              );
            },
          ),
          const SizedBox(height: 10),
          TextFormField(
            initialValue: relay.label,
            maxLength: 40,
            decoration: const InputDecoration(
              labelText: 'Etykieta',
              border: OutlineInputBorder(),
            ),
            onChanged: (value) => onChanged(
              relay.copyWith(
                label: value.trim().isEmpty
                    ? 'CH${relay.channel}'
                    : value.trim(),
              ),
            ),
          ),
          Row(
            children: [
              Expanded(
                child: DropdownButtonFormField<String>(
                  initialValue: relay.defaultState,
                  decoration: const InputDecoration(
                    labelText: 'Stan domyślny',
                    border: OutlineInputBorder(),
                  ),
                  items: const [
                    DropdownMenuItem(value: 'auto', child: Text('Automatyka')),
                    DropdownMenuItem(value: 'on', child: Text('ON')),
                    DropdownMenuItem(value: 'off', child: Text('OFF')),
                  ],
                  onChanged: (value) => onChanged(
                    relay.copyWith(defaultState: value ?? relay.defaultState),
                  ),
                ),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: DropdownButtonFormField<String>(
                  initialValue: relay.safeState,
                  decoration: const InputDecoration(
                    labelText: 'Stan awaryjny',
                    border: OutlineInputBorder(),
                  ),
                  items: const [
                    DropdownMenuItem(value: 'on', child: Text('ON')),
                    DropdownMenuItem(value: 'off', child: Text('OFF')),
                  ],
                  onChanged: (value) => onChanged(
                    relay.copyWith(safeState: value ?? relay.safeState),
                  ),
                ),
              ),
            ],
          ),
          LabeledSwitch(
            label: 'Sterowanie ręczne dozwolone',
            value: relay.manualAllowed,
            onChanged: (value) =>
                onChanged(relay.copyWith(manualAllowed: value)),
          ),
          LabeledSwitch(
            label: 'Wymagaj PIN-u',
            value: relay.pinRequired,
            onChanged: (value) => onChanged(relay.copyWith(pinRequired: value)),
          ),
          Align(
            alignment: Alignment.centerRight,
            child: OutlinedButton.icon(
              onPressed: testing ? null : onTest,
              icon: testing
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.bolt_rounded),
              label: const Text('Test 3 s'),
            ),
          ),
        ],
      ),
    );
  }
}

class _RelayDefinition {
  const _RelayDefinition({
    required this.channel,
    required this.function,
    required this.label,
    required this.defaultState,
    required this.safeState,
    required this.manualAllowed,
    required this.pinRequired,
  });

  factory _RelayDefinition.fromJson(JsonMap json) {
    final channel = json.integer('channel');
    if (channel < 1 || channel > 8) {
      throw const FormatException('Kanał poza zakresem CH1–CH8.');
    }
    return _RelayDefinition(
      channel: channel,
      function: json.text('function', 'none'),
      label: json.text('label', 'CH$channel'),
      defaultState: json.text('defaultState', 'off'),
      safeState: json.text('safeState', 'off'),
      manualAllowed: json.flag('manualAllowed', true),
      pinRequired: json.flag('pinRequired'),
    );
  }

  final int channel;
  final String function;
  final String label;
  final String defaultState;
  final String safeState;
  final bool manualAllowed;
  final bool pinRequired;

  _RelayDefinition copyWith({
    String? function,
    String? label,
    String? defaultState,
    String? safeState,
    bool? manualAllowed,
    bool? pinRequired,
  }) => _RelayDefinition(
    channel: channel,
    function: function ?? this.function,
    label: label ?? this.label,
    defaultState: defaultState ?? this.defaultState,
    safeState: safeState ?? this.safeState,
    manualAllowed: manualAllowed ?? this.manualAllowed,
    pinRequired: pinRequired ?? this.pinRequired,
  );

  Map<String, dynamic> toJson() => {
    'channel': channel,
    'function': function,
    'label': label,
    'defaultState': defaultState,
    'safeState': safeState,
    'manualAllowed': manualAllowed,
    'pinRequired': pinRequired,
  };
}

List<_RelayDefinition> _firmwareDefaults() => const [
  _RelayDefinition(
    channel: 1,
    function: 'main_light',
    label: 'Świetlówka przednia',
    defaultState: 'auto',
    safeState: 'off',
    manualAllowed: true,
    pinRequired: false,
  ),
  _RelayDefinition(
    channel: 2,
    function: 'plant_light',
    label: 'Świetlówka tylna',
    defaultState: 'auto',
    safeState: 'off',
    manualAllowed: true,
    pinRequired: false,
  ),
  _RelayDefinition(
    channel: 3,
    function: 'filter',
    label: 'Filtr',
    defaultState: 'auto',
    safeState: 'on',
    manualAllowed: true,
    pinRequired: false,
  ),
  _RelayDefinition(
    channel: 4,
    function: 'aeration',
    label: 'Napowietrzanie',
    defaultState: 'auto',
    safeState: 'off',
    manualAllowed: true,
    pinRequired: false,
  ),
  _RelayDefinition(
    channel: 5,
    function: 'heater',
    label: 'Grzałka',
    defaultState: 'auto',
    safeState: 'off',
    manualAllowed: true,
    pinRequired: true,
  ),
  _RelayDefinition(
    channel: 6,
    function: 'co2',
    label: 'Elektrozawór CO₂',
    defaultState: 'auto',
    safeState: 'off',
    manualAllowed: true,
    pinRequired: true,
  ),
  _RelayDefinition(
    channel: 7,
    function: 'feeder',
    label: 'Karmnik',
    defaultState: 'off',
    safeState: 'off',
    manualAllowed: true,
    pinRequired: true,
  ),
  _RelayDefinition(
    channel: 8,
    function: 'water_dosing',
    label: 'Dolewka ATO',
    defaultState: 'auto',
    safeState: 'off',
    manualAllowed: false,
    pinRequired: true,
  ),
];
