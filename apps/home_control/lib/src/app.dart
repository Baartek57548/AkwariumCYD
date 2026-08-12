import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

import 'data/credentials_store.dart';
import 'design/app_theme.dart';
import 'state/aquacyd_controller.dart';
import 'ui/home_shell.dart';
import 'ui/onboarding_page.dart';

class AquaCydHomeApp extends StatefulWidget {
  const AquaCydHomeApp({
    required this.credentialsStore,
    this.apiFactory,
    this.socketFactory,
    this.enableRealtime = true,
    super.key,
  });

  final CredentialsStore credentialsStore;
  final HomeAssistantApiFactory? apiFactory;
  final HomeAssistantSocketFactory? socketFactory;
  final bool enableRealtime;

  @override
  State<AquaCydHomeApp> createState() => _AquaCydHomeAppState();
}

class _AquaCydHomeAppState extends State<AquaCydHomeApp> {
  late final AquaCydController _controller;

  @override
  void initState() {
    super.initState();
    _controller = AquaCydController(
      credentialsStore: widget.credentialsStore,
      apiFactory: widget.apiFactory,
      socketFactory: widget.socketFactory,
      enableRealtime: widget.enableRealtime,
    );
    _controller.initialize();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'AquaCYD Home',
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
        animation: _controller,
        builder: (context, _) {
          return switch (_controller.phase) {
            AquaAppPhase.loading => const _LoadingPage(),
            AquaAppPhase.connecting => const _LoadingPage(
              title: 'Łączenie z Home Assistantem',
              subtitle: 'Sprawdzam serwer, token i encje AquaCYD…',
            ),
            AquaAppPhase.setup => OnboardingPage(controller: _controller),
            AquaAppPhase.ready => HomeShell(controller: _controller),
            AquaAppPhase.failure => _FailurePage(controller: _controller),
          };
        },
      ),
    );
  }
}

class _LoadingPage extends StatelessWidget {
  const _LoadingPage({
    this.title = 'AquaCYD Home',
    this.subtitle = 'Przygotowuję bezpieczne połączenie…',
  });

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
              const _AppMark(size: 88),
              const SizedBox(height: 28),
              Text(
                title,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                  fontWeight: FontWeight.w800,
                ),
              ),
              const SizedBox(height: 10),
              Text(
                subtitle,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodyLarge?.copyWith(
                  color: Theme.of(context).colorScheme.onSurfaceVariant,
                ),
              ),
              const SizedBox(height: 30),
              const SizedBox(
                width: 34,
                height: 34,
                child: CircularProgressIndicator(strokeWidth: 3),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _FailurePage extends StatelessWidget {
  const _FailurePage({required this.controller});

  final AquaCydController controller;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 520),
          child: Padding(
            padding: const EdgeInsets.all(28),
            child: Card(
              child: Padding(
                padding: const EdgeInsets.all(28),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: <Widget>[
                    Icon(
                      Icons.cloud_off_rounded,
                      size: 58,
                      color: Theme.of(context).colorScheme.error,
                    ),
                    const SizedBox(height: 20),
                    Text(
                      'Home Assistant jest nieosiągalny',
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.headlineSmall
                          ?.copyWith(fontWeight: FontWeight.w800),
                    ),
                    const SizedBox(height: 12),
                    Text(
                      controller.errorMessage ??
                          'Sprawdź sieć lokalną i działanie serwera.',
                      textAlign: TextAlign.center,
                    ),
                    const SizedBox(height: 24),
                    SizedBox(
                      width: double.infinity,
                      child: FilledButton.icon(
                        onPressed: controller.retryConnection,
                        icon: const Icon(Icons.refresh_rounded),
                        label: const Text('Spróbuj ponownie'),
                      ),
                    ),
                    const SizedBox(height: 10),
                    SizedBox(
                      width: double.infinity,
                      child: TextButton(
                        onPressed: controller.beginReconfiguration,
                        child: const Text('Zmień adres lub token'),
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

class _AppMark extends StatelessWidget {
  const _AppMark({required this.size});

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
        boxShadow: <BoxShadow>[
          BoxShadow(
            color: AquaColors.cyan.withValues(alpha: 0.25),
            blurRadius: 28,
            offset: const Offset(0, 12),
          ),
        ],
      ),
      child: Icon(Icons.water_rounded, size: size * 0.55, color: Colors.white),
    );
  }
}
