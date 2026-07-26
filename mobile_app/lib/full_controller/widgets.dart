import 'package:flutter/material.dart';

import '../design_system.dart';

class ControllerPageBody extends StatelessWidget {
  const ControllerPageBody({
    super.key,
    required this.children,
    this.maxWidth = 1180,
    this.padding = const EdgeInsets.fromLTRB(16, 12, 16, 32),
    this.onRefresh,
  });

  final List<Widget> children;
  final double maxWidth;
  final EdgeInsets padding;
  final Future<void> Function()? onRefresh;

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      top: false,
      child: RefreshIndicator(
        onRefresh: onRefresh ?? () async {},
        notificationPredicate: onRefresh == null
            ? (_) => false
            : (notification) => notification.depth == 0,
        child: ListView(
          physics: const AlwaysScrollableScrollPhysics(),
          keyboardDismissBehavior: ScrollViewKeyboardDismissBehavior.onDrag,
          padding: padding,
          children: [
            Center(
              child: ConstrainedBox(
                constraints: BoxConstraints(maxWidth: maxWidth),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: children,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class SectionHeader extends StatelessWidget {
  const SectionHeader({
    super.key,
    required this.title,
    this.description,
    this.trailing,
  });

  final String title;
  final String? description;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(top: 10, bottom: 10),
      child: LayoutBuilder(
        builder: (context, constraints) {
          final titleBlock = Semantics(
            header: true,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                    fontWeight: FontWeight.w800,
                  ),
                ),
                if (description != null)
                  Padding(
                    padding: const EdgeInsets.only(top: 3),
                    child: Text(
                      description!,
                      style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                        color: Theme.of(context).colorScheme.onSurfaceVariant,
                      ),
                    ),
                  ),
              ],
            ),
          );
          if (trailing == null) return titleBlock;

          final textScale = MediaQuery.textScalerOf(context).scale(1);
          if (constraints.maxWidth < 420 || textScale > 1.3) {
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                titleBlock,
                const SizedBox(height: 10),
                Align(alignment: Alignment.centerRight, child: trailing),
              ],
            );
          }
          return Row(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              Expanded(child: titleBlock),
              const SizedBox(width: 12),
              trailing!,
            ],
          );
        },
      ),
    );
  }
}

@immutable
class ControllerSectionOption<T> {
  const ControllerSectionOption({
    required this.value,
    required this.icon,
    required this.label,
  });

  final T value;
  final IconData icon;
  final String label;
}

class ControllerSectionSwitcher<T> extends StatelessWidget {
  const ControllerSectionSwitcher({
    super.key,
    required this.options,
    required this.selected,
    required this.onSelected,
    this.compactLabel = 'Widok',
  });

  final List<ControllerSectionOption<T>> options;
  final T selected;
  final ValueChanged<T> onSelected;
  final String compactLabel;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final textScale = MediaQuery.textScalerOf(context).scale(1);
        final compact = constraints.maxWidth < 520 || textScale > 1.3;
        if (compact) {
          return DropdownButtonFormField<T>(
            initialValue: selected,
            isExpanded: true,
            decoration: InputDecoration(labelText: compactLabel),
            items: [
              for (final option in options)
                DropdownMenuItem<T>(
                  value: option.value,
                  child: Row(
                    children: [
                      Icon(option.icon, size: 20),
                      const SizedBox(width: AquaSpacing.xs),
                      Expanded(
                        child: Text(
                          option.label,
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                        ),
                      ),
                    ],
                  ),
                ),
            ],
            onChanged: (value) {
              if (value != null && value != selected) onSelected(value);
            },
          );
        }

        return SizedBox(
          width: double.infinity,
          child: SegmentedButton<T>(
            segments: [
              for (final option in options)
                ButtonSegment<T>(
                  value: option.value,
                  icon: Icon(option.icon),
                  label: Text(option.label),
                ),
            ],
            selected: {selected},
            showSelectedIcon: false,
            onSelectionChanged: (selection) => onSelected(selection.first),
          ),
        );
      },
    );
  }
}

