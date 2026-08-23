import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:flutter/material.dart';
import 'package:home_entities/home_entities.dart';

import '../aquahub/app_update.dart';
import '../design/components.dart';
import 'biometric_gate.dart';
import 'controller.dart';
import 'entity_widgets.dart';
import 'preferences.dart';
import 'strings.dart';

final class AutomationsPage extends StatelessWidget {
  const AutomationsPage({required this.controller, super.key});

  final HomeControlController controller;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final snapshot = controller.snapshot!;
    final routines =
        snapshot.entities
            .where(
              (entity) => <HomeEntityType>{
                HomeEntityType.scene,
                HomeEntityType.script,
              }.contains(entity.type),
            )
            .toList(growable: false)
          ..sort((a, b) => a.name.compareTo(b.name));
    final scenes = routines
        .where((entity) => entity.type == HomeEntityType.scene)
        .toList(growable: false);
    final scripts = routines
        .where((entity) => entity.type == HomeEntityType.script)
        .toList(growable: false);
    return CustomScrollView(
      key: const PageStorageKey<String>('automations-page'),
      slivers: <Widget>[
        SliverPadding(
          padding: const EdgeInsets.fromLTRB(20, 20, 20, 12),
          sliver: SliverToBoxAdapter(
            child: Text(
              strings.t('automations'),
              style: Theme.of(
                context,
              ).textTheme.headlineMedium?.copyWith(fontWeight: FontWeight.w900),
            ),
          ),
        ),
        if (snapshot.automations.isEmpty && routines.isEmpty)
          SliverFillRemaining(
            hasScrollBody: false,
            child: _CenteredMessage(
              icon: Icons.account_tree_outlined,
              text: strings.t('noAutomations'),
            ),
          ),
        if (snapshot.automations.isNotEmpty)
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 0, 20, 16),
            sliver: SliverList.builder(
              itemCount: snapshot.automations.length,
              itemBuilder: (context, index) {
                final automation = snapshot.automations[index];
                final entity = snapshot.entity(automation.id);
                final pending =
                    entity != null && controller.isPending(entity.id);
                final enabled =
                    entity?.available == true &&
                    entity?.writable == true &&
                    !snapshot.isOffline &&
                    !pending;
                return Card(
                  margin: const EdgeInsets.only(bottom: ProductSpacing.sm),
                  child: SwitchListTile(
                    secondary: pending
                        ? const SizedBox.square(
                            dimension: 24,
                            child: CircularProgressIndicator(strokeWidth: 2.5),
                          )
                        : const Icon(Icons.account_tree_rounded),
                    title: Text(automation.name),
                    subtitle: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: <Widget>[
                        if (automation.description.isNotEmpty)
                          Text(automation.description),
                        if (automation.lastTriggered != null)
                          Text(strings.relativeTime(automation.lastTriggered)),
                      ],
                    ),
                    value: entity?.booleanValue ?? automation.enabled,
                    onChanged: enabled
                        ? (value) => requestEntityCommand(
                            context,
                            entity!,
                            value,
                            controller,
                          )
                        : null,
                  ),
                );
              },
            ),
          ),
        if (scenes.isNotEmpty) ...<Widget>[
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 8, 20, 12),
            sliver: SliverToBoxAdapter(
              child: HomeSectionHeader(
                title: strings.t('scenes'),
                subtitle: strings.t('scenesDescription'),
              ),
            ),
          ),
          _RoutineGrid(routines: scenes, controller: controller),
        ],
        if (scripts.isNotEmpty) ...<Widget>[
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 8, 20, 12),
            sliver: SliverToBoxAdapter(
              child: HomeSectionHeader(
                title: strings.t('scripts'),
                subtitle: strings.t('scriptsDescription'),
              ),
            ),
          ),
          _RoutineGrid(routines: scripts, controller: controller),
        ],
        const SliverToBoxAdapter(
          child: SizedBox(height: ProductLayout.pageBottomPadding),
        ),
      ],
    );
  }
}

final class _RoutineGrid extends StatelessWidget {
  const _RoutineGrid({required this.routines, required this.controller});

  final List<HomeEntity> routines;
  final HomeControlController controller;

