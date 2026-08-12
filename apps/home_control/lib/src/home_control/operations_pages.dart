import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:flutter/material.dart';
import 'package:home_entities/home_entities.dart';

import '../aquahub/app_update.dart';
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
        if (snapshot.automations.isEmpty)
          SliverFillRemaining(
            hasScrollBody: false,
            child: _CenteredMessage(
              icon: Icons.account_tree_outlined,
              text: strings.t('noAutomations'),
            ),
          )
        else
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 0, 20, 100),
            sliver: SliverList.builder(
              itemCount: snapshot.automations.length,
              itemBuilder: (context, index) {
                final automation = snapshot.automations[index];
                final entity = snapshot.entity(automation.id);
                return Card(
                  margin: const EdgeInsets.only(bottom: ProductSpacing.sm),
                  child: SwitchListTile(
                    secondary: const Icon(Icons.account_tree_rounded),
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
                    value: automation.enabled,
                    onChanged: entity?.writable == true
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
      ],
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
            builder: (context, _) => _AppUpdateCard(controller: appUpdate),
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
  const _AppUpdateCard({required this.controller});

  final AppUpdateController controller;

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
                    : controller.downloadAndInstall,
                icon: const Icon(Icons.download_rounded),
                label: Text(strings.t('install')),
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

final class _DeviceUpdateCard extends StatelessWidget {
  const _DeviceUpdateCard({required this.update, required this.controller});

  final HomeUpdate update;
  final HomeControlController controller;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final pending = controller.isPending(update.id);
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
                onPressed: pending
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
          child: SegmentedButton<ThemeMode>(
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
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  title: Text(strings.t('simulateOffline')),
                  value: controller.demoOffline,
                  onChanged: controller.setDemoOffline,
                ),
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  title: Text(strings.t('simulateAlarm')),
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
                subtitle: Text(controller.snapshot!.sourceKind.name),
              ),
              OutlinedButton.icon(
                onPressed: controller.switchSource,
                icon: const Icon(Icons.swap_horiz_rounded),
                label: Text(strings.t('switchSource')),
              ),
              const SizedBox(height: ProductSpacing.xs),
              TextButton.icon(
                onPressed: () => _confirmRemove(context),
                icon: const Icon(Icons.delete_outline_rounded),
                label: Text(strings.t('removeSource')),
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