class MetricTile extends StatelessWidget {
  const MetricTile({
    super.key,
    required this.icon,
    required this.label,
    required this.value,
    required this.detail,
    this.tone,
  });

  final IconData icon;
  final String label;
  final String value;
  final String detail;
  final Color? tone;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final color = tone ?? colors.primary;
    return MergeSemantics(
      child: Card(
        margin: EdgeInsets.zero,
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(icon, color: color, size: 21),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      label,
                      style: const TextStyle(fontWeight: FontWeight.w600),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 12),
              Text(
                value,
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                  fontWeight: FontWeight.w900,
                ),
              ),
              const SizedBox(height: 2),
              Text(
                detail,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                style: Theme.of(
                  context,
                ).textTheme.bodySmall?.copyWith(color: colors.onSurfaceVariant),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class ResponsiveGrid extends StatelessWidget {
  const ResponsiveGrid({
    super.key,
    required this.children,
    this.minimumChildWidth = 220,
    this.spacing = 12,
  });

  final List<Widget> children;
  final double minimumChildWidth;
  final double spacing;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final textScale = MediaQuery.textScalerOf(context).scale(1);
        final accessibilityGrowth = (textScale - 1).clamp(0.0, 2.0);
        // Większa efektywna szerokość kafelka zapobiega ściskaniu treści,
        // gdy użytkownik korzysta z systemowego powiększenia tekstu.
        final effectiveMinimumWidth =
            minimumChildWidth * (1 + accessibilityGrowth * 0.25);
        final columns =
            ((constraints.maxWidth + spacing) /
                    (effectiveMinimumWidth + spacing))
                .floor()
                .clamp(1, 4);
        final width =
            (constraints.maxWidth - spacing * (columns - 1)) / columns;
        return Wrap(
          spacing: spacing,
          runSpacing: spacing,
          children: [
            for (final child in children) SizedBox(width: width, child: child),
          ],
        );
      },
    );
  }
}

class InfoRow extends StatelessWidget {
  const InfoRow({
    super.key,
    required this.label,
    required this.value,
    this.icon,
  });

  final String label;
  final String value;
  final IconData? icon;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final labelWidget = Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        if (icon != null) ...[
          Icon(icon, size: 18, color: colors.primary),
          const SizedBox(width: 8),
        ],
        Flexible(child: Text(label)),
      ],
    );
    final valueWidget = Text(
      value,
      textAlign: TextAlign.end,
      style: const TextStyle(fontWeight: FontWeight.w700),
    );
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
      child: LayoutBuilder(
        builder: (context, constraints) {
          final textScale = MediaQuery.textScalerOf(context).scale(1);
          if (constraints.maxWidth < 340 || textScale > 1.4) {
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                labelWidget,
                const SizedBox(height: 4),
                Align(alignment: Alignment.centerRight, child: valueWidget),
              ],
            );
          }
          return Row(
            children: [
              Expanded(child: labelWidget),
              const SizedBox(width: 12),
              Flexible(child: valueWidget),
            ],
          );
        },
      ),
    );
  }
}

class StatusBanner extends StatelessWidget {
  const StatusBanner({
    super.key,
    required this.icon,
    required this.title,
    required this.message,
    required this.isError,
  });

