import 'package:flutter/material.dart';

import '../../design_system.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../widgets.dart';
import 'charts_view.dart';
import 'logs_view.dart';

class InsightsCenterView extends StatefulWidget {
  const InsightsCenterView({
    super.key,
    required this.session,
    required this.runAction,
    required this.ensureAdmin,
  });

  final ControllerSession session;
  final RunControllerAction runAction;
  final Future<bool> Function() ensureAdmin;

  @override
  State<InsightsCenterView> createState() => _InsightsCenterViewState();
}

class _InsightsCenterViewState extends State<InsightsCenterView> {
  int _section = 0;
  Widget? _logs;

  @override
  void didUpdateWidget(InsightsCenterView oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (!identical(oldWidget.session, widget.session)) {
      _logs = null;
    }
  }

  void _select(int section) {
    if (section == _section) return;
    if (section == 1) {
      _logs ??= LogsView(
        session: widget.session,
        runAction: widget.runAction,
        ensureAdmin: widget.ensureAdmin,
      );
    }
    setState(() => _section = section);
  }

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
                  icon: Icons.show_chart_rounded,
                  label: 'Trendy i historia',
                ),
                ControllerSectionOption(
                  value: 1,
                  icon: Icons.receipt_long_rounded,
                  label: 'Dziennik zdarzeń',
                ),
              ],
              selected: _section,
              compactLabel: 'Sekcja historii',
              onSelected: _select,
            ),
          ),
        ),
        Divider(height: 1, color: colors.outlineVariant),
        Expanded(
          child: IndexedStack(
            index: _section,
            children: [
              ChartsView(session: widget.session),
              _logs ?? const SizedBox.shrink(),
            ],
          ),
        ),
      ],
    );
  }
}
