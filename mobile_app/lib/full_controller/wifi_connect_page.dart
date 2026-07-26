import 'package:flutter/material.dart';

import '../controller_address.dart';
import '../controller_preferences.dart';
import 'controller_api.dart';
import 'controller_session.dart';
import 'controller_shell.dart';
import 'widgets.dart';

class WifiConnectPage extends StatefulWidget {
  const WifiConnectPage({super.key, this.returnSession = false});

  final bool returnSession;

  @override
  State<WifiConnectPage> createState() => _WifiConnectPageState();
}

class _WifiConnectPageState extends State<WifiConnectPage> {
  final ControllerPreferences preferences = ControllerPreferences();
  final TextEditingController address = TextEditingController();
  bool loading = true;
  bool connecting = false;
  String? fieldError;
  String? errorTitle;
  String? connectionError;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    try {
      final saved = await preferences.loadAddress();
      if (!mounted) return;
      setState(() {
        address.text = saved.toString();
        loading = false;
      });
    } on Object catch (error) {
      if (!mounted) return;
      setState(() {
        address.text = ControllerAddress.defaultValue;
        errorTitle = 'Nie udało się wczytać adresu';
        connectionError =
            'Nie udało się odczytać zapisanego adresu. Możesz wpisać go ręcznie: $error';
        loading = false;
      });
    }
  }

  @override
  void dispose() {
    address.dispose();
    super.dispose();
  }

  Future<void> _connect() async {
    if (connecting) return;
    FocusManager.instance.primaryFocus?.unfocus();
    Uri uri;
    try {
      uri = ControllerAddress.parse(address.text);
    } on FormatException catch (exception) {
      setState(() {
        fieldError = exception.message;
        errorTitle = null;
        connectionError = null;
      });
      return;
    }
    setState(() {
      connecting = true;
      fieldError = null;
      errorTitle = null;
      connectionError = null;
    });
    final api = ControllerApi(uri);
    var handedOff = false;
    try {
      final status = await api.status(includeHistory: true);
      try {
        await preferences.saveAddress(uri);
        await preferences.saveAutoReconnect(true);
      } on Object catch (error) {
        if (mounted) {
          ScaffoldMessenger.of(context)
            ..hideCurrentSnackBar()
            ..showSnackBar(
              SnackBar(
                content: Text(
                  'Połączono, ale nie udało się zapisać adresu: $error',
                ),
              ),
            );
        }
      }
      if (!mounted) return;
      final session = ControllerSession.wifi(
        api,
        initialStatus: status,
        cachedAt: DateTime.now(),
      );
      handedOff = true;
      if (widget.returnSession) {
        Navigator.of(context).pop<ControllerSession>(session);
        return;
      }
      await Navigator.of(context).pushReplacement(
        MaterialPageRoute<void>(
          builder: (_) => ControllerShell(session: session),
        ),
      );
    } on ControllerApiException catch (exception) {
      if (mounted) {
        setState(() {
          errorTitle = 'Nie udało się połączyć';
          connectionError = exception.message;
        });
      }
    } on Object catch (error) {
      if (mounted) {
        setState(() {
          errorTitle = 'Nie udało się połączyć';
          connectionError =
              'Nie udało się nawiązać połączenia ze sterownikiem: $error';
        });
      }
    } finally {
      if (!handedOff) await api.disconnect();
      if (mounted) setState(() => connecting = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Połączenie Wi-Fi')),
      body: loading
          ? const StatePanel.loading(
              title: 'Wczytywanie adresu',
              message: 'Przygotowujemy ostatnio używane połączenie.',
            )
          : SafeArea(
              top: false,
              child: Center(
                child: SingleChildScrollView(
                  keyboardDismissBehavior:
                      ScrollViewKeyboardDismissBehavior.onDrag,
                  padding: const EdgeInsets.all(24),
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: 520),
                    child: Card(
                      child: Padding(
                        padding: const EdgeInsets.all(22),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.stretch,
                          children: [
                            const ExcludeSemantics(
                              child: Icon(Icons.router_rounded, size: 58),
                            ),
                            const SizedBox(height: 12),
                            Semantics(
                              header: true,
                              child: Text(
                                'Połącz ze sterownikiem',
                                textAlign: TextAlign.center,
                                style: Theme.of(context).textTheme.headlineSmall
                                    ?.copyWith(fontWeight: FontWeight.w800),
                              ),
                            ),
                            const SizedBox(height: 6),
                            Text(
                              'Telefon musi być w tej samej sieci lub połączony z punktem dostępowym cydAkwarium.',
                              textAlign: TextAlign.center,
                              style: Theme.of(context).textTheme.bodyMedium,
                            ),
                            if (connectionError != null) ...[
                              const SizedBox(height: 18),
                              StatusBanner(
                                icon: Icons.wifi_off_rounded,
                                title: errorTitle ?? 'Sprawdź adres sterownika',
                                message: connectionError!,
                                isError: true,
                              ),
                            ],
                            const SizedBox(height: 20),
                            TextField(
                              controller: address,
                              enabled: !connecting,
                              keyboardType: TextInputType.url,
                              autocorrect: false,
                              enableSuggestions: false,
                              autofillHints: const [AutofillHints.url],
                              textInputAction: TextInputAction.done,
                              decoration: InputDecoration(
                                labelText: 'Adres sterownika',
                                hintText: ControllerAddress.defaultValue,
                                helperText:
                                    'Wpisz nazwę lokalną albo adres IP sterownika.',
                                errorText: fieldError,
                              ),
                              onChanged: (_) {
                                if (fieldError != null ||
                                    connectionError != null) {
                                  setState(() {
                                    fieldError = null;
                                    errorTitle = null;
                                    connectionError = null;
                                  });
                                }
                              },
                              onSubmitted: (_) => _connect(),
                            ),
                            const SizedBox(height: 14),
                            FilledButton.icon(
                              onPressed: connecting ? null : _connect,
                              icon: connecting
                                  ? const SizedBox.square(
                                      dimension: 18,
                                      child: CircularProgressIndicator(
                                        strokeWidth: 2,
                                        semanticsLabel:
                                            'Sprawdzanie połączenia',
                                      ),
                                    )
                                  : const Icon(Icons.link_rounded),
                              label: Text(
                                connecting
                                    ? 'Sprawdzanie API…'
                                    : widget.returnSession
                                    ? 'Połącz i synchronizuj'
                                    : 'Połącz i otwórz aplikację',
                              ),
                            ),
                            const SizedBox(height: 8),
                            TextButton(
                              onPressed: connecting
                                  ? null
                                  : () => setState(() {
                                      address.text = 'http://192.168.4.1';
                                      fieldError = null;
                                      errorTitle = null;
                                      connectionError = null;
                                    }),
                              child: const Text(
                                'Użyj adresu punktu dostępowego 192.168.4.1',
                                textAlign: TextAlign.center,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                  ),
                ),
              ),
            ),
    );
  }
}