  @override
  Widget build(BuildContext context) => SliverPadding(
    padding: const EdgeInsets.fromLTRB(
      ProductLayout.pageHorizontalPadding,
      0,
      ProductLayout.pageHorizontalPadding,
      ProductSpacing.md,
    ),
    sliver: SliverLayoutBuilder(
      builder: (context, constraints) {
        final textScale = MediaQuery.textScalerOf(context).scale(1);
        final columns =
            constraints.crossAxisExtent >= ProductLayout.threeColumnBreakpoint
            ? 3
            : constraints.crossAxisExtent >= ProductLayout.twoColumnBreakpoint
            ? 2
            : 1;
        if (columns == 1) {
          return SliverList.separated(
            itemCount: routines.length,
            separatorBuilder: (_, _) =>
                const SizedBox(height: ProductSpacing.sm),
            itemBuilder: (context, index) =>
                SceneCard(entity: routines[index], controller: controller),
          );
        }
        return SliverGrid(
          gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
            crossAxisCount: columns,
            mainAxisExtent: 112 + ((textScale - 1).clamp(0, 2) * 54),
            crossAxisSpacing: ProductSpacing.sm,
            mainAxisSpacing: ProductSpacing.sm,
          ),
          delegate: SliverChildBuilderDelegate(
            (context, index) =>
                SceneCard(entity: routines[index], controller: controller),
            childCount: routines.length,
          ),
        );
      },
    ),
  );
}

final class SceneCard extends StatelessWidget {
  const SceneCard({required this.entity, required this.controller, super.key});

  final HomeEntity entity;
  final HomeControlController controller;

