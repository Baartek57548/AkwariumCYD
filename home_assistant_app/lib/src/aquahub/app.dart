import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

import '../design/app_theme.dart';
import 'controller.dart';
import 'credentials_store.dart';
import 'setup_page.dart';
import 'shell.dart';

final class AquaHubApp extends StatefulWidget {
  const AquaHubApp({
    required this.credentialsStore,
    this.apiFactory,
    this.bootstrapFactory,
    this.enablePolling = true,
    super.key,
  });

  final HubCredentialsStore credentialsStore;
  final AuthenticatedHubApiFactory? apiFactory;
  final BootstrapHubApiFactory? bootstrapFactory;
  final bool enablePolling;

  @override
  State<AquaHubApp> createState() => _AquaHubAppState();
}

final class _AquaHubAppState extends State<AquaHubApp> {
  late final HubController controller;

  @override
  void initState() {
    super.initState();
    controller = HubController(
      credentialsStore: widget.credentialsStore,
      apiFactory: widget.apiFactory,
      bootstrapFactory: widget.bootstrapFactory,
      enablePolling: widget.enablePolling,
    );
    controller.initialize();
  }

  @override
  void dispose() {
    controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'AquaHub',
      theme: AquaTheme.light(),
      darkTheme: AquaTheme.dark(),
      themeMode: ThemeMode.system,
      locale: const Locale('pl'),
      supportedLocales: const <Locale>[Locale('pl'), Locale('en')],
      localizationsDelegates: const <LocalizationsDelegate<Object>>[
        GlobalMaterialLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
      ],
      home: AnimatedBuilder(
        animation: controller,
        builder: (context, _) => switch (controller.phase) {
          HubAppPhase.initializing => const _HubLoading(
            title: 'AquaHub',
            subtitle: 'Otwieram bezpieczny magazyn urządzenia…',
          ),
          HubAppPhase.connecting => const _HubLoading(
            title: 'Łączenie z AquaHub',
            subtitle: 'Weryfikuję certyfikat, token i rejestr urządzeń…',
          ),
          HubAppPhase.setup => HubSetupPage(controller: controller),
          HubAppPhase.ready => HubShell(controller: controller),
          HubAppPhase.failure => _HubFailure(controller: controller),
        },
      ),
    );
  }
}

final class _HubLoading extends StatelessWidget {
  const _HubLoading({required this.title, required this.subtitle});

  final String title;
  final String subtitle;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Padding(
          padding: const EdgeInsets.all(32),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: <Widget>[
              const HubMark(size: 86),
              const SizedBox(height: 26),
              Text(
                title,
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                  fontWeight: FontWeight.w800,
                ),
              ),
              const SizedBox(height: 10),
              Text(
                subtitle,
                textAlign: TextAlign.center,
                style: TextStyle(
                  color: Theme.of(context).colorScheme.onSurfaceVariant,
                ),
              ),
              const SizedBox(height: 28),
              const CircularProgressIndicator(),
            ],
          ),
        ),
      ),
    );
  }
}

final class _HubFailure extends StatelessWidget {
  const _HubFailure({required this.controller});

  final HubController controller;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 520),
          child: Padding(
            padding: const EdgeInsets.all(24),
            child: Card(
              child: Padding(
                padding: const EdgeInsets.all(28),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: <Widget>[
                    Icon(
                      Icons.hub_outlined,
                      size: 56,
                      color: Theme.of(context).colorScheme.error,
                    ),
                    const SizedBox(height: 18),
                    Text(
                      'AquaHub jest nieosiągalny',
                      style: Theme.of(context).textTheme.headlineSmall
                          ?.copyWith(fontWeight: FontWeight.w800),
                      textAlign: TextAlign.center,
                    ),
                    const SizedBox(height: 12),
                    Text(
                      controller.errorMessage ??
                          'Sprawdź sieć lokalną, VPN oraz zasilanie panelu.',
                      textAlign: TextAlign.center,
                    ),
                    const SizedBox(height: 24),
                    SizedBox(
                      width: double.infinity,
                      child: FilledButton.icon(
                        onPressed: controller.retry,
                        icon: const Icon(Icons.refresh_rounded),
                        label: const Text('Spróbuj ponownie'),
                      ),
                    ),
                    const SizedBox(height: 8),
                    TextButton(
                      onPressed: controller.disconnect,
                      child: const Text('Usuń sesję i sparuj ponownie'),
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

final class HubMark extends StatelessWidget {
  const HubMark({required this.size, super.key});

  final double size;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: size,
      height: size,
      decoration: BoxDecoration(
        gradient: const LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: <Color>[AquaColors.cyan, AquaColors.blue],
        ),
        borderRadius: BorderRadius.circular(size * 0.3),
      ),
      child: Icon(Icons.hub_rounded, size: size * 0.52, color: Colors.white),
    );
  }
}
