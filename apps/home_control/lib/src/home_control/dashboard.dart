import 'dart:ui' as ui;

import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:aquacyd_protocol/aquacyd_protocol.dart';
import 'package:flutter/material.dart';
import 'package:home_entities/home_entities.dart';

import 'controller.dart';
import 'entity_widgets.dart';
import 'strings.dart';

final class HomeDashboardPage extends StatelessWidget {
  const HomeDashboardPage({
    required this.controller,
    this.onOpenRooms,
    this.onOpenDevices,
    this.onOpenUpdates,
    this.onCustomize,
    super.key,
  });

  final HomeControlController controller;
  final VoidCallback? onOpenRooms;
  final VoidCallback? onOpenDevices;
  final VoidCallback? onOpenUpdates;
  final VoidCallback? onCustomize;

  @override
  Widget build(BuildContext context) {
    final snapshot = controller.snapshot!;
    final strings = HomeControlStrings.of(context);
    final visible = controller.dashboard.order.where(
      (id) =>
          !controller.dashboard.hidden.contains(id) &&
          _sectionIsAvailable(id, snapshot),
    );
    final attention = _AttentionModel.fromSnapshot(
      snapshot,
      strings: strings,
      controller: controller,
      onOpenDevices: onOpenDevices,
      onOpenUpdates: onOpenUpdates,
      onOpenAquarium: () => showAquariumDetails(context, controller),
    );
    return RefreshIndicator(
      onRefresh: () async => controller.refresh(announce: true),
      child: CustomScrollView(
        key: const PageStorageKey<String>('home-dashboard'),
        slivers: <Widget>[
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 18, 20, 10),
            sliver: SliverToBoxAdapter(
              child: Center(
                child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 1280),
                  child: _HomeStatusHeader(
                    snapshot: snapshot,
                    attentionCount: attention.items.length,
                  ),
                ),
              ),
            ),
          ),
          if (attention.items.isNotEmpty)
            SliverPadding(
              padding: const EdgeInsets.fromLTRB(20, 6, 20, 10),
              sliver: SliverToBoxAdapter(
                child: Center(
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: 1280),
                    child: _AttentionCenter(model: attention),
                  ),
                ),
              ),
            ),
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 10, 20, 10),
            sliver: SliverToBoxAdapter(
              child: Center(
                child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 1280),
                  child: _DashboardSections(
                    sections: visible.toList(growable: false),
                    snapshot: snapshot,
                    controller: controller,
                    onOpenRooms: onOpenRooms,
                    onOpenDevices: onOpenDevices,
                    onCustomize: onCustomize,
                  ),
                ),
              ),
            ),
          ),
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(20, 14, 20, 104),
            sliver: SliverToBoxAdapter(
              child: Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: <Widget>[
                  Icon(
                    Icons.lock_outline_rounded,
                    size: 15,
                    color: Theme.of(context).colorScheme.onSurfaceVariant,
                  ),
                  const SizedBox(width: 6),
                  Flexible(
                    child: Text(
                      strings.withValue(
                        'lastSync',
                        strings.relativeTime(snapshot.synchronizedAt),
                      ),
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: Theme.of(context).colorScheme.onSurfaceVariant,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  static bool _sectionIsAvailable(String id, HomeSnapshot snapshot) =>
      switch (id) {
        'aquarium' => AquariumOverview.fromSnapshot(snapshot).present,
        _ => true,
      };
}

final class _HomeStatusHeader extends StatelessWidget {
  const _HomeStatusHeader({
    required this.snapshot,
    required this.attentionCount,
  });

  final HomeSnapshot snapshot;
  final int attentionCount;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final scheme = Theme.of(context).colorScheme;
    final dark = Theme.of(context).brightness == Brightness.dark;
    final online = snapshot.devices.where((device) => device.available).length;
    final enabledAutomations = snapshot.automations
        .where((automation) => automation.enabled)
        .length;
    final updates = snapshot.updates
        .where(
          (update) =>
              update.phase == HomeUpdatePhase.available || update.mandatory,
        )
        .length;
    final critical = snapshot.isOffline;
    final needsAttention = critical || attentionCount > 0;
    final accent = critical
        ? scheme.error
        : needsAttention
        ? scheme.tertiary
        : scheme.primary;
    final statusKey = critical
        ? 'homeOfflineTitle'
        : needsAttention
        ? 'homeAttentionTitle'
        : 'homeHealthyTitle';
    final statusDescription = critical
        ? strings.t('homeOfflineDescription')
        : needsAttention
        ? strings.withValue('homeAttentionDescription', attentionCount)
        : strings.t('homeHealthyDescription');

    return Semantics(
      container: true,
      label: '${strings.t(statusKey)}. $statusDescription',
      child: Container(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
            colors: <Color>[
              scheme.surfaceContainerLow,
              Color.alphaBlend(
                accent.withValues(alpha: dark ? 0.11 : 0.07),
                scheme.surfaceContainerLow,
              ),
            ],
          ),
          borderRadius: BorderRadius.circular(ProductRadius.hero),
          border: Border.all(
            color: accent.withValues(alpha: dark ? 0.24 : 0.16),
          ),
          boxShadow: <BoxShadow>[
            BoxShadow(
              color: scheme.shadow.withValues(alpha: dark ? 0.16 : 0.06),
              blurRadius: 26,
              offset: const Offset(0, 10),
            ),
          ],
        ),
        child: Padding(
          padding: const EdgeInsets.all(ProductSpacing.lg),
          child: LayoutBuilder(
            builder: (context, constraints) {
              final wide = constraints.maxWidth >= 580;
              final introduction = _StatusIntroduction(
                snapshot: snapshot,
                statusKey: statusKey,
                description: statusDescription,
                accent: accent,
              );
              final metrics = _HeaderMetrics(
                online: online,
                devices: snapshot.devices.length,
                automations: enabledAutomations,
                updates: updates,
              );
              if (!wide) {
                return Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: <Widget>[
                    introduction,
                    const SizedBox(height: ProductSpacing.lg),
                    metrics,
                  ],
                );
              }
              return Row(
                crossAxisAlignment: CrossAxisAlignment.center,
                children: <Widget>[
                  Expanded(flex: 7, child: introduction),
                  const SizedBox(width: ProductSpacing.xl),
                  Expanded(flex: 5, child: metrics),
                ],
              );
            },
          ),
        ),
      ),
    );
  }
}

final class _StatusIntroduction extends StatelessWidget {
  const _StatusIntroduction({
    required this.snapshot,
    required this.statusKey,
    required this.description,
    required this.accent,
  });