  @override
  Widget build(BuildContext context) {
    assert(
      entity.type == HomeEntityType.scene ||
          entity.type == HomeEntityType.script,
      'SceneCard accepts only scene and script entities.',
    );
    final strings = HomeControlStrings.of(context);
    final scheme = Theme.of(context).colorScheme;
    final pending = controller.isPending(entity.id);
    final offline = controller.snapshot?.isOffline ?? true;
    final enabled = entity.available && entity.writable && !offline && !pending;
    final isScene = entity.type == HomeEntityType.scene;
    final action = strings.t(isScene ? 'activateScene' : 'runScript');
    final status = pending
        ? strings.t('commandPending')
        : offline
        ? strings.t('offline')
        : entity.available
        ? strings.t(isScene ? 'sceneReady' : 'scriptReady')
        : strings.t('unavailable');
    final accent = isScene ? scheme.secondary : scheme.primary;
    final accentContainer = isScene
        ? scheme.secondaryContainer
        : scheme.primaryContainer;
    final onAccentContainer = isScene
        ? scheme.onSecondaryContainer
        : scheme.onPrimaryContainer;
    return Card(
      clipBehavior: Clip.antiAlias,
      child: Padding(
        padding: const EdgeInsets.all(ProductSpacing.md),
        child: Row(
          children: <Widget>[
            ExcludeSemantics(
              child: Container(
                width: ProductLayout.minimumTouchTarget,
                height: ProductLayout.minimumTouchTarget,
                decoration: BoxDecoration(
                  color: accentContainer,
                  borderRadius: BorderRadius.circular(ProductRadius.control),
                ),
                child: Icon(
                  isScene
                      ? Icons.auto_awesome_rounded
                      : Icons.play_circle_rounded,
                  color: onAccentContainer,
                ),
              ),
            ),
            const SizedBox(width: ProductSpacing.sm),
            Expanded(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: <Widget>[
                  Text(
                    entity.name,
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis,
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                  const SizedBox(height: ProductSpacing.xxs),
                  Text(
                    status,
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis,
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: enabled || pending ? accent : scheme.error,
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(width: ProductSpacing.xs),
            AnimatedSwitcher(
              duration: ProductMotion.fast,
              child: pending
                  ? SizedBox.square(
                      key: ValueKey<String>(
                        'routine-pending-${entity.id.value}',
                      ),
                      dimension: ProductLayout.minimumTouchTarget,
                      child: const Padding(
                        padding: EdgeInsets.all(ProductSpacing.sm),
                        child: CircularProgressIndicator(strokeWidth: 2.5),
                      ),
                    )
                  : SizedBox.square(
                      key: ValueKey<String>(
                        'routine-action-${entity.id.value}',
                      ),
                      dimension: ProductLayout.minimumTouchTarget,
                      child: IconButton.filledTonal(
                        tooltip: '$action: ${entity.name}',
                        onPressed: enabled
                            ? () => requestEntityCommand(
                                context,
                                entity,
                                true,
                                controller,
                              )
                            : null,
                        icon: Icon(
                          isScene
                              ? Icons.auto_awesome_rounded
                              : Icons.play_arrow_rounded,
                        ),
                      ),
                    ),
            ),
          ],
        ),
      ),
    );
  }
}

final class UpdatesPage extends StatelessWidget {
  const UpdatesPage({required this.controller, super.key});

  final HomeControlController controller;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final appUpdate = AppUpdateScope.maybeOf(context);
    final updates = controller.snapshot!.updates;
    return ListView(
      key: const PageStorageKey<String>('updates-page'),
      padding: const EdgeInsets.fromLTRB(20, 20, 20, 100),
      children: <Widget>[
        Text(
          strings.t('updates'),
          style: Theme.of(
            context,
          ).textTheme.headlineMedium?.copyWith(fontWeight: FontWeight.w900),
        ),
        const SizedBox(height: ProductSpacing.lg),
        Text(
          strings.t('appUpdate'),
          style: Theme.of(
            context,
          ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
        ),
        const SizedBox(height: ProductSpacing.sm),
        if (appUpdate != null)
          AnimatedBuilder(
            animation: appUpdate,
            builder: (context, _) => _AppUpdateCard(
              controller: appUpdate,
              authorizeInstall: controller.authorizeCriticalOperation,
            ),
          ),
        const SizedBox(height: ProductSpacing.lg),
        Text(
          strings.t('deviceUpdates'),
          style: Theme.of(
            context,
          ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
        ),
        const SizedBox(height: ProductSpacing.sm),
        if (updates.isEmpty)
          _CenteredMessage(
            icon: Icons.system_update_rounded,
            text: strings.t('noUpdates'),
          )
        else
          for (final update in updates)
            _DeviceUpdateCard(update: update, controller: controller),
      ],
    );
  }
}

final class _AppUpdateCard extends StatelessWidget {
  const _AppUpdateCard({
    required this.controller,
    required this.authorizeInstall,
  });

  final AppUpdateController controller;
  final Future<bool> Function() authorizeInstall;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final release = controller.release;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(ProductSpacing.md),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: <Widget>[
            ListTile(
              contentPadding: EdgeInsets.zero,
              leading: const CircleAvatar(
                child: Icon(Icons.mobile_friendly_rounded),
              ),
              title: Text(strings.t('appName')),
              subtitle: Text(
                controller.installed == null
                    ? strings.t('noData')
                    : strings.withValue(
                        'currentVersion',
                        controller.installed!.label,
                      ),
              ),
              trailing: IconButton(
                tooltip: strings.t('refresh'),
                onPressed: controller.busy ? null : controller.check,
                icon: const Icon(Icons.refresh_rounded),
              ),
            ),
            if (controller.phase == AppUpdatePhase.downloading) ...<Widget>[
              LinearProgressIndicator(value: controller.progress),
              Text(
                '${(controller.progress * 100).round()}%',
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: ProductSpacing.sm),
            ],
            if (controller.errorMessage != null)
              Text(
                controller.errorMessage!,
                style: TextStyle(color: Theme.of(context).colorScheme.error),
              ),
            if (release != null) ...<Widget>[
              Text(strings.withValue('latestVersion', release.label)),
              if (release.notes.isNotEmpty) Text(release.notes),
              const SizedBox(height: ProductSpacing.sm),
              FilledButton.icon(
                onPressed: controller.busy
                    ? null
                    : () => _runAuthorizedUpdate(
                        authorizeInstall,
                        controller.phase == AppUpdatePhase.permissionRequired
                            ? controller.retryInstaller
                            : controller.downloadAndInstall,
                      ),
                icon: const Icon(Icons.download_rounded),
                label: Text(
                  strings.t(
                    controller.phase == AppUpdatePhase.permissionRequired
                        ? 'continueInstallation'
                        : 'install',
                  ),
                ),
              ),
            ] else if (controller.phase == AppUpdatePhase.upToDate)
              Text(strings.t('upToDate'))
            else if (controller.phase == AppUpdatePhase.unsupported)
              Text(strings.t('unsupported')),
          ],
        ),
      ),
    );
  }
}

Future<void> _runAuthorizedUpdate(
  Future<bool> Function() authorize,
  Future<void> Function() action,
) async {
  if (!await authorize()) return;
  await action();
}

final class _DeviceUpdateCard extends StatelessWidget {
  const _DeviceUpdateCard({required this.update, required this.controller});

