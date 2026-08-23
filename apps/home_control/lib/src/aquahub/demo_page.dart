import 'package:flutter/material.dart';

import 'controller.dart';
import 'demo.dart';
import 'shell.dart';

final class HubDemoPage extends StatefulWidget {
  const HubDemoPage({super.key});

  @override
  State<HubDemoPage> createState() => _HubDemoPageState();
}

final class _HubDemoPageState extends State<HubDemoPage> {
  late final HubController controller;

  @override
  void initState() {
    super.initState();
    controller = HubController(
      credentialsStore: DemoHubCredentialsStore(),
      apiFactory: createDemoHubApi,
      enablePolling: false,
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
    return AnimatedBuilder(
      animation: controller,
      builder: (context, _) => switch (controller.phase) {
        HubAppPhase.ready => HubShell(controller: controller, demoMode: true),
        HubAppPhase.failure => Scaffold(
          appBar: AppBar(title: const Text('Tryb demonstracyjny')),
          body: Center(
            child: Padding(
              padding: const EdgeInsets.all(24),
              child: Text(
                controller.errorMessage ?? 'Nie udało się otworzyć demo.',
                textAlign: TextAlign.center,
              ),
            ),
          ),
        ),
        _ => const Scaffold(body: Center(child: CircularProgressIndicator())),
      },
    );
  }
}