  final HomeSnapshot snapshot;
  final String statusKey;
  final String description;
  final Color accent;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final theme = Theme.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: <Widget>[
        Wrap(
          spacing: ProductSpacing.xs,
          runSpacing: ProductSpacing.xs,
          crossAxisAlignment: WrapCrossAlignment.center,
          children: <Widget>[
            _StatusPill(
              label: snapshot.isOffline
                  ? strings.t('offline')
                  : strings.t('liveStatus'),
              color: accent,
              icon: snapshot.isOffline ? Icons.cloud_off_rounded : Icons.circle,
            ),
            Text(
              snapshot.sourceName,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: theme.textTheme.labelMedium?.copyWith(
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ],
        ),
        const SizedBox(height: ProductSpacing.sm),
        Text(
          strings.t(statusKey),
          maxLines: 2,
          overflow: TextOverflow.ellipsis,
          style: theme.textTheme.headlineMedium?.copyWith(
            fontWeight: FontWeight.w800,
            letterSpacing: -0.8,
          ),
        ),
        const SizedBox(height: ProductSpacing.xs),
        Text(
          description,
          maxLines: 3,
          overflow: TextOverflow.ellipsis,
          style: theme.textTheme.bodyMedium?.copyWith(
            color: theme.colorScheme.onSurfaceVariant,
            height: 1.45,
          ),
        ),
      ],
    );
  }
}

final class _HeaderMetrics extends StatelessWidget {
  const _HeaderMetrics({
    required this.online,
    required this.devices,
    required this.automations,
    required this.updates,
  });

  final int online;
  final int devices;
  final int automations;
  final int updates;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final metrics = <Widget>[
      _HeaderMetric(
        icon: Icons.devices_other_rounded,
        value: '$online/$devices',
        label: strings.t('devicesOnlineLabel'),
      ),
      _HeaderMetric(
        icon: Icons.auto_awesome_motion_rounded,
        value: '$automations',
        label: strings.t('automationsActiveLabel'),
      ),
      _HeaderMetric(
        icon: updates > 0
            ? Icons.system_update_rounded
            : Icons.verified_rounded,
        value: '$updates',
        label: strings.t('updatesAvailableLabel'),
      ),
    ];
    return LayoutBuilder(
      builder: (context, constraints) {
        final textScale = MediaQuery.textScalerOf(context).scale(1);
        final stack = textScale > 1.35;
        if (stack) {
          return Column(
            children: <Widget>[
              for (var index = 0; index < metrics.length; index++) ...<Widget>[
                SizedBox(width: double.infinity, child: metrics[index]),
                if (index != metrics.length - 1)
                  const SizedBox(height: ProductSpacing.xs),
              ],
            ],
          );
        }
        return IntrinsicHeight(
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: <Widget>[
              for (var index = 0; index < metrics.length; index++) ...<Widget>[
                Expanded(child: metrics[index]),
                if (index != metrics.length - 1)
                  const SizedBox(width: ProductSpacing.xs),
              ],
            ],
          ),
        );
      },
    );
  }
}

final class _HeaderMetric extends StatelessWidget {
  const _HeaderMetric({
    required this.icon,
    required this.value,
    required this.label,
  });

  final IconData icon;
  final String value;
  final String label;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      constraints: const BoxConstraints(minHeight: 86),
      padding: const EdgeInsets.all(ProductSpacing.sm),
      decoration: BoxDecoration(
        color: scheme.surface.withValues(alpha: 0.66),
        borderRadius: BorderRadius.circular(ProductRadius.control),
        border: Border.all(
          color: scheme.outlineVariant.withValues(alpha: 0.45),
        ),
      ),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          Wrap(
            spacing: 6,
            runSpacing: 2,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: <Widget>[
              Icon(icon, size: 17, color: scheme.primary),
              Text(
                value,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: Theme.of(context).textTheme.titleLarge?.copyWith(
                  fontWeight: FontWeight.w800,
                  fontFeatures: const <ui.FontFeature>[
                    ui.FontFeature.tabularFigures(),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 4),
          Text(
            label,
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
            style: Theme.of(context).textTheme.labelSmall?.copyWith(
              color: scheme.onSurfaceVariant,
              height: 1.15,
            ),
          ),
        ],
      ),
    );
  }
}

final class _StatusPill extends StatelessWidget {
  const _StatusPill({
    required this.label,
    required this.color,
    required this.icon,
  });