  final HomeUpdate update;
  final HomeControlController controller;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final pending = controller.isPending(update.id);
    final sourceOffline = controller.snapshot?.isOffline ?? true;
    return Card(
      margin: const EdgeInsets.only(bottom: ProductSpacing.sm),
      child: Padding(
        padding: const EdgeInsets.all(ProductSpacing.md),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: <Widget>[
            ListTile(
              contentPadding: EdgeInsets.zero,
              leading: const CircleAvatar(child: Icon(Icons.memory_rounded)),
              title: Text(update.name),
              subtitle: Text(
                '${strings.withValue('currentVersion', update.currentVersion)}\n'
                '${strings.withValue('latestVersion', update.latestVersion)}',
              ),
              isThreeLine: true,
              trailing: update.mandatory
                  ? Chip(label: Text(strings.t('mandatory')))
                  : null,
            ),
            if (update.releaseNotes.isNotEmpty) Text(update.releaseNotes),
            if (update.progress > 0) ...<Widget>[
              const SizedBox(height: ProductSpacing.sm),
              LinearProgressIndicator(value: update.progress),
            ],
            if (update.error != null)
              Text(
                update.error!,
                style: TextStyle(color: Theme.of(context).colorScheme.error),
              ),
            const SizedBox(height: ProductSpacing.sm),
            if (update.canInstall)
              FilledButton.icon(
                onPressed: pending || sourceOffline
                    ? null
                    : () => _confirmInstall(context, update, controller),
                icon: pending
                    ? const SizedBox(
                        width: 18,
                        height: 18,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : const Icon(Icons.system_update_alt_rounded),
                label: Text(strings.t('install')),
              )
            else
              Text(
                strings.t(
                  update.phase == HomeUpdatePhase.unsupported
                      ? 'unsupported'
                      : 'upToDate',
                ),
              ),
          ],
        ),
      ),
    );
  }

  Future<void> _confirmInstall(
    BuildContext context,
    HomeUpdate update,
    HomeControlController controller,
  ) async {
    final strings = HomeControlStrings.of(context);
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        icon: const Icon(Icons.warning_amber_rounded),
        title: Text(strings.t('confirmTitle')),
        content: Text(strings.t('confirmCritical')),
        actions: <Widget>[
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: Text(strings.t('cancel')),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: Text(strings.t('confirm')),
          ),
        ],
      ),
    );
    if (confirmed == true) await controller.installUpdate(update);
  }
}

final class SettingsPage extends StatelessWidget {
  const SettingsPage({required this.controller, super.key});

