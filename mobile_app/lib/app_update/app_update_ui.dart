import 'package:flutter/material.dart';

import 'app_update_controller.dart';
import 'app_update_models.dart';

enum AppUpdatePromptAction { install, remindLater, skip }

class AppUpdateScope extends InheritedNotifier<AppUpdateController> {
  const AppUpdateScope({
    super.key,
    required AppUpdateController controller,
    required super.child,
  }) : super(notifier: controller);

  static AppUpdateController? maybeOf(
    BuildContext context, {
    bool listen = true,
  }) {
    final scope = listen
        ? context.dependOnInheritedWidgetOfExactType<AppUpdateScope>()
        : context.getInheritedWidgetOfExactType<AppUpdateScope>();
    return scope?.notifier;
  }
}

class AppUpdatePromptDialog extends StatelessWidget {
  const AppUpdatePromptDialog({super.key, required this.release});

  final AppRelease release;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final notes = _shortReleaseNotes(release.notes);
    final textScale = MediaQuery.textScalerOf(context).scale(1);
    final stackActions =
        MediaQuery.sizeOf(context).width < 380 || textScale > 1.5;
    final actions = <Widget>[
      TextButton(
        onPressed: () => Navigator.pop(context, AppUpdatePromptAction.skip),
        child: const Text('Pomiń tę wersję'),
      ),
      TextButton(
        onPressed: () =>
            Navigator.pop(context, AppUpdatePromptAction.remindLater),
        child: const Text('Później'),
      ),
      FilledButton(
        onPressed: () => Navigator.pop(context, AppUpdatePromptAction.install),
        child: const Text('Pobierz i zainstaluj'),
      ),
    ];
    return PopScope(
      canPop: false,
      child: AlertDialog(
        scrollable: true,
        icon: Icon(
          Icons.system_update_rounded,
          size: 40,
          color: colors.primary,
          semanticLabel: 'Dostępna aktualizacja',
        ),
        title: Text('AquaCYD ${release.version} jest dostępna'),
        content: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 460),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Nowa wersja ma ${release.formattedSize}. Po pobraniu Android poprosi o potwierdzenie instalacji.',
              ),
              if (notes.isNotEmpty) ...[
                const SizedBox(height: 16),
                Text(
                  'Co nowego',
                  style: Theme.of(
                    context,
                  ).textTheme.titleSmall?.copyWith(fontWeight: FontWeight.w800),
                ),
                const SizedBox(height: 6),
                Text(notes),
              ],
              if (stackActions) ...[
                const SizedBox(height: 20),
                for (var index = 0; index < actions.length; index++)
                  Padding(
                    padding: EdgeInsets.only(
                      bottom: index == actions.length - 1 ? 0 : 8,
                    ),
                    child: SizedBox(
                      width: double.infinity,
                      child: actions[index],
                    ),
                  ),
              ],
            ],
          ),
        ),
        actions: stackActions ? const [] : actions,
      ),
    );
  }

  static String _shortReleaseNotes(String notes) {
    final normalized = notes
        .replaceAll(RegExp(r'<!--[\s\S]*?-->'), '')
        .replaceAll(RegExp(r'[#*_`]'), '')
        .trim();
    if (normalized.length <= 500) return normalized;
    return '${normalized.substring(0, 497).trimRight()}…';
  }
}

class AppUpdateProgressDialog extends StatelessWidget {
  const AppUpdateProgressDialog({super.key, required this.controller});