  final IconData icon;
  final String title;
  final String message;
  final bool isError;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final foreground = isError
        ? colors.onErrorContainer
        : colors.onPrimaryContainer;
    return Semantics(
      container: true,
      liveRegion: true,
      label: '$title. $message',
      child: ExcludeSemantics(
        child: Container(
          padding: const EdgeInsets.all(16),
          decoration: BoxDecoration(
            color: isError ? colors.errorContainer : colors.primaryContainer,
            borderRadius: BorderRadius.circular(AquaRadius.card),
          ),
          child: Row(
            children: [
              Icon(icon, size: 30, color: foreground),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      title,
                      style: TextStyle(
                        color: foreground,
                        fontWeight: FontWeight.w800,
                      ),
                    ),
                    Text(message, style: TextStyle(color: foreground)),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

enum _StatePanelKind { loading, empty, error }

class StatePanel extends StatelessWidget {
  const StatePanel.loading({
    super.key,
    required this.title,
    required this.message,
  }) : _kind = _StatePanelKind.loading,
       icon = null,
       actionLabel = null,
       onAction = null;

  const StatePanel.empty({
    super.key,
    required this.title,
    required this.message,
    this.icon,
    this.actionLabel,
    this.onAction,
  }) : _kind = _StatePanelKind.empty;

  const StatePanel.error({
    super.key,
    required this.title,
    required this.message,
    this.icon,
    this.actionLabel,
    this.onAction,
  }) : _kind = _StatePanelKind.error;

  final _StatePanelKind _kind;
  final String title;
  final String message;
  final IconData? icon;
  final String? actionLabel;
  final VoidCallback? onAction;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final isError = _kind == _StatePanelKind.error;
    final semanticsLabel = '$title. $message';
    final action = actionLabel;
    return Semantics(
      container: true,
      liveRegion: _kind != _StatePanelKind.empty,
      child: Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(24),
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 460),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Semantics(
                  label: semanticsLabel,
                  child: ExcludeSemantics(
                    child: Column(
                      children: [
                        if (_kind == _StatePanelKind.loading)
                          const SizedBox.square(
                            dimension: 48,
                            child: CircularProgressIndicator(),
                          )
                        else
                          Icon(
                            icon ??
                                (isError
                                    ? Icons.error_outline_rounded
                                    : Icons.inbox_outlined),
                            size: 64,
                            color: isError ? colors.error : colors.primary,
                          ),
                        const SizedBox(height: 18),
                        Text(
                          title,
                          textAlign: TextAlign.center,
                          style: Theme.of(context).textTheme.titleLarge
                              ?.copyWith(fontWeight: FontWeight.w800),
                        ),
                        const SizedBox(height: 8),
                        Text(
                          message,
                          textAlign: TextAlign.center,
                          style: Theme.of(context).textTheme.bodyMedium
                              ?.copyWith(color: colors.onSurfaceVariant),
                        ),
                      ],
                    ),
                  ),
                ),
                if (action != null && onAction != null) ...[
                  const SizedBox(height: 20),
                  FilledButton.icon(
                    onPressed: onAction,
                    icon: const Icon(Icons.refresh_rounded),
                    label: Text(action),
                  ),
                ],
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class SaveButton extends StatelessWidget {
  const SaveButton({
    super.key,
    required this.onPressed,
    required this.label,
    this.busy = false,
    this.icon = Icons.save_rounded,
  });

  final VoidCallback? onPressed;
  final String label;
  final bool busy;
  final IconData icon;

  @override
  Widget build(BuildContext context) {
    return FilledButton.icon(
      onPressed: busy ? null : onPressed,
      icon: busy
          ? const SizedBox.square(
              dimension: 18,
              child: CircularProgressIndicator(
                strokeWidth: 2,
                semanticsLabel: 'Zapisywanie zmian',
              ),
            )
          : Icon(icon),
      label: Text(label),
    );
  }
}

class LabeledSwitch extends StatelessWidget {
  const LabeledSwitch({
    super.key,
    required this.label,
    required this.value,
    required this.onChanged,
    this.subtitle,
  });

  final String label;
  final String? subtitle;
  final bool value;
  final ValueChanged<bool>? onChanged;

  @override
  Widget build(BuildContext context) {
    return SwitchListTile(
      contentPadding: EdgeInsets.zero,
      title: Text(label, style: const TextStyle(fontWeight: FontWeight.w700)),
      subtitle: subtitle == null ? null : Text(subtitle!),
      value: value,
      onChanged: onChanged,
    );
  }
}

String? validateNumber(
  String? value, {
  required String label,
  required double minimum,
  required double maximum,
}) {
  final parsed = double.tryParse((value ?? '').replaceAll(',', '.'));
  if (parsed == null) return '$label musi być liczbą.';
  if (parsed < minimum || parsed > maximum) {
    return '$label musi być w zakresie $minimum–$maximum.';
  }
  return null;
}