  final HomeControlController controller;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final applicationVersion =
        AppUpdateScope.maybeOf(context)?.installed?.label ??
        homeControlVersionLabel;
    return ListView(
      key: const PageStorageKey<String>('settings-page'),
      padding: const EdgeInsets.fromLTRB(20, 20, 20, 100),
      children: <Widget>[
        Text(
          strings.t('settings'),
          style: Theme.of(
            context,
          ).textTheme.headlineMedium?.copyWith(fontWeight: FontWeight.w900),
        ),
        const SizedBox(height: ProductSpacing.lg),
        _SettingsSection(
          title: strings.t('theme'),
          child: LayoutBuilder(
            builder: (context, constraints) => constraints.maxWidth < 420
                ? DropdownButtonFormField<ThemeMode>(
                    initialValue: controller.themeMode,
                    decoration: InputDecoration(labelText: strings.t('theme')),
                    items: <DropdownMenuItem<ThemeMode>>[
                      for (final mode in ThemeMode.values)
                        DropdownMenuItem<ThemeMode>(
                          value: mode,
                          child: Semantics(
                            label: _themeLabel(strings, mode),
                            child: ExcludeSemantics(
                              child: Text(_themeLabel(strings, mode)),
                            ),
                          ),
                        ),
                    ],
                    onChanged: (value) {
                      if (value != null) controller.setThemeMode(value);
                    },
                  )
                : SegmentedButton<ThemeMode>(
                    segments: <ButtonSegment<ThemeMode>>[
                      ButtonSegment<ThemeMode>(
                        value: ThemeMode.system,
                        label: Text(strings.t('themeSystem')),
                        icon: const Icon(Icons.brightness_auto_rounded),
                      ),
                      ButtonSegment<ThemeMode>(
                        value: ThemeMode.light,
                        label: Text(strings.t('themeLight')),
                        icon: const Icon(Icons.light_mode_rounded),
                      ),
                      ButtonSegment<ThemeMode>(
                        value: ThemeMode.dark,
                        label: Text(strings.t('themeDark')),
                        icon: const Icon(Icons.dark_mode_rounded),
                      ),
                    ],
                    selected: <ThemeMode>{controller.themeMode},
                    onSelectionChanged: (values) =>
                        controller.setThemeMode(values.first),
                  ),
          ),
        ),
        _SettingsSection(
          title: strings.t('language'),
          child: SegmentedButton<String>(
            segments: <ButtonSegment<String>>[
              ButtonSegment<String>(
                value: 'pl',
                label: Text(strings.t('polish')),
              ),
              ButtonSegment<String>(
                value: 'en',
                label: Text(strings.t('english')),
              ),
            ],
            selected: <String>{controller.locale.languageCode},
            onSelectionChanged: (values) =>
                controller.setLocale(Locale(values.first)),
          ),
        ),
        _SettingsSection(
          title: strings.t('security'),
          child: HomeToggle(
            icon: Icons.fingerprint_rounded,
            label: strings.t('biometricProtection'),
            description: strings.t(switch (controller.biometricAvailability) {
              BiometricAvailability.available =>
                'biometricProtectionDescription',
              BiometricAvailability.unavailable => 'biometricUnavailable',
              BiometricAvailability.failed => 'biometricCheckFailed',
            }),
            value: controller.biometricProtectionEnabled,
            onChanged: controller.biometricBusy
                ? null
                : (value) => controller.setBiometricProtection(value),
          ),
        ),
        _SettingsSection(
          title: strings.t('dashboard'),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: <Widget>[
              OutlinedButton.icon(
                onPressed: () => Navigator.of(context).push<void>(
                  MaterialPageRoute<void>(
                    builder: (_) => DashboardEditorPage(controller: controller),
                  ),
                ),
                icon: const Icon(Icons.dashboard_customize_rounded),
                label: Text(strings.t('editDashboard')),
              ),
              TextButton(
                onPressed: controller.resetDashboard,
                child: Text(strings.t('resetDashboard')),
              ),
            ],
          ),
        ),
        if (controller.isDemo)
          _SettingsSection(
            title: strings.t('demoScenarios'),
            child: Column(
              children: <Widget>[
                HomeToggle(
                  label: strings.t('simulateOffline'),
                  value: controller.demoOffline,
                  onChanged: controller.setDemoOffline,
                ),
                HomeToggle(
                  label: strings.t('simulateAlarm'),
                  value: controller.demoAlarm,
                  onChanged: controller.setDemoAlarm,
                ),
              ],
            ),
          ),
        _SettingsSection(
          title: strings.t('sourceManagement'),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: <Widget>[
              ListTile(
                contentPadding: EdgeInsets.zero,
                leading: const Icon(Icons.hub_rounded),
                title: Text(controller.snapshot!.sourceName),
                subtitle: Text(
                  strings.sourceName(controller.snapshot!.sourceKind),
                ),
              ),
              OutlinedButton.icon(
                onPressed: controller.switchSource,
                icon: const Icon(Icons.swap_horiz_rounded),
                label: Text(strings.t('switchSource')),
              ),
              if (controller.activeSourceKind ==
                  HomeSourceKind.homeAssistant) ...<Widget>[
                const SizedBox(height: ProductSpacing.md),
                Align(
                  alignment: Alignment.centerLeft,
                  child: Text(
                    strings.t('haInstances'),
                    style: Theme.of(context).textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                ),
                RadioGroup<String>(
                  groupValue: controller.selectedHomeAssistantProfileId,
                  onChanged: (value) {
                    if (value != null) {
                      controller.selectHomeAssistantProfile(value);
                    }
                  },
                  child: Column(
                    children: <Widget>[
                      for (final profile in controller.homeAssistantProfiles)
                        RadioListTile<String>(
                          contentPadding: EdgeInsets.zero,
                          value: profile.id,
                          title: Text(profile.name),
                          subtitle: Text(profile.baseUri.toString()),
                          secondary: IconButton(
                            tooltip: strings.t('removeHaInstance'),
                            onPressed:
                                controller.homeAssistantProfiles.length <= 1
                                ? null
                                : () => _confirmDeleteHaProfile(
                                    context,
                                    profile.id,
                                    profile.name,
                                  ),
                            icon: const Icon(Icons.delete_outline_rounded),
                          ),
                        ),
                    ],
                  ),
                ),
                OutlinedButton.icon(
                  onPressed: controller.beginHomeAssistantSetup,
                  icon: const Icon(Icons.add_home_work_outlined),
                  label: Text(strings.t('addHaInstance')),
                ),
              ],
              const SizedBox(height: ProductSpacing.xs),
              TextButton.icon(
                onPressed: () => _confirmRemove(context),
                icon: const Icon(Icons.delete_outline_rounded),
                label: Text(strings.t('removeSource')),
              ),
            ],
          ),
        ),
        _SettingsSection(
          title: strings.t('diagnostics'),
          child: Column(
            children: <Widget>[
              _DiagnosticRow(
                label: strings.t('connection'),
                value: strings.t(
                  controller.snapshot!.isOffline ? 'offline' : 'connected',
                ),
              ),
              _DiagnosticRow(
                label: strings.t('lastSyncLabel'),
                value: strings.relativeTime(
                  controller.snapshot!.synchronizedAt,
                ),
              ),
              _DiagnosticRow(
                label: strings.t('entities'),
                value: '${controller.snapshot!.entities.length}',
              ),
              _DiagnosticRow(
                label: strings.t('devices'),
                value: '${controller.snapshot!.devices.length}',
              ),
              _DiagnosticRow(
                label: strings.t('localCache'),
                value: strings.t('encrypted'),
              ),
            ],
          ),
        ),
        _SettingsSection(
          title: strings.t('privacyAndAbout'),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: <Widget>[
              ListTile(
                contentPadding: EdgeInsets.zero,
                leading: const Icon(Icons.verified_user_outlined),
                title: Text(strings.t('privacy')),
                subtitle: Text(strings.t('privacyDescription')),
              ),
              ListTile(
                contentPadding: EdgeInsets.zero,
                leading: const Icon(Icons.info_outline_rounded),
                title: Text(strings.t('appName')),
                subtitle: Text(
                  strings.withValue('appVersion', applicationVersion),
                ),
              ),
              OutlinedButton.icon(
                onPressed: () => showLicensePage(
                  context: context,
                  applicationName: strings.t('appName'),
                  applicationVersion: applicationVersion,
                ),
                icon: const Icon(Icons.description_outlined),
                label: Text(strings.t('openSourceLicenses')),
              ),
            ],
          ),
        ),
      ],
    );
  }

  Future<void> _confirmRemove(BuildContext context) async {
    final strings = HomeControlStrings.of(context);
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        icon: const Icon(Icons.delete_forever_rounded),
        title: Text(strings.t('removeSource')),
        content: Text(strings.t('removeSourceConfirm')),
        actions: <Widget>[
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: Text(strings.t('cancel')),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: Text(strings.t('confirm')),
          ),
        ],
      ),
    );
    if (confirmed == true) await controller.removeActiveSource();
  }

  Future<void> _confirmDeleteHaProfile(
    BuildContext context,
    String id,
    String name,
  ) async {
    final strings = HomeControlStrings.of(context);
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        icon: const Icon(Icons.delete_outline_rounded),
        title: Text(strings.t('removeHaInstance')),
        content: Text(strings.withValue('removeHaInstanceConfirm', name)),
        actions: <Widget>[
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: Text(strings.t('cancel')),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: Text(strings.t('confirm')),
          ),
        ],
      ),
    );
    if (confirmed == true) {
      await controller.deleteHomeAssistantProfile(id);
    }
  }
}

