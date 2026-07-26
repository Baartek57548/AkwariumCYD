import 'package:flutter/material.dart';

import '../../design_system.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../widgets.dart';
import 'automation_view.dart';
import 'schedules_view.dart';

class AutomationCenterView extends StatefulWidget {
  const AutomationCenterView({
    super.key,
    required this.session,
    required this.runAction,
  });

  final ControllerSession session;
  final RunControllerAction runAction;

  @override
  State<AutomationCenterView> createState() => _AutomationCenterViewState();
}

class _AutomationCenterViewState extends State<AutomationCenterView> {
  int _section = 0;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Column(
      children: [
        Material(
          color: colors.surfaceContainerLow,
          child: Padding(
            padding: const EdgeInsets.fromLTRB(
              AquaSpacing.md,
              AquaSpacing.sm,
              AquaSpacing.md,
              AquaSpacing.sm,
            ),
            child: ControllerSectionSwitcher<int>(
              options: const [
                ControllerSectionOption(
                  value: 0,
                  icon: Icons.calendar_month_rounded,
                  label: 'Plan dobowy',
                ),
                ControllerSectionOption(
                  value: 1,
                  icon: Icons.auto_mode_rounded,
                  label: 'Reguły i bezpieczeństwo',
                ),
              ],
              selected: _section,
              compactLabel: 'Sekcja automatyki',
              onSelected: (section) => setState(() => _section = section),
            ),
          ),
        ),
        Divider(height: 1, color: colors.outlineVariant),
        Expanded(
          child: IndexedStack(
            index: _section,
            children: [
              SchedulesView(
                session: widget.session,
                runAction: widget.runAction,
              ),
              AutomationView(
                session: widget.session,
                runAction: widget.runAction,
              ),
            ],
          ),
        ),
      ],
    );
  }
}
