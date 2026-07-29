import 'dart:async';

import 'package:flutter/material.dart';

import 'remote_alarm_gateway.dart';
import 'remote_push.dart';

typedef RemoteGatewayProvisionCallback =
    Future<String> Function(RemoteGatewayProvisioningRequest request);
typedef RemoteGatewayClearCallback = Future<String> Function();

class RemoteAlarmGatewayCard extends StatefulWidget {
  const RemoteAlarmGatewayCard({
    super.key,
    this.store,
    this.client,
    this.onProvisionController,
    this.onClearControllerProvisioning,
    this.provisioningUnavailableReason,
  }) : assert(
         (onProvisionController == null) ==
             (onClearControllerProvisioning == null),
         'Obie akcje provisioningu muszą być dostępne albo wyłączone.',
       );

  final RemoteAlarmGatewayStore? store;
  final RemoteAlarmGatewayClient? client;
  final RemoteGatewayProvisionCallback? onProvisionController;
  final RemoteGatewayClearCallback? onClearControllerProvisioning;
  final String? provisioningUnavailableReason;

  @override
  State<RemoteAlarmGatewayCard> createState() => _RemoteAlarmGatewayCardState();
}

class _RemoteAlarmGatewayCardState extends State<RemoteAlarmGatewayCard> {
  late final RemoteAlarmGatewayStore _store;
  late final RemoteAlarmGatewayClient _client;
  late final TextEditingController _baseUrl;
  late final TextEditingController _deviceId;
  late final TextEditingController _viewerToken;
  late final TextEditingController _hmacSecret;
  final _formKey = GlobalKey<FormState>();

  bool _enabled = false;
  bool _hasStoredToken = false;
  bool _loading = true;
  bool _busy = false;
  String? _message;
  bool _messageIsError = false;

  @override
  void initState() {
    super.initState();
    _store = widget.store ?? RemoteAlarmGatewayStore();
    _client = widget.client ?? RemoteAlarmGatewayClient();
    _baseUrl = TextEditingController();
    _deviceId = TextEditingController();
    _viewerToken = TextEditingController();
    _hmacSecret = TextEditingController();
    unawaited(_load());
  }

  @override
  void dispose() {
    _baseUrl.dispose();
    _deviceId.dispose();
    _viewerToken.dispose();
    _hmacSecret
      ..clear()
      ..dispose();
    super.dispose();
  }

  Future<void> _load() async {
    try {
      final configuration = await _store.load();
      if (!mounted) return;
      setState(() {
        _baseUrl.text = configuration?.baseUrl.toString() ?? '';
        _deviceId.text = configuration?.deviceId ?? '';
        _enabled = configuration?.enabled ?? false;
        _hasStoredToken = configuration?.hasViewerToken ?? false;
        _loading = false;
      });
    } on Object {
      if (!mounted) return;
      setState(() {
        _loading = false;
        _message = 'Nie udało się odczytać konfiguracji bramki.';
        _messageIsError = true;
      });
    }
  }

  RemoteAlarmGatewayConfiguration? _configuration({
    required bool requireToken,
  }) {
    if (_formKey.currentState?.validate() != true) return null;
    try {
      final configuration = RemoteAlarmGatewayConfiguration(
        baseUrl: Uri.parse(_baseUrl.text.trim()),
        deviceId: _deviceId.text,
        enabled: _enabled,
        hasViewerToken: _hasStoredToken || _viewerToken.text.trim().isNotEmpty,
      );
      if (requireToken && !configuration.hasViewerToken) {
        throw const FormatException('Podaj token viewer bramki.');
      }
      return configuration;
    } on FormatException catch (error) {
      setState(() {
        _message = error.message;
        _messageIsError = true;
      });
      return null;
    }
  }