  final AppUpdateController controller;

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: false,
      child: AnimatedBuilder(
        animation: controller,
        builder: (context, _) {
          final state = controller.state;
          final release = state.release;
          final percent = (state.progress * 100).round().clamp(0, 100);
          final textScale = MediaQuery.textScalerOf(context).scale(1);
          final stackActions =
              MediaQuery.sizeOf(context).width < 380 || textScale > 1.5;
          final actions = _actionsFor(context, state);
          return AlertDialog(
            scrollable: true,
            icon: _iconFor(context, state.phase),
            title: Text(_titleFor(state.phase, release)),
            content: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 420),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  if (state.phase == AppUpdatePhase.downloading) ...[
                    Semantics(
                      label: 'Postęp pobierania aktualizacji: $percent procent',
                      value: '$percent%',
                      child: LinearProgressIndicator(
                        value: state.progress.clamp(0, 1),
                      ),
                    ),
                    const SizedBox(height: 12),
                    Text(
                      '$percent% z ${release?.formattedSize ?? 'pliku'}',
                      textAlign: TextAlign.center,
                    ),
                  ] else if (state.phase == AppUpdatePhase.verifying) ...[
                    const Center(
                      child: CircularProgressIndicator(
                        semanticsLabel: 'Weryfikowanie aktualizacji',
                      ),
                    ),
                    const SizedBox(height: 14),
                    const Text(
                      'Sprawdzamy sumę SHA-256, pakiet, wersję i podpis aplikacji.',
                      textAlign: TextAlign.center,
                    ),
                  ] else
                    Text(
                      state.message ?? _messageFor(state.phase),
                      textAlign: TextAlign.center,
                    ),
                  if (stackActions && actions.isNotEmpty) ...[
                    const SizedBox(height: 20),
                    for (var index = 0; index < actions.length; index++)
                      Padding(
                        padding: EdgeInsets.only(
                          bottom: index == actions.length - 1 ? 0 : 8,
                        ),
                        child: SizedBox(
                          width: double.infinity,
                          child: actions[index],
                        ),
                      ),
                  ],
                ],
              ),
            ),
            actions: stackActions ? const [] : actions,
          );
        },
      ),
    );
  }

  Widget _iconFor(BuildContext context, AppUpdatePhase phase) {
    final colors = Theme.of(context).colorScheme;
    return switch (phase) {
      AppUpdatePhase.failed => Icon(
        Icons.error_outline_rounded,
        color: colors.error,
        size: 40,
        semanticLabel: 'Błąd aktualizacji',
      ),
      AppUpdatePhase.installerOpened => Icon(
        Icons.verified_rounded,
        color: colors.primary,
        size: 40,
        semanticLabel: 'Aktualizacja zweryfikowana',
      ),
      AppUpdatePhase.awaitingInstallPermission => Icon(
        Icons.admin_panel_settings_outlined,
        color: colors.tertiary,
        size: 40,
        semanticLabel: 'Wymagana zgoda systemowa',
      ),
      AppUpdatePhase.readyToInstall => Icon(
        Icons.download_done_rounded,
        color: colors.primary,
        size: 40,
        semanticLabel: 'Aktualizacja gotowa do instalacji',
      ),
      _ => Icon(
        Icons.system_update_rounded,
        color: colors.primary,
        size: 40,
        semanticLabel: 'Aktualizacja aplikacji',
      ),
    };
  }

  String _titleFor(AppUpdatePhase phase, AppRelease? release) {
    return switch (phase) {
      AppUpdatePhase.downloading =>
        'Pobieranie AquaCYD ${release?.version ?? ''}',
      AppUpdatePhase.verifying => 'Weryfikacja aktualizacji',
      AppUpdatePhase.readyToInstall => 'Aktualizacja została pobrana',
      AppUpdatePhase.awaitingInstallPermission => 'Potrzebna zgoda Androida',
      AppUpdatePhase.installerOpened => 'Aktualizacja jest gotowa',
      AppUpdatePhase.failed => 'Nie udało się zaktualizować',
      AppUpdatePhase.available => 'Pobieranie anulowane',
      _ => 'Aktualizacja AquaCYD',
    };
  }

  String _messageFor(AppUpdatePhase phase) {
    return switch (phase) {
      AppUpdatePhase.available => 'Plik aktualizacji nie został zainstalowany.',
      AppUpdatePhase.installerOpened =>
        'Potwierdź instalację w systemowym oknie Androida.',
      AppUpdatePhase.readyToInstall =>
        'Instalator otworzy się po powrocie do aplikacji.',
      _ => 'Przygotowywanie aktualizacji…',
    };
  }

  List<Widget> _actionsFor(BuildContext context, AppUpdateState state) {
    switch (state.phase) {
      case AppUpdatePhase.downloading:
        return [
          TextButton(
            onPressed: controller.cancelDownload,
            child: const Text('Anuluj'),
          ),
        ];
      case AppUpdatePhase.verifying:
        return const [];
      case AppUpdatePhase.awaitingInstallPermission:
        return [
          TextButton(
            onPressed: () {
              controller.closeInstallFlow();
              Navigator.pop(context);
            },
            child: const Text('Anuluj'),
          ),
          FilledButton(
            onPressed: controller.reopenInstallPermissionSettings,
            child: const Text('Otwórz ustawienia'),
          ),
        ];
      case AppUpdatePhase.failed:
        return [
          TextButton(
            onPressed: () {
              controller.closeInstallFlow();
              Navigator.pop(context);
            },
            child: const Text('Zamknij'),
          ),
          FilledButton(
            onPressed: controller.retryInstallFlow,
            child: const Text('Spróbuj ponownie'),
          ),
        ];
      default:
        return [
          FilledButton(
            onPressed: () {
              controller.closeInstallFlow();
              Navigator.pop(context);
            },
            child: const Text('Zamknij'),
          ),
        ];
    }
  }
}