String _themeLabel(HomeControlStrings strings, ThemeMode mode) =>
    strings.t(switch (mode) {
      ThemeMode.system => 'themeSystem',
      ThemeMode.light => 'themeLight',
      ThemeMode.dark => 'themeDark',
    });

final class _DiagnosticRow extends StatelessWidget {
  const _DiagnosticRow({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: ProductSpacing.xs),
    child: Row(
      children: <Widget>[
        Expanded(child: Text(label)),
        const SizedBox(width: ProductSpacing.sm),
        Flexible(
          child: Text(
            value,
            maxLines: 3,
            overflow: TextOverflow.ellipsis,
            textAlign: TextAlign.end,
            style: Theme.of(
              context,
            ).textTheme.bodyMedium?.copyWith(fontWeight: FontWeight.w700),
          ),
        ),
      ],
    ),
  );
}

final class DashboardEditorPage extends StatefulWidget {
  const DashboardEditorPage({required this.controller, super.key});

  final HomeControlController controller;

  @override
  State<DashboardEditorPage> createState() => _DashboardEditorPageState();
}

final class _DashboardEditorPageState extends State<DashboardEditorPage> {
  late DashboardPreferences value;

  @override
  void initState() {
    super.initState();
    value = widget.controller.dashboard;
  }

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    return Scaffold(
      appBar: AppBar(
        title: Text(strings.t('editDashboard')),
        actions: <Widget>[
          IconButton(
            tooltip: strings.t('confirm'),
            onPressed: () async {
              await widget.controller.saveDashboard(value);
              if (context.mounted) Navigator.pop(context);
            },
            icon: const Icon(Icons.check_rounded),
          ),
        ],
      ),
      body: ReorderableListView.builder(
        padding: const EdgeInsets.all(ProductSpacing.md),
        itemCount: value.order.length,
        onReorder: (oldIndex, newIndex) {
          setState(() {
            final order = List<String>.from(value.order);
            if (newIndex > oldIndex) newIndex--;
            final item = order.removeAt(oldIndex);
            order.insert(newIndex, item);
            value = value.copyWith(order: order);
          });
        },
        itemBuilder: (context, index) {
          final id = value.order[index];
          final hidden = value.hidden.contains(id);
          final large = value.largeCards.contains(id);
          return Card(
            key: ValueKey<String>(id),
            child: Column(
              children: <Widget>[
                SwitchListTile(
                  secondary: const Icon(Icons.drag_handle_rounded),
                  title: Text(_sectionName(strings, id)),
                  subtitle: Text(strings.t('visible')),
                  value: !hidden,
                  onChanged: (shown) => setState(() {
                    final values = Set<String>.from(value.hidden);
                    shown ? values.remove(id) : values.add(id);
                    value = value.copyWith(hidden: values);
                  }),
                ),
                SwitchListTile(
                  contentPadding: const EdgeInsets.only(left: 72, right: 16),
                  title: Text(strings.t('largeCard')),
                  value: large,
                  onChanged: (enabled) => setState(() {
                    final values = Set<String>.from(value.largeCards);
                    enabled ? values.add(id) : values.remove(id);
                    value = value.copyWith(largeCards: values);
                  }),
                ),
              ],
            ),
          );
        },
      ),
    );
  }

  String _sectionName(HomeControlStrings strings, String id) => switch (id) {
    'aquarium' => strings.t('aquarium'),
    'favorites' => strings.t('favorites'),
    'areas' => strings.t('areasOverview'),
    'activity' => strings.t('recentActivity'),
    _ => id,
  };
}

final class _SettingsSection extends StatelessWidget {
  const _SettingsSection({required this.title, required this.child});

  final String title;
  final Widget child;

  @override
  Widget build(BuildContext context) => Card(
    margin: const EdgeInsets.only(bottom: ProductSpacing.md),
    child: Padding(
      padding: const EdgeInsets.all(ProductSpacing.md),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: <Widget>[
          Text(
            title,
            style: Theme.of(
              context,
            ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
          ),
          const SizedBox(height: ProductSpacing.md),
          child,
        ],
      ),
    ),
  );
}

final class _CenteredMessage extends StatelessWidget {
  const _CenteredMessage({required this.icon, required this.text});

  final IconData icon;
  final String text;

  @override
  Widget build(BuildContext context) => Center(
    child: Padding(
      padding: const EdgeInsets.all(ProductSpacing.xl),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: <Widget>[
          Icon(icon, size: 54),
          const SizedBox(height: ProductSpacing.md),
          Text(text, textAlign: TextAlign.center),
        ],
      ),
    ),
  );
}