  Future<void> _save() async {
    if (_busy) return;
    final configuration = _configuration(requireToken: _enabled);
    if (configuration == null) return;
    final coordinator = RemotePushScope.maybeOf(context);
    setState(() => _busy = true);
    try {
      final token = _viewerToken.text.trim();
      await _store.save(
        configuration,
        newViewerToken: token.isEmpty ? null : token,
      );
      var pushReady = true;
      if (coordinator != null) {
        try {
          await coordinator.reconcile();
        } on Object {
          pushReady = false;
        }
      }
      _viewerToken.clear();
      if (!mounted) return;
      setState(() {
        _hasStoredToken = configuration.hasViewerToken;
        _message = pushReady
            ? 'Konfiguracja bezpiecznej bramki została zapisana.'
            : 'Konfiguracja została zapisana, ale rejestracja push nie '
                  'powiodła się. Aplikacja ponowi ją po uruchomieniu.';
        _messageIsError = false;
      });
    } on Object catch (error) {
      if (!mounted) return;
      setState(() {
        _message = error is StateError
            ? error.message
            : 'Nie udało się zapisać konfiguracji bramki.';
        _messageIsError = true;
      });
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _test() async {
    if (_busy) return;
    final configuration = _configuration(requireToken: true);
    if (configuration == null) return;
    setState(() => _busy = true);
    try {
      final typedToken = _viewerToken.text.trim();
      late final String viewerToken;
      if (typedToken.isNotEmpty) {
        viewerToken = RemoteAlarmGatewayConfiguration.validateViewerToken(
          typedToken,
        );
      } else {
        final stored = await _store.loadCredentials();
        if (stored == null) {
          throw StateError(
            'Włącz i zapisz bramkę albo podaj nowy token viewer.',
          );
        }
        viewerToken = stored.viewerToken;
      }
      final result = await _client.test(
        RemoteAlarmGatewayCredentials(
          configuration: configuration.copyWith(enabled: true),
          viewerToken: viewerToken,
        ),
      );
      if (!mounted) return;
      setState(() {
        final latency = result.roundTrip;
        _message = latency == null
            ? result.message
            : '${result.message} ${latency.inMilliseconds} ms.';
        _messageIsError = !result.success;
      });
    } on FormatException catch (error) {
      if (!mounted) return;
      setState(() {
        _message = error.message;
        _messageIsError = true;
      });
    } on StateError catch (error) {
      if (!mounted) return;
      setState(() {
        _message = error.message;
        _messageIsError = true;
      });
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _clearToken() async {
    if (_busy || !_hasStoredToken) return;
    setState(() => _busy = true);
    try {
      var remotelyUnregistered = true;
      final coordinator = RemotePushScope.maybeOf(context);
      if (coordinator != null) {
        try {
          await coordinator.unregister();
        } on Object {
          remotelyUnregistered = false;
        }
      }
      await _store.clearViewerToken();
      if (!mounted) return;
      setState(() {
        _enabled = false;
        _hasStoredToken = false;
        _viewerToken.clear();
        _message = remotelyUnregistered
            ? 'Token viewer został bezpiecznie usunięty.'
            : 'Token lokalny usunięto. Bramka nie potwierdziła '
                  'wyrejestrowania push.';
        _messageIsError = !remotelyUnregistered;
      });
    } on Object {
      if (!mounted) return;
      setState(() {
        _message = 'Nie udało się usunąć tokenu viewer.';
        _messageIsError = true;
      });
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _provisionController() async {
    final callback = widget.onProvisionController;
    if (_busy || callback == null) return;
    final configuration = _configuration(requireToken: false);
    if (configuration == null) return;
    late final RemoteGatewayProvisioningRequest request;
    try {
      request = RemoteGatewayProvisioningRequest(
        baseUrl: configuration.baseUrl,
        deviceId: configuration.deviceId,
        hmacSecret: _hmacSecret.text,
        enabled: _enabled,
      );
    } on FormatException catch (error) {
      setState(() {
        _message = error.message;
        _messageIsError = true;
      });
      return;
    }
    setState(() => _busy = true);
    try {
      final result = await callback(request);
      if (!mounted) return;
      setState(() {
        _message = result;
        _messageIsError = false;
      });
    } on Object catch (error) {
      if (!mounted) return;
      setState(() {
        _message = error is StateError
            ? error.message
            : 'Nie udało się wysłać konfiguracji do sterownika przez BLE.';
        _messageIsError = true;
      });
    } finally {
      _hmacSecret.clear();
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _clearControllerProvisioning() async {
    final callback = widget.onClearControllerProvisioning;
    if (_busy || callback == null) return;
    setState(() => _busy = true);
    try {
      final result = await callback();
      if (!mounted) return;
      setState(() {
        _message = result;
        _messageIsError = false;
      });
    } on Object catch (error) {
      if (!mounted) return;
      setState(() {
        _message = error is StateError
            ? error.message
            : 'Nie udało się wyczyścić konfiguracji sterownika.';
        _messageIsError = true;
      });
    } finally {
      _hmacSecret.clear();
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final pushConfiguration = RemotePushRuntimeConfiguration.fromEnvironment();
    final canProvisionController = widget.onProvisionController != null;
    return Card(
      clipBehavior: Clip.antiAlias,
      child: ExpansionTile(
        key: const Key('remote-alarm-gateway-card'),
        leading: const Icon(Icons.cloud_done_outlined),
        title: const Text(
          'Bezpieczna bramka zdalna',
          style: TextStyle(fontWeight: FontWeight.w900),
        ),
        subtitle: Text(
          _enabled
              ? 'Zdalne alarmy włączone · token w bezpiecznym magazynie'
              : 'Opcjonalny HTTPS gateway i push poza siecią domową',
        ),
        childrenPadding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
        children: [
          if (_loading)
            const Padding(
              padding: EdgeInsets.all(20),
              child: Row(
                children: [
                  Icon(Icons.shield_outlined),
                  SizedBox(width: 12),
                  Expanded(
                    child: Text(
                      'Odczytywanie bezpiecznej konfiguracji aplikacji…',
                    ),
                  ),
                ],
              ),
            )
          else
            Form(
              key: _formKey,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  SwitchListTile.adaptive(
                    contentPadding: EdgeInsets.zero,
                    value: _enabled,
                    onChanged: _busy
                        ? null
                        : (value) => setState(() => _enabled = value),
                    title: const Text('Włącz zdalne alarmy'),
                    subtitle: const Text(
                      'Kanał lokalny działa niezależnie od tej opcji.',
                    ),
                  ),
                  ListTile(
                    contentPadding: EdgeInsets.zero,
                    leading: Icon(
                      pushConfiguration.isComplete
                          ? Icons.notifications_active_outlined
                          : Icons.notifications_none_outlined,
                    ),
                    title: const Text('Opcjonalny push poza aplikacją'),
                    subtitle: Text(
                      pushConfiguration.isComplete
                          ? 'FCM jest skonfigurowany. Token zostanie '
                                'zarejestrowany po zapisaniu aktywnej bramki.'
                          : 'Standardowy APK nie zawiera obowiązkowej '
                                'konfiguracji Firebase. Alarmy lokalne działają.',
                    ),
                  ),
                  TextFormField(
                    key: const Key('remote-gateway-base-url-field'),
                    controller: _baseUrl,
                    enabled: !_busy,
                    keyboardType: TextInputType.url,
                    autocorrect: false,
                    decoration: const InputDecoration(
                      labelText: 'Bazowy adres HTTPS',
                      hintText: 'https://gateway.example.com',
                      prefixIcon: Icon(Icons.https_rounded),
                    ),
                    validator: (value) {
                      final uri = Uri.tryParse(value?.trim() ?? '');
                      if (uri == null) return 'Podaj poprawny adres HTTPS.';
                      try {
                        RemoteAlarmGatewayConfiguration(
                          baseUrl: uri,
                          deviceId: 'test-device',
                          enabled: false,
                          hasViewerToken: false,
                        );
                        return null;
                      } on FormatException catch (error) {
                        return error.message;
                      }
                    },
                  ),
                  const SizedBox(height: 12),
                  TextFormField(
                    key: const Key('remote-gateway-device-id-field'),
                    controller: _deviceId,
                    enabled: !_busy,
                    autocorrect: false,
                    decoration: const InputDecoration(
                      labelText: 'Identyfikator urządzenia',
                      hintText: 'aquacyd-salon',
                      prefixIcon: Icon(Icons.developer_board_rounded),
                    ),
                    validator: (value) {
                      try {
                        RemoteAlarmGatewayConfiguration(
                          baseUrl: Uri.parse('https://gateway.invalid'),
                          deviceId: value ?? '',
                          enabled: false,
                          hasViewerToken: false,
                        );
                        return null;
                      } on FormatException catch (error) {
                        return error.message;
                      }
                    },
                  ),
                  const SizedBox(height: 12),
                  TextFormField(
                    key: const Key('remote-gateway-viewer-token-field'),
                    controller: _viewerToken,
                    enabled: !_busy,
                    obscureText: true,
                    autocorrect: false,
                    enableSuggestions: false,
                    decoration: InputDecoration(
                      labelText: _hasStoredToken
                          ? 'Nowy token viewer (opcjonalnie)'
                          : 'Token viewer',
                      helperText: _hasStoredToken
                          ? 'Obecny token pozostanie zapisany, jeśli pole jest puste.'
                          : 'Sekret trafi wyłącznie do systemowego magazynu kluczy.',
                      prefixIcon: const Icon(Icons.key_rounded),
                    ),
                    validator: (value) {
                      final token = value?.trim() ?? '';
                      if (token.isEmpty) return null;
                      try {
                        RemoteAlarmGatewayConfiguration.validateViewerToken(
                          token,
                        );
                        return null;
                      } on FormatException catch (error) {
                        return error.message;
                      }
                    },
                  ),
                  const SizedBox(height: 16),
                  const Divider(),
                  ListTile(
                    contentPadding: EdgeInsets.zero,
                    leading: const Icon(Icons.developer_board_rounded),
                    title: const Text(
                      'Provisioning sterownika',
                      style: TextStyle(fontWeight: FontWeight.w800),
                    ),
                    subtitle: Text(
                      canProvisionController
                          ? 'Dostępny przez aktywne, szyfrowane BLE v2. '
                                'Polecenie wymaga autoryzacji administratora.'
                          : widget.provisioningUnavailableReason ??
                                'Połącz sterownik przez BLE v2. Provisioning '
                                    'przez Wi‑Fi lub HTTP jest celowo zablokowany.',
                    ),
                  ),
                  TextFormField(
                    key: const Key('remote-gateway-hmac-secret-field'),
                    controller: _hmacSecret,
                    enabled: canProvisionController && !_busy,
                    obscureText: true,
                    autocorrect: false,
                    enableSuggestions: false,
                    decoration: const InputDecoration(
                      labelText: 'Efemeryczny sekret HMAC (Base64)',
                      helperText:
                          '32–64 bajty. Pole nie jest zapisywane w aplikacji '
                          'i zostanie wyczyszczone po próbie wysłania.',
                      prefixIcon: Icon(Icons.password_rounded),
                    ),
                    validator: (value) {
                      final secret = value?.trim() ?? '';
                      if (secret.isEmpty) return null;
                      try {
                        RemoteGatewayProvisioningRequest.validateHmacSecret(
                          secret,
                        );
                        return null;
                      } on FormatException catch (error) {
                        return error.message;
                      }
                    },
                  ),
                  if (_message case final message?) ...[
                    const SizedBox(height: 12),
                    Text(
                      message,
                      style: TextStyle(
                        color: _messageIsError ? colors.error : colors.primary,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                  ],
                  const SizedBox(height: 12),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      FilledButton.icon(
                        onPressed: _busy ? null : _save,
                        icon: const Icon(Icons.save_rounded),
                        label: const Text('Zapisz'),
                      ),
                      OutlinedButton.icon(
                        onPressed: _busy ? null : _test,
                        icon: const Icon(Icons.network_check_rounded),
                        label: const Text('Testuj połączenie'),
                      ),
                      TextButton.icon(
                        onPressed: _busy || !_hasStoredToken
                            ? null
                            : _clearToken,
                        icon: const Icon(Icons.delete_outline_rounded),
                        label: const Text('Wyczyść token'),
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      FilledButton.tonalIcon(
                        key: const Key('remote-gateway-provision-button'),
                        onPressed: !canProvisionController || _busy
                            ? null
                            : _provisionController,
                        icon: const Icon(Icons.bluetooth_connected_rounded),
                        label: const Text('Wyślij do sterownika przez BLE'),
                      ),
                      OutlinedButton.icon(
                        key: const Key(
                          'remote-gateway-clear-controller-button',
                        ),
                        onPressed: !canProvisionController || _busy
                            ? null
                            : _clearControllerProvisioning,
                        icon: const Icon(Icons.delete_forever_outlined),
                        label: const Text('Wyczyść w sterowniku'),
                      ),
                    ],
                  ),
                ],
              ),
            ),
        ],
      ),
    );
  }
}