class AppUpdateSettingsCard extends StatelessWidget {
  const AppUpdateSettingsCard({super.key, required this.controller});

  final AppUpdateController controller;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: controller,
      builder: (context, _) {
        final state = controller.state;
        final installed = state.installedApp;
        final checking = state.phase == AppUpdatePhase.checking;
        final available = state.release;
        return Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Row(
                  children: [
                    const Icon(Icons.system_update_alt_rounded),
                    const SizedBox(width: 12),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            'Aktualizacje aplikacji',
                            style: Theme.of(context).textTheme.titleMedium
                                ?.copyWith(fontWeight: FontWeight.w800),
                          ),
                          Text(
                            installed == null
                                ? 'Odczytywanie wersji…'
                                : 'Zainstalowana: ${installed.versionName} (${installed.versionCode})',
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
                if (available != null &&
                    available.version >
                        (installed?.semanticVersion ??
                            const SemanticVersion(0, 0, 0))) ...[
                  const SizedBox(height: 12),
                  Text(
                    'Dostępna: ${available.version} • ${available.formattedSize}',
                    style: TextStyle(
                      color: Theme.of(context).colorScheme.primary,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                ],
                const SizedBox(height: 14),
                OutlinedButton.icon(
                  onPressed: checking
                      ? null
                      : () => controller.checkForUpdates(manual: true),
                  icon: checking
                      ? const SizedBox.square(
                          dimension: 18,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.refresh_rounded),
                  label: Text(
                    checking ? 'Sprawdzanie…' : 'Sprawdź aktualizacje',
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class AppUpdateFooter extends StatelessWidget {
  const AppUpdateFooter({super.key});

  @override
  Widget build(BuildContext context) {
    final controller = AppUpdateScope.maybeOf(context, listen: false);
    if (controller == null) return const SizedBox.shrink();
    return AnimatedBuilder(
      animation: controller,
      builder: (context, _) {
        final state = controller.state;
        final installed = state.installedApp;
        if (state.phase == AppUpdatePhase.disabled || installed == null) {
          return const SizedBox.shrink();
        }
        return Center(
          child: TextButton.icon(
            onPressed: state.phase == AppUpdatePhase.checking
                ? null
                : () => controller.checkForUpdates(manual: true),
            icon: const Icon(Icons.info_outline_rounded),
            label: Text(
              state.phase == AppUpdatePhase.checking
                  ? 'Sprawdzanie aktualizacji…'
                  : 'AquaCYD ${installed.versionName} • sprawdź aktualizacje',
            ),
          ),
        );
      },
    );
  }
}
