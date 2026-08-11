import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'app.dart';
import 'controller.dart';

final class HubSetupPage extends StatefulWidget {
  const HubSetupPage({required this.controller, super.key});

  final HubController controller;

  @override
  State<HubSetupPage> createState() => _HubSetupPageState();
}

final class _HubSetupPageState extends State<HubSetupPage> {
  final _urlController = TextEditingController(
    text: 'https://aquahub.local:8443',
  );
  final _codeController = TextEditingController();
  bool _busy = false;
  bool _fingerprintConfirmed = false;

  @override
  void dispose() {
    _urlController.dispose();
    _codeController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final info = widget.controller.discoveredInfo;
    return Scaffold(
      body: SafeArea(
        child: Center(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(24),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 620),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: <Widget>[
                  const Align(
                    alignment: Alignment.centerLeft,
                    child: HubMark(size: 74),
                  ),
                  const SizedBox(height: 24),
                  Text(
                    info == null
                        ? 'Połącz centrum AquaHub'
                        : 'Potwierdź tożsamość',
                    style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  const SizedBox(height: 10),
                  Text(
                    info == null
                        ? 'Telefon łączy się tylko z panelem ESP32‑P4. Sterownik CYD pozostaje autonomiczny i nie otrzymuje dostępu do Wi‑Fi.'
                        : 'Porównaj odcisk SHA‑256 z ekranem System na panelu. To blokuje podszycie się pod AquaHub.',
                    style: TextStyle(
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                      height: 1.45,
                    ),
                  ),
                  const SizedBox(height: 26),
                  Card(
                    child: Padding(
                      padding: const EdgeInsets.all(22),
                      child: info == null ? _addressForm() : _pairingForm(),
                    ),
                  ),
                  if (widget.controller.errorMessage != null) ...<Widget>[
                    const SizedBox(height: 14),
                    Semantics(
                      liveRegion: true,
                      child: Text(
                        widget.controller.errorMessage!,
                        style: TextStyle(
                          color: Theme.of(context).colorScheme.error,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                    ),
                  ],
                  const SizedBox(height: 18),
                  Row(
                    children: <Widget>[
                      Icon(
                        Icons.lock_outline_rounded,
                        size: 20,
                        color: Theme.of(context).colorScheme.primary,
                      ),
                      const SizedBox(width: 10),
                      const Expanded(
                        child: Text(
                          'Token trafia do szyfrowanego magazynu systemu. Dostęp zdalny realizuj przez VPN.',
                        ),
                      ),
                    ],
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _addressForm() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: <Widget>[
        TextField(
          controller: _urlController,
          enabled: !_busy,
          keyboardType: TextInputType.url,
          autocorrect: false,
          decoration: const InputDecoration(
            labelText: 'Adres panelu',
            prefixIcon: Icon(Icons.router_outlined),
          ),
        ),
        const SizedBox(height: 16),
        FilledButton.icon(
          onPressed: _busy ? null : _discover,
          icon: _busy
              ? const SizedBox.square(
                  dimension: 18,
                  child: CircularProgressIndicator(strokeWidth: 2),
                )
              : const Icon(Icons.search_rounded),
          label: const Text('Sprawdź AquaHub'),
        ),
      ],
    );
  }

  Widget _pairingForm() {
    final fingerprint = widget.controller.discoveredInfo!.tlsFingerprint;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: <Widget>[
        Text(
          'Odcisk certyfikatu',
          style: Theme.of(context).textTheme.labelLarge,
        ),
        const SizedBox(height: 8),
        SelectableText(
          _formatFingerprint(fingerprint),
          style: const TextStyle(fontFamily: 'monospace', height: 1.5),
        ),
        const SizedBox(height: 14),
        CheckboxListTile(
          contentPadding: EdgeInsets.zero,
          controlAffinity: ListTileControlAffinity.leading,
          value: _fingerprintConfirmed,
          onChanged: _busy
              ? null
              : (value) =>
                    setState(() => _fingerprintConfirmed = value == true),
          title: const Text('Odcisk jest identyczny jak na panelu'),
        ),
        const SizedBox(height: 10),
        TextField(
          controller: _codeController,
          enabled: !_busy,
          keyboardType: TextInputType.number,
          inputFormatters: <TextInputFormatter>[
            FilteringTextInputFormatter.digitsOnly,
            LengthLimitingTextInputFormatter(6),
          ],
          decoration: const InputDecoration(
            labelText: '6‑cyfrowy kod z panelu',
            prefixIcon: Icon(Icons.pin_outlined),
          ),
        ),
        const SizedBox(height: 16),
        FilledButton.icon(
          onPressed: _busy ? null : _pair,
          icon: _busy
              ? const SizedBox.square(
                  dimension: 18,
                  child: CircularProgressIndicator(strokeWidth: 2),
                )
              : const Icon(Icons.verified_user_outlined),
          label: const Text('Sparuj bezpiecznie'),
        ),
        TextButton(
          onPressed: _busy ? null : _discover,
          child: const Text('Sprawdź połączenie ponownie'),
        ),
      ],
    );
  }

  Future<void> _discover() async {
    setState(() => _busy = true);
    await widget.controller.discover(_urlController.text);
    if (mounted) setState(() => _busy = false);
  }

  Future<void> _pair() async {
    setState(() => _busy = true);
    await widget.controller.pair(
      baseUrl: _urlController.text,
      code: _codeController.text,
      fingerprintConfirmed: _fingerprintConfirmed,
    );
    if (mounted) setState(() => _busy = false);
  }

  String _formatFingerprint(String value) {
    final groups = <String>[];
    for (var index = 0; index < value.length; index += 4) {
      groups.add(value.substring(index, index + 4));
    }
    return groups.join(' ');
  }
}