  final String label;
  final Color color;
  final IconData icon;

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
    decoration: BoxDecoration(
      color: color.withValues(alpha: 0.13),
      borderRadius: BorderRadius.circular(999),
      border: Border.all(color: color.withValues(alpha: 0.25)),
    ),
    child: Row(
      mainAxisSize: MainAxisSize.min,
      children: <Widget>[
        Icon(icon, size: icon == Icons.circle ? 8 : 15, color: color),
        const SizedBox(width: 7),
        Text(
          label,
          style: Theme.of(context).textTheme.labelMedium?.copyWith(
            color: color,
            fontWeight: FontWeight.w700,
          ),
   ÷Î{¶‰žËkºwµçU°(€€€€€€€€€€€€€Ñ¥Ñ±”èÍÑÉ¥¹Ì¹Ð ¹½I••¹Ñ¡…¹•ÍQ¥Ñ±”œ¤°(€€€€€€€€€€€€€‘•ÍÉ¥ÁÑ¥½¸èÍÑÉ¥¹Ì¹Ð ¹½I••¹Ñ¡…¹•Ìœ¤°(€€€€€€€€€€€€¤(€€€€€€€€€€è½±Õµ¸ (€€€€€€€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€€€€€€€€€™½È€¡Ù…È¥¹‘•à€ô€Àì¥¹‘•à€ðÉ••¹Ð¹Ñ…­” Ô¤¹±•¹Ñ ì¥¹‘•à¬¬¤(€€€€€€€€€€€€€€€€€}Ñ¥Ù¥ÑåI½Ü (€€€€€€€€€€€€€€€€€€€•¹Ñ¥ÑäèÉ••¹Ñm¥¹‘•át°(€€€€€€€€€€€€€€€€€€€±…ÍÐè¥¹‘•à€ôôÉ••¹Ð¹Ñ…­” Ô¤¹±•¹Ñ €´€Ä°(€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€t°(€€€€€€€€€€€€¤°(€€€€¤ì(€ô)ô()™¥¹…°±…ÍÌ}Ñ¥Ù¥ÑåI½Ü•áÑ•¹‘ÌMÑ…Ñ•±•ÍÍ]¥‘•Ðì(€½¹ÍÐ}Ñ¥Ù¥ÑåI½Ü¡íÉ•ÅÕ¥É•Ñ¡¥Ì¹•¹Ñ¥Ñä°É•ÅÕ¥É•Ñ¡¥Ì¹±…ÍÑô¤ì((€™¥¹…°!½µ•¹Ñ¥Ñä•¹Ñ¥Ñäì(€™¥¹…°‰½½°±…ÍÐì((€½Ù•ÉÉ¥‘”(€]¥‘•Ð‰Õ¥±¡	Õ¥±‘½¹Ñ•áÐ½¹Ñ•áÐ¤ì(€€€™¥¹…°ÍÑÉ¥¹Ì€ô!½µ•½¹ÑÉ½±MÑÉ¥¹Ì¹½˜¡½¹Ñ•áÐ¤ì(€€€™¥¹…°Í¡•µ”€ôQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹½±½ÉM¡•µ”ì(€€€™¥¹…°¡…¹•‘Ð€ô•¹Ñ¥Ñä¹¡…¹•‘Ð€üü•¹Ñ¥Ñä¹ÕÁ‘…Ñ•‘Ðì(€€€™¥¹…°Ñ•áÑM…±”€ô5•‘¥…EÕ•Éä¹Ñ•áÑM…±•É=˜¡½¹Ñ•áÐ¤¹Í…±” Ä¤ì(€€€™¥¹…°ÑÉ…¥±¥¹Q¥µ”€ôQ•áÐ (€€€€€ÍÑÉ¥¹Ì¹É•±…Ñ¥Ù•Q¥µ”¡¡…¹•‘Ð¤°(€€€€€µ…á1¥¹•Ìè€Ä°(€€€€€½Ù•É™±½ÜèQ•áÑ=Ù•É™±½Ü¹•±±¥ÁÍ¥Ì°(€€€€€ÍÑå±”èQ¡•µ”¹½˜ (€€€€€€€½¹Ñ•áÐ°(€€€€€€¤¹Ñ•áÑQ¡•µ”¹±…‰•±Mµ…±°ü¹½Áå]¥Ñ ¡½±½ÈèÍ¡•µ”¹½¹MÕÉ™…•Y…É¥…¹Ð¤°(€€€€¤ì(€€€É•ÑÕÉ¸I½Ü (€€€€€É½ÍÍá¥Í±¥¹µ•¹ÐèÉ½ÍÍá¥Í±¥¹µ•¹Ð¹ÍÑ…ÉÐ°(€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€M¥é•‘	½à (€€€€€€€€€Ý¥‘Ñ è€Èà°(€€€€€€€€€¡¥±è½±Õµ¸ (€€€€€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€€€€€€€½¹Ñ…¥¹•È (€€€€€€€€€€€€€€€Ý¥‘Ñ è€ÄÀ°(€€€€€€€€€€€€€€€¡•¥¡Ðè€ÄÀ°(€€€€€€€€€€€€€€€µ…É¥¸è½¹ÍÐ‘•%¹Í•ÑÌ¹½¹±ä¡Ñ½Àè€Äà¤°(€€€€€€€€€€€€€€€‘•½É…Ñ¥½¸è	½á•½É…Ñ¥½¸ (€€€€€€€€€€€€€€€€€½±½Èè•¹Ñ¥Ñä¹…Ù…¥±…‰±”€üÍ¡•µ”¹ÁÉ¥µ…Éä€èÍ¡•µ”¹•ÉÉ½È°(€€€€€€€€€€€€€€€€€Í¡…Á”è	½áM¡…Á”¹¥É±”°(€€€€€€€€€€€€€€€€€‰½É‘•Èè	½É‘•È¹…±°¡½±½ÈèÍ¡•µ”¹ÍÕÉ™…”°Ý¥‘Ñ è€È¤°(€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€¥˜€ …±…ÍÐ¤(€€€€€€€€€€€€€€€½¹Ñ…¥¹•È¡Ý¥‘Ñ è€Ä°¡•¥¡Ðè€ÐØ°½±½ÈèÍ¡•µ”¹½ÕÑ±¥¹•Y…É¥…¹Ð¤°(€€€€€€€€€€€t°(€€€€€€€€€€¤°(€€€€€€€€¤°(€€€€€€€½¹ÍÐM¥é•‘	½à¡Ý¥‘Ñ èAÉ½‘ÕÑMÁ…¥¹œ¹áÌ¤°(€€€€€€€áÁ…¹‘• (€€€€€€€€€¡¥±èA…‘‘¥¹œ (€€€€€€€€€€€Á…‘‘¥¹œè½¹ÍÐ‘•%¹Í•ÑÌ¹Íåµµ•ÑÉ¥Œ¡Ù•ÉÑ¥…°è€ÄÀ¤°(€€€€€€€€€€€¡¥±èI½Ü (€€€€€€€€€€€€€É½ÍÍá¥Í±¥¹µ•¹ÐèÉ½ÍÍá¥Í±¥¹µ•¹Ð¹ÍÑ…ÉÐ°(€€€€€€€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€€€€€€€€€½¹Ñ…¥¹•È (€€€€€€€€€€€€€€€€€Ý¥‘Ñ è€ÐÀ°(€€€€€€€€€€€€€€€€€¡•¥¡Ðè€ÐÀ°(€€€€€€€€€€€€€€€€€‘•½É…Ñ¥½¸è	½á•½É…Ñ¥½¸ (€€€€€€€€€€€€€€€€€€€½±½ÈèÍ¡•µ”¹ÍÕÉ™…•½¹Ñ…¥¹•É!¥¡•ÍÐ°(€€€€€€€€€€€€€€€€€€€‰½É‘•ÉI…‘¥ÕÌè	½É‘•ÉI…‘¥ÕÌ¹¥ÉÕ±…È¡AÉ½‘ÕÑI…‘¥ÕÌ¹½¹ÑÉ½°¤°(€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€¡¥±è%½¸¡¥½¹½É¹Ñ¥Ñä¡•¹Ñ¥Ñä¹ÑåÁ”¤°Í¥é”è€ÈÄ¤°(€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€½¹ÍÐM¥é•‘	½à¡Ý¥‘Ñ èAÉ½‘ÕÑMÁ…¥¹œ¹Í´¤°(€€€€€€€€€€€€€€€áÁ…¹‘• (€€€€€€€€€€€€€€€€€¡¥±è½±Õµ¸ (€€€€€€€€€€€€€€€€€€€É½ÍÍá¥Í±¥¹µ•¹ÐèÉ½ÍÍá¥Í±¥¹µ•¹Ð¹ÍÑ…ÉÐ°(€€€€€€€€€€€€€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€€€€€€€€€€€€€€€Q•áÐ (€€€€€€€€€€€€€€€€€€€€€€€•¹Ñ¥Ñä¹¹…µ”°(€€€€€€€€€€€€€€€€€€€€€€€µ…á1¥¹•ÌèÑ•áÑM…±”€ø€Ä¸Ô€ü€È€è€Ä°(€€€€€€€€€€€€€€€€€€€€€€€½Ù•É™±½ÜèQ•áÑ=Ù•É™±½Ü¹•±±¥ÁÍ¥Ì°(€€€€€€€€€€€€€€€€€€€€€€€ÍÑå±”èQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹Ñ•áÑQ¡•µ”¹Ñ¥Ñ±•Mµ…±°°(€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€€Q•áÐ (€€€€€€€€€€€€€€€€€€€€€€€ÍÑÉ¥¹Ì¹•¹Ñ¥ÑåMÑ…Ñ”¡•¹Ñ¥Ñä¤°(€€€€€€€€€€€€€€€€€€€€€€€µ…á1¥¹•Ìè€Ä°(€€€€€€€€€€€€€€€€€€€€€€€½Ù•É™±½ÜèQ•áÑ=Ù•É™±½Ü¹•±±¥ÁÍ¥Ì°(€€€€€€€€€€€€€€€€€€€€€€€ÍÑå±”èQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹Ñ•áÑQ¡•µ”¹‰½‘åMµ…±°ü¹½Áå]¥Ñ  (€€€€€€€€€€€€€€€€€€€€€€€€€½±½ÈèÍ¡•µ”¹½¹MÕÉ™…•Y…É¥…¹Ð°(€€€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€€¥˜€¡Ñ•áÑM…±”€ø€Ä¸ÌÔ¤€¸¸¸ñ]¥‘•Ðùl(€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐM¥é•‘	½à¡¡•¥¡Ðè€È¤°(€€€€€€€€€€€€€€€€€€€€€€€ÑÉ…¥±¥¹Q¥µ”°(€€€€€€€€€€€€€€€€€€€€€t°(€€€€€€€€€€€€€€€€€€€t°(€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€¥˜€¡Ñ•áÑM…±”€ðô€Ä¸ÌÔ¤€¸¸¸ñ]¥‘•Ðùl(€€€€€€€€€€€€€€€€€½¹ÍÐM¥é•‘	½à¡Ý¥‘Ñ èAÉ½‘ÕÑMÁ…¥¹œ¹áÌ¤°(€€€€€€€€€€€€€€€€€±•á¥‰±”¡¡¥±èÑÉ…¥±¥¹Q¥µ”¤°(€€€€€€€€€€€€€€€t°(€€€€€€€€€€€€€t°(€€€€€€€€€€€€¤°(€€€€€€€€€€¤°(€€€€€€€€¤°(€€€€€t°(€€€€¤ì(€ô)ô()™¥¹…°±…ÍÌ}M•Ñ¥½¹MÕÉ™…”•áÑ•¹‘ÌMÑ…Ñ•±•ÍÍ]¥‘•Ðì(€½¹ÍÐ}M•Ñ¥½¹MÕÉ™…”¡ì(€€€É•ÅÕ¥É•Ñ¡¥Ì¹Ñ¥Ñ±”°(€€€É•ÅÕ¥É•Ñ¡¥Ì¹¥½¸°(€€€É•ÅÕ¥É•Ñ¡¥Ì¹¡¥±°(€€€Ñ¡¥Ì¹ÍÕ‰Ñ¥Ñ±”°(€€€Ñ¡¥Ì¹…Ñ¥½¹1…‰•°°(€€€Ñ¡¥Ì¹½¹Ñ¥½¸°(€€€Ñ¡¥Ì¹…•¹Ð°(€€€Ñ¡¥Ì¹‰…‘”°(€ô¤ì((€™¥¹…°MÑÉ¥¹œÑ¥Ñ±”ì(€™¥¹…°MÑÉ¥¹œüÍÕ‰Ñ¥Ñ±”ì(€™¥¹…°%½¹…Ñ„¥½¸ì(€™¥¹…°]¥‘•Ð¡¥±ì(€™¥¹…°MÑÉ¥¹œü…Ñ¥½¹1…‰•°ì(€™¥¹…°Y½¥‘…±±‰…¬ü½¹Ñ¥½¸ì(€™¥¹…°½±½Èü…•¹Ðì(€™¥¹…°MÑÉ¥¹œü‰…‘”ì((€½Ù•ÉÉ¥‘”(€]¥‘•Ð‰Õ¥±¡	Õ¥±‘½¹Ñ•áÐ½¹Ñ•áÐ¤ì(€€€™¥¹…°Í¡•µ”€ôQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹½±½ÉM¡•µ”ì(€€€™¥¹…°É•Í½±Ù•‘•¹Ð€ô…•¹Ð€üüÍ¡•µ”¹ÁÉ¥µ…Éäì(€€€É•ÑÕÉ¸½¹Ñ…¥¹•È (€€€€€Á…‘‘¥¹œè½¹ÍÐ‘•%¹Í•ÑÌ¹…±°¡AÉ½‘ÕÑMÁ…¥¹œ¹±œ¤°(€€€€€‘•½É…Ñ¥½¸è	½á•½É…Ñ¥½¸ (€€€€€€€½±½ÈèÍ¡•µ”¹ÍÕÉ™…•½¹Ñ…¥¹•É1½Ü°(€€€€€€€‰½É‘•ÉI…‘¥ÕÌè	½É‘•ÉI…‘¥ÕÌ¹¥ÉÕ±…È¡AÉ½‘ÕÑI…‘¥ÕÌ¹¡•É¼¤°(€€€€€€€‰½É‘•Èè	½É‘•È¹…±° (€€€€€€€€€½±½ÈèÍ¡•µ”¹½ÕÑ±¥¹•Y…É¥…¹Ð¹Ý¥Ñ¡Y…±Õ•Ì¡…±Á¡„è€À¸ÔÔ¤°(€€€€€€€€¤°(€€€€€€¤°(€€€€€¡¥±è½±Õµ¸ (€€€€€€€É½ÍÍá¥Í±¥¹µ•¹ÐèÉ½ÍÍá¥Í±¥¹µ•¹Ð¹ÍÑ…ÉÐ°(€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€€€1…å½ÕÑ	Õ¥±‘•È (€€€€€€€€€€€‰Õ¥±‘•Èè€¡½¹Ñ•áÐ°½¹ÍÑÉ…¥¹ÑÌ¤ì(€€€€€€€€€€€€€™¥¹…°Ñ•áÑM…±”€ô5•‘¥…EÕ•Éä¹Ñ•áÑM…±•É=˜¡½¹Ñ•áÐ¤¹Í…±” Ä¤ì(€€€€€€€€€€€€€™¥¹…°Í¡½Ý1…‰•°€ô½¹ÍÑÉ…¥¹ÑÌ¹µ…á]¥‘Ñ €øô€ÐØÀ€˜˜Ñ•áÑM…±”€ðô€Ä¸Ìì(€€€€€€€€€€€€€É•ÑÕÉ¸I½Ü (€€€€€€€€€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€€€€€€€€€€€½¹Ñ…¥¹•È (€€€€€€€€€€€€€€€€€€€Ý¥‘Ñ è€ÐÐ°(€€€€€€€€€€€€€€€€€€€¡•¥¡Ðè€ÐÐ°(€€€€€€€€€€€€€€€€€€€‘•½É…Ñ¥½¸è	½á•½É…Ñ¥½¸ (€€€€€€€€€€€€€€€€€€€€€½±½ÈèÉ•Í½±Ù•‘•¹Ð¹Ý¥Ñ¡Y…±Õ•Ì¡…±Á¡„è€À¸ÄÈ¤°(€€€€€€€€€€€€€€€€€€€€€‰½É‘•ÉI…‘¥ÕÌè	½É‘•ÉI…‘¥ÕÌ¹¥ÉÕ±…È (€€€€€€€€€€€€€€€€€€€€€€€AÉ½‘ÕÑI…‘¥ÕÌ¹½¹ÑÉ½°°(€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€¡¥±è%½¸¡¥½¸°Í¥é”è€ÈÈ°½±½ÈèÉ•Í½±Ù•‘•¹Ð¤°(€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€½¹ÍÐM¥é•‘	½à¡Ý¥‘Ñ èAÉ½‘ÕÑMÁ…¥¹œ¹Í´¤°(€€€€€€€€€€€€€€€€€áÁ…¹‘• (€€€€€€€€€€€€€€€€€€€¡¥±è½±Õµ¸ (€€€€€€€€€€€€€€€€€€€€€É½ÍÍá¥Í±¥¹µ•¹ÐèÉ½ÍÍá¥Í±¥¹µ•¹Ð¹ÍÑ…ÉÐ°(€€€€€€€€€€€€€€€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€€€€€€€€€€€€€€€€€I½Ü (€€€€€€€€€€€€€€€€€€€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€€€€€€€€€€€€€€€€€€€€€±•á¥‰±” (€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¡¥±èQ•áÐ (€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€Ñ¥Ñ±”°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€µ…á1¥¹•ÌèÑ•áÑM…±”€ø€Ä¸Ì€ü€È€è€Ä°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½Ù•É™±½ÜèQ•áÑ=Ù•É™±½Ü¹•±±¥ÁÍ¥Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€ÍÑå±”èQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹Ñ•áÑQ¡•µ”¹Ñ¥Ñ±•1…É”(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€ü¹½Áå]¥Ñ ¡™½¹Ñ]•¥¡Ðè½¹Ñ]•¥¡Ð¹ÜÜÀÀ¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€¥˜€¡‰…‘”€„ô¹Õ±°¤€¸¸¸ñ]¥‘•Ðùl(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹ÍÐM¥é•‘	½à¡Ý¥‘Ñ èAÉ½‘ÕÑMÁ…¥¹œ¹áÌ¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹Ñ…¥¹•È (€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€Á…‘‘¥¹œè½¹ÍÐ‘•%¹Í•ÑÌ¹Íåµµ•ÑÉ¥Œ (€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¡½É¥é½¹Ñ…°è€à°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€Ù•ÉÑ¥…°è€Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€‘•½É…Ñ¥½¸è	½á•½É…Ñ¥½¸ (€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½±½ÈèÉ•Í½±Ù•‘•¹Ð¹Ý¥Ñ¡Y…±Õ•Ì¡…±Á¡„è€À¸ÄÌ¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€‰½É‘•ÉI…‘¥ÕÌè	½É‘•ÉI…‘¥ÕÌ¹¥ÉÕ±…È äää¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¡¥±èQ•áÐ (€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€‰…‘”„°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€ÍÑå±”èQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹Ñ•áÑQ¡•µ”¹±…‰•±Mµ…±°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€ü¹½Áå]¥Ñ  (€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€½±½ÈèÉ•Í½±Ù•‘•¹Ð°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€™½¹Ñ]•¥¡Ðè½¹Ñ]•¥¡Ð¹ÜàÀÀ°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€t°(€€€€€€€€€€€€€€€€€€€€€€€€€t°(€€€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€€€€¥˜€¡ÍÕ‰Ñ¥Ñ±”€„ô¹Õ±°¤(€€€€€€€€€€€€€€€€€€€€€€€€€Q•áÐ (€€€€€€€€€€€€€€€€€€€€€€€€€€€ÍÕ‰Ñ¥Ñ±”„°(€€€€€€€€€€€€€€€€€€€€€€€€€€€µ…á1¥¹•Ìè€È°(€€€€€€€€€€€€€€€€€€€€€€€€€€€½Ù•É™±½ÜèQ•áÑ=Ù•É™±½Ü¹•±±¥ÁÍ¥Ì°(€€€€€€€€€€€€€€€€€€€€€€€€€€€ÍÑå±”èQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹Ñ•áÑQ¡•µ”¹‰½‘åMµ…±°(€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€ü¹½Áå]¥Ñ ¡½±½ÈèÍ¡•µ”¹½¹MÕÉ™…•Y…É¥…¹Ð¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€€€t°(€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€¥˜€¡½¹Ñ¥½¸€„ô¹Õ±°¤(€€€€€€€€€€€€€€€€€€€Í¡½Ý1…‰•°(€€€€€€€€€€€€€€€€€€€€€€€€üQ•áÑ	ÕÑÑ½¸¹¥½¸ (€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹AÉ•ÍÍ•è½¹Ñ¥½¸°(€€€€€€€€€€€€€€€€€€€€€€€€€€€¥½¸è½¹ÍÐ%½¸¡%½¹Ì¹…ÉÉ½Ý}™½ÉÝ…É‘}É½Õ¹‘•¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€€±…‰•°èQ•áÐ¡…Ñ¥½¹1…‰•°„¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€¤(€€€€€€€€€€€€€€€€€€€€€€€€è%½¹	ÕÑÑ½¸ (€€€€€€€€€€€€€€€€€€€€€€€€€€€Ñ½½±Ñ¥Àè…Ñ¥½¹1…‰•°°(€€€€€€€€€€€€€€€€€€€€€€€€€€€½¹AÉ•ÍÍ•è½¹Ñ¥½¸°(€€€€€€€€€€€€€€€€€€€€€€€€€€€¥½¸è½¹ÍÐ%½¸¡%½¹Ì¹…ÉÉ½Ý}™½ÉÝ…É‘}É½Õ¹‘•¤°(€€€€€€€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€t°(€€€€€€€€€€€€€€¤ì(€€€€€€€€€€€ô°(€€€€€€€€€€¤°(€€€€€€€€€½¹ÍÐM¥é•‘	½à¡¡•¥¡ÐèAÉ½‘ÕÑMÁ…¥¹œ¹±œ¤°(€€€€€€€€€¡¥±°(€€€€€€€t°(€€€€€€¤°(€€€€¤ì(€ô)ô()™¥¹…°±…ÍÌ}AÉ•µ¥ÕµµÁÑåMÑ…Ñ”•áÑ•¹‘ÌMÑ…Ñ•±•ÍÍ]¥‘•Ðì(€½¹ÍÐ}AÉ•µ¥ÕµµÁÑåMÑ…Ñ”¡ì(€€€É•ÅÕ¥É•Ñ¡¥Ì¹¥½¸°(€€€É•ÅÕ¥É•Ñ¡¥Ì¹Ñ¥Ñ±”°(€€€É•ÅÕ¥É•Ñ¡¥Ì¹‘•ÍÉ¥ÁÑ¥½¸°(€€€Ñ¡¥Ì¹…Ñ¥½¹1…‰•°°(€€€Ñ¡¥Ì¹½¹Ñ¥½¸°(€ô¤ì((€™¥¹…°%½¹…Ñ„¥½¸ì(€™¥¹…°MÑÉ¥¹œÑ¥Ñ±”ì(€™¥¹…°MÑÉ¥¹œ‘•ÍÉ¥ÁÑ¥½¸ì(€™¥¹…°MÑÉ¥¹œü…Ñ¥½¹1…‰•°ì(€™¥¹…°Y½¥‘…±±‰…¬ü½¹Ñ¥½¸ì((€½Ù•ÉÉ¥‘”(€]¥‘•Ð‰Õ¥±¡	Õ¥±‘½¹Ñ•áÐ½¹Ñ•áÐ¤ì(€€€™¥¹…°Í¡•µ”€ôQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹½±½ÉM¡•µ”ì(€€€É•ÑÕÉ¸½¹Ñ…¥¹•È (€€€€€Ý¥‘Ñ è‘½Õ‰±”¹¥¹™¥¹¥Ñä°(€€€€€Á…‘‘¥¹œè½¹ÍÐ‘•%¹Í•ÑÌ¹…±°¡AÉ½‘ÕÑMÁ…¥¹œ¹±œ¤°(€€€€€‘•½É…Ñ¥½¸è	½á•½É…Ñ¥½¸ (€€€€€€€½±½ÈèÍ¡•µ”¹ÍÕÉ™…•½¹Ñ…¥¹•È°(€€€€€€€‰½É‘•ÉI…‘¥ÕÌè	½É‘•ÉI…‘¥ÕÌ¹¥ÉÕ±…È¡AÉ½‘ÕÑI…‘¥ÕÌ¹…É¤°(€€€€€€¤°(€€€€€¡¥±è½±Õµ¸ (€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€€€½¹Ñ…¥¹•È (€€€€€€€€€€€Ý¥‘Ñ è€ÔÈ°(€€€€€€€€€€€¡•¥¡Ðè€ÔÈ°(€€€€€€€€€€€‘•½É…Ñ¥½¸è	½á•½É…Ñ¥½¸ (€€€€€€€€€€€€€½±½ÈèÍ¡•µ”¹ÁÉ¥µ…Éå½¹Ñ…¥¹•È°(€€€€€€€€€€€€€Í¡…Á”è	½áM¡…Á”¹¥É±”°(€€€€€€€€€€€€¤°(€€€€€€€€€€€¡¥±è%½¸¡¥½¸°½±½ÈèÍ¡•µ”¹½¹AÉ¥µ…Éå½¹Ñ…¥¹•È¤°(€€€€€€€€€€¤°(€€€€€€€€€½¹ÍÐM¥é•‘	½à¡¡•¥¡ÐèAÉ½‘ÕÑMÁ…¥¹œ¹Í´¤°(€€€€€€€€€Q•áÐ (€€€€€€€€€€€Ñ¥Ñ±”°(€€€€€€€€€€€Ñ•áÑ±¥¸èQ•áÑ±¥¸¹•¹Ñ•È°(€€€€€€€€€€€ÍÑå±”èQ¡•µ”¹½˜¡½¹Ñ•áÐ¤¹Ñ•áÑQ¡•µ”¹Ñ¥Ñ±•5•‘¥Õ´°(€€€€€€€€€€¤°(€€€€€€€€€½¹ÍÐM¥é•‘	½à¡¡•¥¡Ðè€Ð¤°(€€€€€€€€€Q•áÐ (€€€€€€€€€€€‘•ÍÉ¥ÁÑ¥½¸°(€€€€€€€€€€€Ñ•áÑ±¥¸èQ•áÑ±¥¸¹•¹Ñ•È°(€€€€€€€€€€€ÍÑå±”èQ¡•µ”¹½˜ (€€€€€€€€€€€€€½¹Ñ•áÐ°(€€€€€€€€€€€€¤¹Ñ•áÑQ¡•µ”¹‰½‘åMµ…±°ü¹½Áå]¥Ñ ¡½±½ÈèÍ¡•µ”¹½¹MÕÉ™…•Y…É¥…¹Ð¤°(€€€€€€€€€€¤°(€€€€€€€€€¥˜€¡½¹Ñ¥½¸€„ô¹Õ±°¤€¸¸¸ñ]¥‘•Ðùl(€€€€€€€€€€€½¹ÍÐM¥é•‘	½à¡¡•¥¡ÐèAÉ½‘ÕÑMÁ…¥¹œ¹µ¤°(€€€€€€€€€€€¥±±•‘	ÕÑÑ½¸¹Ñ½¹…±%½¸ (€€€€€€€€€€€€€½¹AÉ•ÍÍ•è½¹Ñ¥½¸°(€€€€€€€€€€€€€¥½¸è½¹ÍÐ%½¸¡%½¹Ì¹…‘‘}É½Õ¹‘•¤°(€€€€€€€€€€€€€±…‰•°èQ•áÐ¡…Ñ¥½¹1…‰•°„¤°(€€€€€€€€€€€€¤°(€€€€€€€€€t°(€€€€€€€t°(€€€€€€¤°(€€€€¤ì(€ô)ô()™¥¹…°±…ÍÌ}M•Ñ¥½¹…É•áÑ•¹‘ÌMÑ…Ñ•±•ÍÍ]¥‘•Ðì(€½¹ÍÐ}M•Ñ¥½¹…É¡ì(€€€É•ÅÕ¥É•Ñ¡¥Ì¹Ñ¥Ñ±”°(€€€É•ÅÕ¥É•Ñ¡¥Ì¹¥½¸°(€€€É•ÅÕ¥É•Ñ¡¥Ì¹¡¥±°(€ô¤ì((€™¥¹…°MÑÉ¥¹œÑ¥Ñ±”ì(€™¥¹…°%½¹…Ñ„¥½¸ì(€™¥¹…°]¥‘•Ð¡¥±ì((€½Ù•ÉÉ¥‘”(€]¥‘•Ð‰Õ¥±¡	Õ¥±‘½¹Ñ•áÐ½¹Ñ•áÐ¤€ôø(€€€€€}M•Ñ¥½¹MÕÉ™…”¡Ñ¥Ñ±”èÑ¥Ñ±”°¥½¸è¥½¸°¡¥±è¡¥±¤ì)ô()™¥¹…°±…ÍÌ}I•ÍÁ½¹Í¥Ù•¹Ñ¥ÑåÉ¥•áÑ•¹‘ÌMÑ…Ñ•±•ÍÍ]¥‘•Ðì(€½¹ÍÐ}I•ÍÁ½¹Í¥Ù•¹Ñ¥ÑåÉ¥¡ì(€€€É•ÅÕ¥É•Ñ¡¥Ì¹•¹Ñ¥Ñ¥•Ì°(€€€É•ÅÕ¥É•Ñ¡¥Ì¹½¹ÑÉ½±±•È°(€ô¤ì((€™¥¹…°1¥ÍÐñ!½µ•¹Ñ¥Ñäø•¹Ñ¥Ñ¥•Ìì(€™¥¹…°!½µ•½¹ÑÉ½±½¹ÑÉ½±±•È½¹ÑÉ½±±•Èì((€½Ù•ÉÉ¥‘”(€]¥‘•Ð‰Õ¥±¡	Õ¥±‘½¹Ñ•áÐ½¹Ñ•áÐ¤€ôø1…å½ÕÑ	Õ¥±‘•È (€€€‰Õ¥±‘•Èè€¡½¹Ñ•áÐ°½¹ÍÑÉ…¥¹ÑÌ¤ì(€€€€€™¥¹…°½±Õµ¹Ì€ô½¹ÍÑÉ…¥¹ÑÌ¹µ…á]¥‘Ñ €øô€ÜØÀ€ü€È€è€Äì(€€€€€™¥¹…°Ý¥‘Ñ €ô½±Õµ¹Ì€ôô€Ä(€€€€€€€€€€ü½¹ÍÑÉ…¥¹ÑÌ¹µ…á]¥‘Ñ (€€€€€€€€€€è€¡½¹ÍÑÉ…¥¹ÑÌ¹µ…á]¥‘Ñ €´AÉ½‘ÕÑMÁ…¥¹œ¹Í´¤€¼€Èì(€€€€€É•ÑÕÉ¸]É…À (€€€€€€€ÍÁ…¥¹œèAÉ½‘ÕÑMÁ…¥¹œ¹Í´°(€€€€€€€ÉÕ¹MÁ…¥¹œèAÉ½‘ÕÑMÁ…¥¹œ¹Í´°(€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€€€™½È€¡™¥¹…°•¹Ñ¥Ñä¥¸•¹Ñ¥Ñ¥•Ì¤(€€€€€€€€€€€M¥é•‘	½à (€€€€€€€€€€€€€Ý¥‘Ñ èÝ¥‘Ñ °(€€€€€€€€€€€€€¡¥±è¹Ñ¥Ñå…É¡•¹Ñ¥Ñäè•¹Ñ¥Ñä°½¹ÑÉ½±±•Èè½¹ÑÉ½±±•È¤°(€€€€€€€€€€€€¤°(€€€€€€€t°(€€€€€€¤ì(€€€ô°(€€¤ì)ô()ÕÑÕÉ”ñÙ½¥øÍ¡½ÝÅÕ…É¥Õµ•Ñ…¥±Ì (€	Õ¥±‘½¹Ñ•áÐ½¹Ñ•áÐ°(€!½µ•½¹ÑÉ½±½¹ÑÉ½±±•È½¹ÑÉ½±±•È°(¤…Íå¹Œì(€…Ý…¥Ð9…Ù¥…Ñ½È¹½˜¡½¹Ñ•áÐ¤¹ÁÕÍ ñÙ½¥ø (€€€5…Ñ•É¥…±A…•I½ÕÑ”ñÙ½¥ø (€€€€€‰Õ¥±‘•Èè€¡|¤€ôøÅÕ…É¥Õµ•Ñ…¥±ÍA…”¡½¹ÑÉ½±±•Èè½¹ÑÉ½±±•È¤°(€€€€¤°(€€¤ì)ô()™¥¹…°±…ÍÌÅÕ…É¥Õµ•Ñ…¥±ÍA…”•áÑ•¹‘ÌMÑ…Ñ•±•ÍÍ]¥‘•Ðì(€½¹ÍÐÅÕ…É¥Õµ•Ñ…¥±ÍA…”¡íÉ•ÅÕ¥É•Ñ¡¥Ì¹½¹ÑÉ½±±•È°ÍÕÁ•È¹­•åô¤ì((€™¥¹…°!½µ•½¹ÑÉ½±½¹ÑÉ½±±•È½¹ÑÉ½±±•Èì((€½Ù•ÉÉ¥‘”(€]¥‘•Ð‰Õ¥±¡	Õ¥±‘½¹Ñ•áÐ½¹Ñ•áÐ¤ì(€€€™¥¹…°ÍÑÉ¥¹Ì€ô!½µ•½¹ÑÉ½±MÑÉ¥¹Ì¹½˜¡½¹Ñ•áÐ¤ì(€€€™¥¹…°Í¹…ÁÍ¡½Ð€ô½¹ÑÉ½±±•È¹Í¹…ÁÍ¡½Ð„ì(€€€™¥¹…°½Ù•ÉÙ¥•Ü€ôÅÕ…É¥Õµ=Ù•ÉÙ¥•Ü¹™É½µM¹…ÁÍ¡½Ð¡Í¹…ÁÍ¡½Ð¤ì(€€€™¥¹…°É½ÕÁ•€ô€ñÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”°1¥ÍÐñ!½µ•¹Ñ¥Ñäøùíôì(€€€™½È€¡™¥¹…°•¹Ñ¥Ñä¥¸½Ù•ÉÙ¥•Ü¹•¹Ñ¥Ñ¥•Ì¤ì(€€€€€É½ÕÁ•(€€€€€€€€€€¹ÁÕÑ%™‰Í•¹Ð¡ÅÕ…É¥ÕµM•µ…¹Ñ¥Ì¹±…ÍÍ¥™ä¡•¹Ñ¥Ñä¤°€ ¤€ôø€ñ!½µ•¹Ñ¥Ñäùmt¤(€€€€€€€€€€¹…‘¡•¹Ñ¥Ñä¤ì(€€€ô(€€€½¹ÍÐ½É‘•È€ô€ñÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”ùl(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹Ñ•µÁ•É…ÑÕÉ”°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹Á °(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹•Œ°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹Ý…Ñ•É1•Ù•°°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹±•…¬°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹…±…É´°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹™É½¹Ñ1¥¡Ð°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹É•…É1¥¡Ð°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹™¥±Ñ•È°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹…•É…Ñ¥½¸°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹¡•…Ñ•È°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹Ñ…É•ÑQ•µÁ•É…ÑÕÉ”°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹Ñ½ÁUÀ°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹¼È°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹‘½Í¥¹œ°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹™••‘•È°(€€€€€ÅÕ…É¥ÕµM•µ…¹Ñ¥I½±”¹™¥ÉµÝ…É•UÁ‘…Ñ”°(€€€tì(€€€É•ÑÕÉ¸M…™™½± (€€€€€…ÁÁ	…ÈèÁÁ	…È¡Ñ¥Ñ±”èQ•áÐ¡ÍÑÉ¥¹Ì¹Ð …ÅÕ…É¥Õ´œ¤¤¤°(€€€€€‰½‘äè1¥ÍÑY¥•Ü (€€€€€€€Á…‘‘¥¹œè½¹ÍÐ‘•%¹Í•ÑÌ¹™É½µ1QI ÈÀ°€ÄÈ°€ÈÀ°€àÀ¤°(€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€€€ÅÕ…É¥Õµ…Í¡‰½…É‘…É (€€€€€€€€€€€Í¹…ÁÍ¡½ÐèÍ¹…ÁÍ¡½Ð°(€€€€€€€€€€€½¹ÑÉ½±±•Èè½¹ÑÉ½±±•È°(€€€€€€€€€€€¥¹Ñ•É…Ñ¥Ù”è™…±Í”°(€€€€€€€€€€¤°(€€€€€€€€€½¹ÍÐM¥é•‘	½à¡¡•¥¡ÐèAÉ½‘ÕÑMÁ…¥¹œ¹±œ¤°(€€€€€€€€€™½È€¡™¥¹…°É½±”¥¸½É‘•È¤(€€€€€€€€€€€™½È€¡™¥¹…°•¹Ñ¥Ñä¥¸É½ÕÁ•‘mÉ½±•t€üü½¹ÍÐ€ñ!½µ•¹Ñ¥Ñäùmt¤(€€€€€€€€€€€€€A…‘‘¥¹œ (€€€€€€€€€€€€€€€Á…‘‘¥¹œè½¹ÍÐ‘•%¹Í•ÑÌ¹½¹±ä¡‰½ÑÑ½´èAÉ½‘ÕÑMÁ…¥¹œ¹Í´¤°(€€€€€€€€€€€€€€€¡¥±è¹Ñ¥Ñå…É¡•¹Ñ¥Ñäè•¹Ñ¥Ñä°½¹ÑÉ½±±•Èè½¹ÑÉ½±±•È¤°(€€€€€€€€€€€€€€¤°(€€€€€€€€€½¹ÍÐM¥é•‘	½à¡¡•¥¡ÐèAÉ½‘ÕÑMÁ…¥¹œ¹µ¤°(€€€€€€€€€}M•Ñ¥½¹…É (€€€€€€€€€€€Ñ¥Ñ±”èÍÑÉ¥¹Ì¹Ð ‘¥…¹½ÍÑ¥Ìœ¤°(€€€€€€€€€€€¥½¸è%½¹Ì¹µ½¹¥Ñ½É}¡•…ÉÑ}É½Õ¹‘•°(€€€€€€€€€€€¡¥±è½±Õµ¸ (€€€€€€€€€€€€€¡¥±‘É•¸è€ñ]¥‘•Ðùl(€€€€€€€€€€€€€€€™½È€¡™¥¹…°‘•Ù¥”¥¸½Ù•ÉÙ¥•Ü¹‘•Ù¥•Ì¤(€€€€€€€€€€€€€€€€€1¥ÍÑQ¥±” (€€€€€€€€€€€€€€€€€€€½¹Ñ•¹ÑA…‘‘¥¹œè‘•%¹Í•ÑÌ¹é•É¼°(€€€€€€€€€€€€€€€€€€€±•…‘¥¹œè%½¸ (€€€€€€€€€€€€€€€€€€€€€‘•Ù¥”¹…Ù…¥±…‰±”(€€€€€€€€€€€€€€€€€€€€€€€€€€ü%½¹Ì¹¡•­}¥É±•}É½Õ¹‘•(€€€€€€€€€€€€€€€€€€€€€€€€€€è%½¹Ì¹•ÉÉ½É}É½Õ¹‘•°(€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€Ñ¥Ñ±”èQ•áÐ¡‘•Ù¥”¹¹…µ”¤°(€€€€€€€€€€€€€€€€€€€ÍÕ‰Ñ¥Ñ±”èQ•áÐ (€€€€€€€€€€€€€€€€€€€€€€œ‘í‘•Ù¥”¹µ½‘•±ôƒ
Ü€‘í‘•Ù¥”¹Í½™ÑÝ…É•Y•ÉÍ¥½¹ôœ°(€€€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€€€€€€€ÑÉ…¥±¥¹œèQ•áÐ¡ÍÑÉ¥¹Ì¹É•±…Ñ¥Ù•Q¥µ”¡‘•Ù¥”¹±…ÍÑM••¸¤¤°(€€€€€€€€€€€€€€€€€€¤°(€€€€€€€€€€€€€t°(€€€€€€€€€€€€¤°(€€€€€€€€€€¤°(€€€€€€€t°(€€€€€€¤°(€€€€¤ì(€ô)ô