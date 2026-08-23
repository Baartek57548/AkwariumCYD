import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../home_control/strings.dart';
import 'app.dart';
import 'controller.dart';
import 'demo_page.dart';
import 'hub_discovery.dart';

final class HubSetupPage extends StatefulWidget {
  const HubSetupPage({
    required this.controller,
    required this.discoveryService,
    this.onBack,
    super.key,
  });

  final HubController controller;
  final HubDiscoveryService discoveryService;
  final VoidCallback? onBack;

  @override
  State<HubSetupPage> createState() => _HubSetupPageState();
}

final class _HubSetupPageState extends State<HubSetupPage> {
  final _urlController = TextEditingController(
    text: 'https://aquahub.local:8443',
  );
  final _codeController = TextEditingController();
  List<DiscoveredHub> _hubs = const <DiscoveredHub>[];
  bool _busy = false;
  bool _scanning = false;
  bool _scanCompleted = false;
  String? _scanErrorKey;
  String? _scanError;
  bool _fingerprintConfirmed = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _scan());
  }

  @override
  void dispose() {
    _urlController.dispose();
    _codeController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final info = widget.controller.discoveredInfo;
    final strings = HomeControlStrings.of(context);
    final controllerError = widget.controller.errorKey == null
        ? widget.controller.errorMessage
        : strings.t(widget.controller.errorKey!);
    return Scaffold(
      appBar: widget.onBack == null
          ? null
          : AppBar(
              leading: IconButton(
                tooltip: MaterialLocalizations.of(context).backButtonTooltip,
                onPressed: widget.onBack,
                icon: const Icon(Icons.arrow_back_rounded),
              ),
            ),
      body: SafeArea(
        child: Center(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(24),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 680),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: <Widget>[
                  const Align(
                    alignment: Alignment.centerLeft,
                    child: HubMark(size: 74),
                  ),
                  const SizedBox(height: 24),
                  Text(
                    strings.t(info == null ? 'hubWelcome' : 'hubConfirmPanel'),
                    style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  const SizedBox(height: 10),
                  Text(
                    strings.t(
                      info == null
                          ? 'hubWelcomeDescription'
                          : 'hubConfirmDescription',
                    ),
                    style: TextStyle(
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                      height: 1.45,
                    ),
                  ),
                  const SizedBox(height: 26),
                  Card(
                    child: Padding(
                      padding: const EdgeInsets.all(22),
                      child: info == null
                          ? _discoveryContent()
                          : _pairingForm(),
                    ),
                  ),
                  if (controllerError != null) ...<Widget>[
                    const SizedBox(height: 14),
                    Semantics(
                      liveRegion: true,
                      child: Text(
                        controllerError,
                        style: TextStyle(
                          color: Theme.of(context).colorScheme.error,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                    ),
                  ],
                  if (info == null) ...<Widget>[
                    const SizedBox(height: 18),
                    OutlinedButton.icon(
                      onPressed: _busy ? null : _openDemo,
                      icon: const Icon(Icons.dashboard_customize_outlined),
                      label: Text(strings.t('hubDemo')),
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
                      Expanded(child: Text(strings.t('hubAutonomyHint'))),
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

  Widget _discoveryContent() {
    final strings = HomeControlStrings.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: <Widget>[
        Row(
          children: <Widget>[
            CircleAvatar(
              child: _scanning
                  ? const SizedBox.square(
                      dimension: 20,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.wifi_find_rounded),
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: <Widget>[
                  Text(
                    _scanning
                        ? strings.t('hubSearching')
                        : _hubs.isNotEmpty
                        ? strings.t('hubChooseFound')
                        : strings.t('hubAutoDiscovery'),
                    style: Theme.of(context).textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  Text(
                    _scanning
                        ? strings.t('hubSameWifi')
                        : _hubs.isNotEmpty
                        ? strings.t('hubHttpsVerified')
                        : _scanErrorKey != null
                        ? strings.locale.languageCode == 'pl' &&
                                  _scanError != null
                              ? _scanError!
                              : strings.t(_scanErrorKey!)
                        : _scanCompleted
                        ? strings.t('hubNotFound')
                        : strings.t('hubMdns'),
                    style: TextStyle(
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                    ),
                  ),
                ],
              ),
            ),
            IconButton(
              tooltip: strings.t('scanAgain'),
              onPressed: _scanning || _busy ? null : _scan,
              icon: const Icon(Icons.refresh_rounded),
            ),
          ],
        ),
        if (_hubs.isNotEmpty) ...<Widget>[
          const SizedBox(height: 16),
          ..._hubs.map(
            (hub) => Padding(
              padding: const EdgeInsets.only(bottom: 10),
              child: Material(
                color: Theme.of(context).colorScheme.surfaceContainerHighest,
                borderRadius: BorderRadius.circular(16),
                child: ListTile(
                  enabled: !_busy,
                  leading: const Icon(Icons.hub_outlined),
                  title: Text(
                    hub.name,
                    style: const TextStyle(fontWeight: FontWeight.w700),
                  ),
                  subtitle: Text(hub.addressLabel),
                  trailing: const Icon(Icons.chevron_right_rounded),
                  onTap: () => _selectHub(hub),
                ),
              ),
            ),
          ),
        ],
        const SizedBox(height: 14),
        ExpansionTile(
          tilePadding: EdgeInsets.zero,
          childrenPadding: EdgeInsets.zero,
          leading: const Icon(Icons.tune_rounded),
          title: Text(strings.t('hubAdvancedConnection')),
          subtitle: Text(strings.t('hubManualOnly')),
          children: <Widget>[
            const SizedBox(height: 8),
            TextField(
              controller: _urlController,
              enabled: !_busy,
              keyboardType: TextInputType.url,
              autocorrect: false,
              decoration: InputDecoration(
                labelText: strings.t('hubHttpsAddress'),
                prefixIcon: const Icon(Icons.link_rounded),
              ),
            ),
            const SizedBox(height: 14),
            FilledButton.icon(
              onPressed: _busy ? null : _discoverManual,
              icon: _busy
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.lock_open_rounded),
              label: Text(strings.t('connectManually')),
            ),
          ],
        ),
      ],
    );
  }

  Widget _pairingForm() {
    final info = widget.controller.discoveredInfo!;
    final strings = HomeControlStrings.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: <Widget>[
        ListTile(
          contentPadding: EdgeInsets.zero,
          leading: const CircleAvatar(child: Icon(Icons.hub_outlined)),
          title: Text(
            info.hostname,
            style: const TextStyle(fontWeight: FontWeight.w800),
          ),
          subtitle: Text(strings.t('hubSecureHttps')),
          trailing: IconButton(
            tooltip: strings.t('chooseAnotherHub'),
            onPressed: _busy ? null : _chooseAnotherHub,
            icon: const Icon(Icons.swap_horiz_rounded),
          ),
        ),
        const Divider(height: 28),
        Text(
          strings.t('certificateFingerprint'),
          style: Theme.of(context).textTheme.labelLarge,
        ),
        const SizedBox(height: 8),
        SelectableText(
          _formatFingerprint(info.tlsFingerprint),
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
          title: Text(strings.t('fingerprintMatches')),
        ),
        const SizedBox(height: 10),
        TextField(
          controller: _codeController,
          enabled: !_busy,
          keyboardType: TextInputType.number,
          autofillHints: const <String>[AutofillHints.oneTimeCode],
          inputFormatters: <TextInputFormatter>[
            FilteringTextInputFormatter.digitsOnly,
            LengthLimitingTextInputFormatter(6),
          ],
          decoration: InputDecoration(
            labelText: strings.t('pairingCode'),
            prefixIcon: const Icon(Icons.pin_outlined),
          ),
          onSubmitted: (_) {
            if (!_busy) _pair();
          },
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
          label: Text(strings.t('pairAndOpen')),
        ),
      ],
    );
  }

  Future<void> _scan() async {
    if (_scanning || _busy || widget.controller.discoveredInfo != null) return;
    setState(() {
      _scanning = true;
      _scanCompleted = false;
      _scanErrorKey = null;
      _scanError = null;
    });
    try {
      final hubs = await widget.discoveryService.scan();
      if (!mounted) return;
      setState(() {
        _hubs = hubs;
        _scanning = false;
        _scanCompleted = true;
      });
      if (hubs.length == 1) await _selectHub(hubs.single);
    } on HubDiscoveryException catch (error) {
      if (!mounted) return;
      setState(() {
        _hubs = const <DiscoveredHub>[];
        _scanning = false;
        _scanCompleted = true;
        _scanErrorKey = 'hubDiscoveryFailed';
        _scanError = error.message;
      });
    } on Object {
      if (!mounted) return;
      setState(() {
        _hubs = const <DiscoveredHub>[];
        _scanning = false;
        _scanCompleted = true;
        _scanErrorKey = 'hubDiscoveryFailed';
        _scanError = null;
      });
    }
  }

  Future<void> _selectHub(DiscoveredHub hub) async {
    _urlController.text = hub.baseUri.toString();
    setState(() => _busy = true);
    await widget.controller.discover(_urlController.text);
    if (mounted) setState(() => _busy = false);
  }

  Future<void> _discoverManual() async {
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

  void _chooseAnotherHub() {
    widget.controller.resetDiscovery();
    setState(() {
      _fingerprintConfirmed = false;
      _codeController.clear();
    });
    _scan();
  }

  void _openDemo() {
    Navigator.of(
      context,
    ).push(MaterialPageRoute<void>(builder: (_) => const HubDemoPage()));
  }

  String _formatFingerprint(String value) {
    final groups = <String>[];
    for (var index = 0; index < value.length; index += 4) {
      groups.add(value.substring(index, index + 4));
    }
    return groups.join(' ');
  }
}
