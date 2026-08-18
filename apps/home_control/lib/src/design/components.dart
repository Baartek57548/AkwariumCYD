import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:flutter/material.dart';

import 'app_theme.dart';

enum HomeStatusTone { neutral, info, success, warning, error }

/// Compact semantic status that never relies on color alone.
final class HomeStatusChip extends StatelessWidget {
  const HomeStatusChip({
    required this.icon,
    required this.label,
    this.tone = HomeStatusTone.neutral,
    super.key,
  });

  final IconData icon;
  final String label;
  final HomeStatusTone tone;

  @override
  Widget build(BuildContext context) {
    final colors = _statusColors(context, tone);
    return Semantics(
      label: label,
      child: ExcludeSemantics(
        child: Container(
          padding: const EdgeInsets.symmetric(
            horizontal: ProductSpacing.sm,
            vertical: ProductSpacing.xs,
          ),
          decoration: BoxDecoration(
            color: colors.background,
            borderRadius: BorderRadius.circular(ProductRadius.pill),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: <Widget>[
              Icon(icon, size: ProductIconSize.small, color: colors.foreground),
              const SizedBox(width: ProductSpacing.xxs),
              Flexible(
                child: Text(
                  label,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: Theme.of(context).textTheme.labelMedium?.copyWith(
                    color: colors.foreground,
                    fontWeight: FontWeight.w700,
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

final class HomeSectionHeader extends StatelessWidget {
  const HomeSectionHeader({
    required this.title,
    this.subtitle,
    this.trailing,
    super.key,
  });

  final String title;
  final String? subtitle;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) => Row(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: <Widget>[
      Expanded(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: <Widget>[
            Text(title, style: Theme.of(context).textTheme.titleLarge),
            if (subtitle case final subtitle?) ...<Widget>[
              const SizedBox(height: ProductSpacing.xxs),
              Text(subtitle, style: Theme.of(context).textTheme.bodySmall),
            ],
          ],
        ),
      ),
      if (trailing case final trailing?) ...<Widget>[
        const SizedBox(width: ProductSpacing.sm),
        trailing,
      ],
    ],
  );
}

final class HomeEmptyState extends StatelessWidget {
  const HomeEmptyState({
    required this.icon,
    required this.title,
    required this.message,
    this.actionLabel,
    this.onAction,
    this.secondaryActionLabel,
    this.onSecondaryAction,
    this.compact = false,
    super.key,
  }) : assert(
         (actionLabel == null) == (onAction == null),
         'Action label and callback must be provided together.',
       ),
       assert(
         (secondaryActionLabel == null) == (onSecondaryAction == null),
         'Secondary action label and callback must be provided together.',
       );

  final IconData icon;
  final String title;
  final String message;
  final String? actionLabel;
  final VoidCallback? onAction;
  final String? secondaryActionLabel;
  final VoidCallback? onSecondaryAction;
  final bool compact;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 440),
        child: Padding(
          padding: EdgeInsets.all(
            compact ? ProductSpacing.md : ProductSpacing.xl,
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: <Widget>[
              Container(
                width: compact ? 52 : 64,
                height: compact ? 52 : 64,
                decoration: BoxDecoration(
                  color: scheme.surfaceContainerHighest,
                  borderRadius: BorderRadius.circular(ProductRadius.card),
                ),
                child: Icon(
                  icon,
                  size: compact
                      ? ProductIconSize.medium
                      : ProductIconSize.large,
                  color: scheme.onSurfaceVariant,
                ),
              ),
              const SizedBox(height: ProductSpacing.md),
              Text(
                title,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.titleLarge,
              ),
              const SizedBox(height: ProductSpacing.xs),
              Text(
                message,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                  color: scheme.onSurfaceVariant,
                ),
              ),
              if (actionLabel case final actionLabel?) ...<Widget>[
                const SizedBox(height: ProductSpacing.lg),
                Wrap(
                  alignment: WrapAlignment.center,
                  spacing: ProductSpacing.sm,
                  runSpacing: ProductSpacing.xs,
                  children: <Widget>[
                    FilledButton(onPressed: onAction, child: Text(actionLabel)),
                    if (secondaryActionLabel case final secondaryLabel?)
                      TextButton(
                        onPressed: onSecondaryAction,
                        child: Text(secondaryLabel),
                      ),
                  ],
                ),
              ] else if (secondaryActionLabel
                  case final secondaryLabel?) ...<Widget>[
                const SizedBox(height: ProductSpacing.lg),
                TextButton(
                  onPressed: onSecondaryAction,
                  child: Text(secondaryLabel),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}

final class HomeLoadingState extends StatelessWidget {
  const HomeLoadingState({
    required this.title,
    required this.message,
    this.leading,
    super.key,
  });

  final String title;
  final String message;
  final Widget? leading;

  @override
  Widget build(BuildContext context) => Center(
    child: Semantics(
      liveRegion: true,
      label: '$title. $message',
      child: ExcludeSemantics(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 420),
          child: Padding(
            padding: const EdgeInsets.all(ProductSpacing.xl),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: <Widget>[
                if (leading case final leading?) ...<Widget>[
                  leading,
                  const SizedBox(height: ProductSpacing.lg),
                ],
                Text(
                  title,
                  textAlign: TextAlign.center,
                  style: Theme.of(context).textTheme.titleLarge,
                ),
                const SizedBox(height: ProductSpacing.xs),
                Text(message, textAlign: TextAlign.center),
                const SizedBox(height: ProductSpacing.lg),
                const CircularProgressIndicator(),
              ],
            ),
          ),
        ),
      ),
    ),
  );
}

final class HomeErrorState extends StatelessWidget {
  const HomeErrorState({
    required this.title,
    required this.message,
    required this.retryLabel,
    required this.onRetry,
    this.secondaryActionLabel,
    this.onSecondaryAction,
    super.key,
  }) : assert(
         (secondaryActionLabel == null) == (onSecondaryAction == null),
         'Secondary action label and callback must be provided together.',
       );

  final String title;
  final String message;
  final String retryLabel;
  final VoidCallback onRetry;
  final String? secondaryActionLabel;
  final VoidCallback? onSecondaryAction;

  @override
  Widget build(BuildContext context) => HomeEmptyState(
    icon: Icons.cloud_off_rounded,
    title: title,
    message: message,
    actionLabel: retryLabel,
    onAction: onRetry,
    secondaryActionLabel: secondaryActionLabel,
    onSecondaryAction: onSecondaryAction,
    key: key,
  );
}

final class HomeMetricCard extends StatelessWidget {
  const HomeMetricCard({
    required this.icon,
    required this.value,
    required this.label,
    this.tone = HomeStatusTone.neutral,
    super.key,
  });

  final IconData icon;
  final String value;
  final String label;
  final HomeStatusTone tone;

  @override
  Widget build(BuildContext context) {
    final colors = _statusColors(context, tone);
    return Container(
      constraints: const BoxConstraints(minWidth: 112, minHeight: 96),
      padding: const EdgeInsets.all(ProductSpacing.md),
      decoration: BoxDecoration(
        color: colors.background,
        borderRadius: BorderRadius.circular(ProductRadius.control),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisAlignment: MainAxisAlignment.center,
        children: <Widget>[
          Icon(icon, size: ProductIconSize.small, color: colors.foreground),
          const SizedBox(height: ProductSpacing.xs),
          Text(
            value,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: Theme.of(context).textTheme.titleLarge?.copyWith(
              color: colors.foreground,
              fontWeight: FontWeight.w800,
            ),
          ),
          Text(
            label,
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
            style: Theme.of(
              context,
            ).textTheme.labelMedium?.copyWith(color: colors.foreground),
          ),
        ],
      ),
    );
  }
}

final class HomeQuickAction extends StatelessWidget {
  const HomeQuickAction({
    required this.icon,
    required this.label,
    required this.description,
    required this.onPressed,
    super.key,
  });

  final IconData icon;
  final String label;
  final String description;
  final VoidCallback? onPressed;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Card(
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onPressed,
        child: ConstrainedBox(
          constraints: const BoxConstraints(minHeight: 88),
          child: Padding(
            padding: const EdgeInsets.all(ProductSpacing.md),
            child: Row(
              children: <Widget>[
                Container(
                  width: ProductLayout.minimumTouchTarget,
                  height: ProductLayout.minimumTouchTarget,
                  decoration: BoxDecoration(
                    color: scheme.primaryContainer,
                    borderRadius: BorderRadius.circular(ProductRadius.control),
                  ),
                  child: Icon(icon, color: scheme.onPrimaryContainer),
                ),
                const SizedBox(width: ProductSpacing.sm),
                Expanded(
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: <Widget>[
                      Text(
                        label,
                        style: Theme.of(context).textTheme.titleMedium,
                      ),
                      Text(
                        description,
                        maxLines: 2,
                        overflow: TextOverflow.ellipsis,
                        style: Theme.of(context).textTheme.bodySmall,
                      ),
                    ],
                  ),
                ),
                const SizedBox(width: ProductSpacing.xs),
                Icon(
                  Icons.arrow_forward_rounded,
                  color: onPressed == null
                      ? scheme.onSurface.withValues(alpha: 0.38)
                      : scheme.primary,
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

final class HomeToggle extends StatelessWidget {
  const HomeToggle({
    required this.label,
    required this.value,
    required this.onChanged,
    this.description,
    this.icon,
    super.key,
  });

  final String label;
  final String? description;
  final IconData? icon;
  final bool value;
  final ValueChanged<bool>? onChanged;

  @override
  Widget build(BuildContext context) => SwitchListTile(
    contentPadding: EdgeInsets.zero,
    secondary: icon == null ? null : Icon(icon),
    title: Text(label),
    subtitle: description == null ? null : Text(description!),
    value: value,
    onChanged: onChanged,
  );
}

final class HomeSlider extends StatelessWidget {
  const HomeSlider({
    required this.label,
    required this.value,
    required this.minimum,
    required this.maximum,
    required this.onChanged,
    this.onChangeEnd,
    this.divisions,
    this.valueLabel,
    super.key,
  }) : assert(maximum > minimum),
       assert(value >= minimum && value <= maximum);

  final String label;
  final double value;
  final double minimum;
  final double maximum;
  final int? divisions;
  final String? valueLabel;
  final ValueChanged<double>? onChanged;
  final ValueChanged<double>? onChangeEnd;

  @override
  Widget build(BuildContext context) => Semantics(
    slider: true,
    label: label,
    value: valueLabel ?? value.toStringAsFixed(1),
    enabled: onChanged != null,
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: <Widget>[
        Row(
          children: <Widget>[
            Expanded(child: Text(label)),
            if (valueLabel case final valueLabel?)
              Text(valueLabel, style: Theme.of(context).textTheme.labelLarge),
          ],
        ),
        Slider(
          value: value,
          min: minimum,
          max: maximum,
          divisions: divisions,
          label: valueLabel,
          onChanged: onChanged,
          onChangeEnd: onChangeEnd,
        ),
      ],
    ),
  );
}

final class HomeBottomSheet extends StatelessWidget {
  const HomeBottomSheet({
    required this.title,
    required this.child,
    this.actions = const <Widget>[],
    super.key,
  });

  final String title;
  final Widget child;
  final List<Widget> actions;

  @override
  Widget build(BuildContext context) => SafeArea(
    top: false,
    child: Padding(
      padding: EdgeInsets.fromLTRB(
        ProductSpacing.lg,
        ProductSpacing.xs,
        ProductSpacing.lg,
        ProductSpacing.lg + MediaQuery.viewInsetsOf(context).bottom,
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: <Widget>[
          Text(title, style: Theme.of(context).textTheme.headlineSmall),
          const SizedBox(height: ProductSpacing.md),
          Flexible(child: child),
          if (actions.isNotEmpty) ...<Widget>[
            const SizedBox(height: ProductSpacing.lg),
            Wrap(
              alignment: WrapAlignment.end,
              spacing: ProductSpacing.sm,
              runSpacing: ProductSpacing.xs,
              children: actions,
            ),
          ],
        ],
      ),
    ),
  );
}

final class HomeModal extends StatelessWidget {
  const HomeModal({
    required this.title,
    required this.child,
    this.icon,
    this.actions = const <Widget>[],
    super.key,
  });

  final IconData? icon;
  final String title;
  final Widget child;
  final List<Widget> actions;

  @override
  Widget build(BuildContext context) => Dialog(
    child: ConstrainedBox(
      constraints: const BoxConstraints(maxWidth: 560),
      child: Padding(
        padding: const EdgeInsets.all(ProductSpacing.lg),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: <Widget>[
            if (icon case final icon?) ...<Widget>[
              Icon(icon, size: ProductIconSize.large),
              const SizedBox(height: ProductSpacing.sm),
            ],
            Text(title, style: Theme.of(context).textTheme.headlineSmall),
            const SizedBox(height: ProductSpacing.md),
            Flexible(child: child),
            if (actions.isNotEmpty) ...<Widget>[
              const SizedBox(height: ProductSpacing.lg),
              Wrap(
                alignment: WrapAlignment.end,
                spacing: ProductSpacing.sm,
                runSpacing: ProductSpacing.xs,
                children: actions,
              ),
            ],
          ],
        ),
      ),
    ),
  );
}

final class HomeAlertDialog extends StatelessWidget {
  const HomeAlertDialog({
    required this.icon,
    required this.title,
    required this.message,
    required this.actions,
    super.key,
  });

  final IconData icon;
  final String title;
  final String message;
  final List<Widget> actions;

  @override
  Widget build(BuildContext context) => AlertDialog(
    icon: Icon(icon),
    title: Text(title),
    content: Text(message),
    actions: actions,
  );
}

({Color background, Color foreground}) _statusColors(
  BuildContext context,
  HomeStatusTone tone,
) {
  final theme = Theme.of(context);
  final scheme = theme.colorScheme;
  final semantic = theme.semanticColors;
  return switch (tone) {
    HomeStatusTone.neutral => (
      background: scheme.surfaceContainerHighest,
      foreground: scheme.onSurfaceVariant,
    ),
    HomeStatusTone.info => (
      background: semantic.infoContainer,
      foreground: semantic.onInfoContainer,
    ),
    HomeStatusTone.success => (
      background: semantic.successContainer,
      foreground: semantic.onSuccessContainer,
    ),
    HomeStatusTone.warning => (
      background: semantic.warningContainer,
      foreground: semantic.onWarningContainer,
    ),
    HomeStatusTone.error => (
      background: scheme.errorContainer,
      foreground: scheme.onErrorContainer,
    ),
  };
}
