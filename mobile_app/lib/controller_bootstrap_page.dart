import 'dart:async';

import 'package:flutter/material.dart';

import 'connection_home_page.dart';
import 'controller_preferences.dart';
import 'design_system.dart';
import 'full_controller/controller_api.dart';
import 'full_controller/controller_session.dart';
import 'full_controller/controller_shell.dart';

/// Automatycznie przywraca ostatnią poprawną sesję Wi‑Fi. Próba jest wykonywana
/// tylko dla adresu zapisanego po udanym połączeniu, więc pierwsze uruchomienie
/// zawsze od razu pokazuje bezpieczny wybór transportu.
class ControllerBootstrapPage extends StatefulWidget {
  const ControllerBootstrapPage({
    super.key,
    this.brandName = 'AquaCYD Control',
    this.showDevelopment = false,
    this.showLegacyWebView = false,
  });

  final String brandName;
  final bool showDevelopment;
  final bool showLegacyWebView;

  @override
  State<ControllerBootstrapPage> createState() =>
      _ControllerBootstrapPageState();
}

class _ControllerBootstrapPageState extends State<ControllerBootstrapPage> {
  final ControllerPreferences _preferences = ControllerPreferences();
  int _attempt = 0;
  Uri? _lastController;
  String? _failure;
  bool _checking = true;

  @override
  void initState() {
    super.initState();
    unawaited(_restore());
  }

  Future<void> _restore() async {
    final attempt = ++_attempt;
    ControllerApi? pendingApi;
    setState(() {
      _checking = true;
      _failure = null;
    });

    try {
      final address = await _preferences.loadSavedAddress();
      final autoReconnect = await _preferences.loadAutoReconnect();
      if (!mounted || attempt != _attempt) return;
      setState(() => _lastController = address);
      if (address == null || !autoReconnect) {
        setState(() => _checking = false);
        return;
      }

      final api = ControllerApi(
        address,
        requestDeadline: const Duration(seconds: 5),
        maximumReadAttempts: 1,
      );
      pendingApi = api;
      await api.status();
      if (!mounted || attempt != _attempt) {
        return;
      }
      final restoredSession = ControllerSession.wifi(api);
      pendingApi = null;
      setState(() => _checking = false);
      Navigator.of(context).push(
        MaterialPageRoute<void>(
          builder: (_) => ControllerShell(session: restoredSession),
        ),
      );
    } on ControllerApiException catch (error) {
      if (!mounted || attempt != _attempt) return;
      setState(() {
        _failure = error.message;
        _checking = false;
      });
    } on Object {
      if (!mounted || attempt != _attempt) return;
      setState(() {
        _failure =
            'Nie udało się automatycznie przywrócić połączenia. '
            'Sprawdź sieć telefonu albo wybierz inny transport.';
        _checking = false;
      });
    } finally {
      await pendingApi?.disconnect();
    }
  }

  void _cancelRestore() {
    _attempt++;
    setState(() => _checking = false);
  }

  @override
  Widget build(BuildContext context) {
    if (!_checking) {
      return ConnectionHomePage(
        brandName: widget.brandName,
        showDevelopment: widget.showDevelopment,
        showLegacyWebView: widget.showLegacyWebView,
        lastController: _lastController,
        resumeError: _failure,
        onRetryLast: _lastController == null
            ? null
            : () => unawaited(_restore()),
      );
    }

    final colors = Theme.of(context).colorScheme;
    return Scaffold(
      body: DecoratedBox(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
            colors: [
              colors.surface,
              colors.surface,
              colors.primaryContainer.withValues(alpha: 0.28),
            ],
          ),
        ),
        child: SafeArea(
          child: Center(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(AquaSpacing.lg),
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 430),
                child: Card(
                  child: Padding(
                    padding: const EdgeInsets.all(AquaSpacing.xl),
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        ClipRRect(
                          borderRadius: BorderRadius.circular(AquaRadius.card),
                          child: Image.asset(
                            'assets/branding/aquacyd-control-icon.png',
                            width: 76,
                            height: 76,
                            fit: BoxFit.cover,
                          ),
                        ),
                        const SizedBox(height: AquaSpacing.lg),
                        Text(
                          'Przywracanie centrum dowodzenia',
                          textAlign: TextAlign.center,
                          style: Theme.of(context).textTheme.headlineSmall
                              ?.copyWith(fontWeight: FontWeight.w900),
                        ),
                        const SizedBox(height: AquaSpacing.xs),
                        Text(
                          _lastController == null
                              ? 'Wczytywanie ostatniego sterownika…'
                              : 'Łączenie z ${_lastController!.host}…',
                          textAlign: TextAlign.center,
                          style: TextStyle(color: colors.onSurfaceVariant),
                        ),
                        const SizedBox(height: AquaSpacing.lg),
                        const LinearProgressIndicator(
                          semanticsLabel: 'Przywracanie połączenia',
                        ),
                        const SizedBox(height: AquaSpacing.md),
                        TextButton(
                          onPressed: _cancelRestore,
                          child: const Text('Wybierz inne połączenie'),
                        ),
                      ],
                    ),
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
