import 'package:flutter/material.dart';

import '../controller_address.dart';
import '../controller_preferences.dart';
import 'controller_api.dart';
import 'controller_session.dart';
import 'controller_shell.dart';

class WifiConnectPage extends StatefulWidget {
  const WifiConnectPage({super.key});

  @override
  State<WifiConnectPage> createState() => _WifiConnectPageState();
}

class _WifiConnectPageState extends State<WifiConnectPage> {
  final ControllerPreferences preferences = ControllerPreferences();
  final TextEditingController address = TextEditingController();
  bool loading = true;
  bool connecting = false;
  String? error;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final saved = await preferences.loadAddress();
    if (!mounted) return;
    setState(() {
      address.text = saved.toString();
      loading = false;
    });
  }

  @override
  void dispose() {
    address.dispose();
    super.dispose();
  }

  Future<void> _connect() async {
    Uri uri;
    try {
      uri = ControllerAddress.parse(address.text);
    } on FormatException catch (exception) {
      setState(() => error = exception.message);
      return;
    }
    setState(() {
      connecting = true;
      error = null;
    });
    final api = ControllerApi(uri);
    try {
      await api.status(includeHistory: true);
      await preferences.saveAddress(uri);
      if (!mounted) return;
      await Navigator.of(context).pushReplacement(
        MaterialPageRoute<void>(
          builder: (_) => ControllerShell(session: ControllerSession.wifi(api)),
        ),
      );
    } on ControllerApiException catch (exception) {
      if (mounted) setState(() => error = exception.message);
    } finally {
      if (mounted) setState(() => connecting = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Połączenie Wi-Fi')),
      body: Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(24),
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 520),
            child: Card(
              child: Padding(
                padding: const EdgeInsets.all(22),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    const Icon(Icons.router_rounded, size: 58),
                    const SizedBox(height: 12),
                    Text(
                      'Połącz ze sterownikiem',
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.headlineSmall
                          ?.copyWith(fontWeight: FontWeight.w800),
                    ),
                    const SizedBox(height: 6),
                    Text(
                      'Telefon musi być w tej samej sieci lub połączony z punktem dostępowym cydAkwarium.',
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.bodyMedium,
                    ),
                    const SizedBox(height: 20),
                    TextField(
                      controller: address,
                      enabled: !loading && !connecting,
                      keyboardType: TextInputType.url,
                      autocorrect: false,
                      textInputAction: TextInputAction.done,
                      decoration: InputDecoration(
                        labelText: 'Adres sterownika',
                        hintText: 'http://akwarium.local',
                        border: const OutlineInputBorder(),
                        errorText: error,
                      ),
                      onSubmitted: (_) => _connect(),
                    ),
                    const SizedBox(height: 14),
                    FilledButton.icon(
                      onPressed: loading || connecting ? null : _connect,
                      icon: connecting
                          ? const SizedBox.square(
                              dimension: 18,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            )
                          : const Icon(Icons.link_rounded),
                      label: Text(
                        connecting
                            ? 'Sprawdzanie API…'
                            : 'Połącz i otwórz aplikację',
                      ),
                    ),
                    const SizedBox(height: 8),
                    TextButton(
                      onPressed: connecting
                          ? null
                          : () => setState(() {
                              address.text = 'http://192.168.4.1';
                              error = null;
                            }),
                      child: const Text(
                        'Użyj adresu punktu dostępowego 192.168.4.1',
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
