import 'package:flutter/material.dart';

import '../design_system.dart';

enum OnboardingConnectionChoice { wifi, bluetooth, offline }

class OnboardingPage extends StatefulWidget {
  const OnboardingPage({super.key});

  @override
  State<OnboardingPage> createState() => _OnboardingPageState();
}

class _OnboardingPageState extends State<OnboardingPage> {
  final PageController _pageController = PageController();
  int _page = 0;

  static const _pages = <_OnboardingStep>[
    _OnboardingStep(
      icon: Icons.water_drop_rounded,
      title: 'Twoje akwarium pod kontrolą',
      description:
          'Aplikacja pokazuje ostatni zapisany stan również bez urządzenia. '
          'Sterowanie włącza się dopiero po bezpiecznym połączeniu i '
          'synchronizacji.',
      points: [
        'Alarmy i najważniejsze pomiary są zawsze na ekranie Start.',
        'Brak sieci nie usuwa historii ani zapisanych ustawień.',
        'Polecenia są blokowane, gdy dane sterownika są nieaktualne.',
      ],
    ),
    _OnboardingStep(
      icon: Icons.notifications_active_rounded,
      title: 'Alarmy bez szumu',
      description:
          'Powiadomienia dotyczą zdarzeń wymagających reakcji. Duplikaty są '
          'łączone, a zdarzenie pozostaje widoczne do potwierdzenia lub '
          'rozwiązania.',
      points: [
        'Wyciek i krytyczna temperatura mają najwyższy priorytet.',
        'Progi, wyciszenie i przypomnienia można później zmienić.',
        'Monitoring poza siecią lokalną wymaga VPN albo zaufanego pośrednika.',
      ],
    ),
    _OnboardingStep(
      icon: Icons.hub_rounded,
      title: 'Wybierz pierwsze połączenie',
      description:
          'Wi‑Fi zapewnia pełny zakres funkcji. Bluetooth pozwala rozpocząć '
          'lokalnie, a tryb offline pozostawia całe centrum dostępne bez '
          'wysyłania poleceń.',
      points: [
        'Zapamiętane Wi‑Fi będzie łączone automatycznie w tle.',
        'Bluetooth nie wymaga dostępu do routera.',
        'Połączenie możesz zmienić w każdej chwili w prawym górnym rogu.',
      ],
    ),
  ];

  @override
  void dispose() {
    _pageController.dispose();
    super.dispose();
  }

  Future<void> _next() async {
    if (_page >= _pages.length - 1) return;
    await _pageController.nextPage(
      duration: const Duration(milliseconds: 260),
      curve: Curves.easeOutCubic,
    );
  }

  void _finish(OnboardingConnectionChoice choice) {
    Navigator.of(context).pop(choice);
  }

  @override
  Widget build(BuildContext context) {
    final lastPage = _page == _pages.length - 1;
    return Scaffold(
      appBar: AppBar(
        title: const Text('Pierwsze uruchomienie'),
        actions: [
          TextButton(
            onPressed: () => _finish(OnboardingConnectionChoice.offline),
            child: const Text('Pomiń'),
          ),
          const SizedBox(width: AquaSpacing.xs),
        ],
      ),
      body: SafeArea(
        child: Column(
          children: [
            LinearProgressIndicator(
              value: (_page + 1) / _pages.length,
              minHeight: 3,
              semanticsLabel: 'Krok ${_page + 1} z ${_pages.length}',
            ),
            Expanded(
              child: PageView.builder(
                controller: _pageController,
                itemCount: _pages.length,
                onPageChanged: (value) => setState(() => _page = value),
                itemBuilder: (context, index) =>
                    _OnboardingStepView(step: _pages[index]),
              ),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(
                AquaSpacing.md,
                AquaSpacing.sm,
                AquaSpacing.md,
                AquaSpacing.lg,
              ),
              child: AnimatedSwitcher(
                duration: const Duration(milliseconds: 180),
                child: lastPage
                    ? Column(
                        key: const ValueKey('connection-actions'),
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          FilledButton.icon(
                            onPressed: () =>
                                _finish(OnboardingConnectionChoice.wifi),
                            icon: const Icon(Icons.wifi_rounded),
                            label: const Text('Skonfiguruj Wi‑Fi'),
                          ),
                          const SizedBox(height: AquaSpacing.sm),
                          OutlinedButton.icon(
                            onPressed: () =>
                                _finish(OnboardingConnectionChoice.bluetooth),
                            icon: const Icon(Icons.bluetooth_rounded),
                            label: const Text('Połącz przez Bluetooth'),
                          ),
                          const SizedBox(height: AquaSpacing.sm),
                          TextButton(
                            onPressed: () =>
                                _finish(OnboardingConnectionChoice.offline),
                            child: const Text('Zacznij offline'),
                          ),
                        ],
                      )
                    : FilledButton.icon(
                        key: const ValueKey('next-action'),
                        onPressed: _next,
                        icon: const Icon(Icons.arrow_forward_rounded),
                        label: const Text('Dalej'),
                      ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _OnboardingStepView extends StatelessWidget {
  const _OnboardingStepView({required this.step});

  final _OnboardingStep step;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return SingleChildScrollView(
      padding: const EdgeInsets.all(AquaSpacing.lg),
      child: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 620),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const SizedBox(height: AquaSpacing.lg),
              Align(
                child: Container(
                  width: 88,
                  height: 88,
                  decoration: BoxDecoration(
                    color: colors.primaryContainer,
                    borderRadius: BorderRadius.circular(AquaRadius.hero),
                  ),
                  child: Icon(
                    step.icon,
                    size: 46,
                    color: colors.onPrimaryContainer,
                  ),
                ),
              ),
              const SizedBox(height: AquaSpacing.xl),
              Text(
                step.title,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                  fontWeight: FontWeight.w900,
                ),
              ),
              const SizedBox(height: AquaSpacing.sm),
              Text(
                step.description,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodyLarge?.copyWith(
                  color: colors.onSurfaceVariant,
                  height: 1.45,
                ),
              ),
              const SizedBox(height: AquaSpacing.xl),
              Card(
                margin: EdgeInsets.zero,
                child: Padding(
                  padding: const EdgeInsets.all(AquaSpacing.md),
                  child: Column(
                    children: [
                      for (
                        var index = 0;
                        index < step.points.length;
                        index++
                      ) ...[
                        _OnboardingPoint(text: step.points[index]),
                        if (index != step.points.length - 1)
                          const SizedBox(height: AquaSpacing.md),
                      ],
                    ],
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _OnboardingPoint extends StatelessWidget {
  const _OnboardingPoint({required this.text});

  final String text;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Icon(Icons.check_circle_rounded, color: colors.primary, size: 22),
        const SizedBox(width: AquaSpacing.sm),
        Expanded(
          child: Text(
            text,
            style: const TextStyle(height: 1.35, fontWeight: FontWeight.w600),
          ),
        ),
      ],
    );
  }
}

class _OnboardingStep {
  const _OnboardingStep({
    required this.icon,
    required this.title,
    required this.description,
    required this.points,
  });

  final IconData icon;
  final String title;
  final String description;
  final List<String> points;
}
