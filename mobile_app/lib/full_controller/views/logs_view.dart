import 'dart:convert';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../controller_api.dart';
import '../controller_session.dart';
import '../controller_shell.dart';
import '../data_access.dart';
import '../widgets.dart';

class LogsView extends StatefulWidget {
  const LogsView({
    super.key,
    required this.session,
    required this.runAction,
    required this.ensureAdmin,
  });

  final ControllerSession session;
  final RunControllerAction runAction;
  final Future<bool> Function() ensureAdmin;

  @override
  State<LogsView> createState() => _LogsViewState();
}

class _LogsViewState extends State<LogsView>
    with SingleTickerProviderStateMixin {
  late final TabController tabController;
  bool loading = false;
  String? message;

  @override
  void initState() {
    super.initState();
    tabController = TabController(length: 2, vsync: this);
    WidgetsBinding.instance.addPostFrameCallback((_) => _load());
  }

  @override
  void dispose() {
    tabController.dispose();
    super.dispose();
  }

  Future<void> _load() async {
    if (!await widget.ensureAdmin()) return;
    setState(() => loading = true);
    try {
      await widget.session.loadLogs();
      if (mounted) setState(() => message = 'Logi zostały zsynchronizowane.');
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => message = error.message);
    } finally {
      if (mounted) setState(() => loading = false);
    }
  }

  Future<void> _clearCritical() async {
    try {
      await widget.runAction(
        'clear_critical_logs',
        confirmation: 'Wyczyścić wszystkie ważne logi zapisane w sterowniku?',
        refreshAfter: false,
      );
      await widget.session.loadLogs();
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => message = error.message);
    }
  }

  Future<void> _export() async {
    final logs = widget.session.logsData;
    final buffer = StringBuffer();
    for (final kind in const ['normal', 'critical']) {
      buffer.writeln(
        kind == 'normal' ? '=== LOGI NORMALNE ===' : '=== LOGI WAŻNE ===',
      );
      for (final item in logs.list(kind)) {
        final entry = jsonMap(item);
        buffer.writeln(
          '${_formatEpoch(entry.integer('ts'))} ${entry.text('level').toUpperCase()} ${entry.text('message')}',
        );
      }
      buffer.writeln();
    }
    final path = await FilePicker.saveFile(
      dialogTitle: 'Eksportuj logi cydAkwarium',
      fileName: 'cydAkwarium-logs.txt',
      bytes: utf8.encode(buffer.toString()),
    );
    if (mounted) {
      setState(
        () => message = path == null
            ? 'Eksport anulowany.'
            : 'Logi zostały zapisane.',
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final logs = widget.session.logsData;
    final normal = logs.list('normal');
    final critical = logs.list('critical');
    return ControllerPageBody(
      children: [
        SectionHeader(
          title: 'Dziennik zdarzeń',
          description: 'Logi normalne i krytyczne z endpointu /api/logs.',
          trailing: IconButton(
            onPressed: loading ? null : _load,
            icon: loading
                ? const SizedBox.square(
                    dimension: 20,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : const Icon(Icons.refresh_rounded),
            tooltip: 'Odśwież logi',
          ),
        ),
        ResponsiveGrid(
          children: [
            MetricTile(
              icon: Icons.info_outline_rounded,
              label: 'Normalne',
              value: '${normal.length}',
              detail: 'Informacje operacyjne',
            ),
            MetricTile(
              icon: Icons.warning_amber_rounded,
              label: 'Ważne',
              value: '${critical.length}',
              detail: critical.isEmpty
                  ? 'Brak aktywnych wpisów'
                  : 'Wymagają sprawdzenia',
              tone: critical.isEmpty
                  ? null
                  : Theme.of(context).colorScheme.error,
            ),
          ],
        ),
        const SizedBox(height: 14),
        Card(
          clipBehavior: Clip.antiAlias,
          child: Column(
            children: [
              TabBar(
                controller: tabController,
                tabs: [
                  Tab(text: 'Normalne (${normal.length})'),
                  Tab(text: 'Ważne (${critical.length})'),
                ],
              ),
              SizedBox(
                height: 520,
                child: TabBarView(
                  controller: tabController,
                  children: [
                    _LogList(entries: normal, critical: false),
                    _LogList(entries: critical, critical: true),
                  ],
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 12),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            OutlinedButton.icon(
              onPressed: _export,
              icon: const Icon(Icons.download_rounded),
              label: const Text('Eksportuj TXT'),
            ),
            FilledButton.tonalIcon(
              onPressed: critical.isEmpty ? null : _clearCritical,
              icon: const Icon(Icons.delete_sweep_outlined),
              label: const Text('Wyczyść ważne logi'),
            ),
          ],
        ),
        if (message != null)
          Padding(
            padding: const EdgeInsets.only(top: 10),
            child: Text(message!),
          ),
      ],
    );
  }

  String _formatEpoch(int epoch) {
    final date = DateTime.fromMillisecondsSinceEpoch(epoch * 1000).toLocal();
    return '${date.year}-${date.month.toString().padLeft(2, '0')}-${date.day.toString().padLeft(2, '0')} '
        '${date.hour.toString().padLeft(2, '0')}:${date.minute.toString().padLeft(2, '0')}:${date.second.toString().padLeft(2, '0')}';
  }
}

class _LogList extends StatelessWidget {
  const _LogList({required this.entries, required this.critical});

  final List<dynamic> entries;
  final bool critical;

  @override
  Widget build(BuildContext context) {
    if (entries.isEmpty) {
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              critical ? Icons.verified_rounded : Icons.inbox_outlined,
              size: 52,
            ),
            const SizedBox(height: 10),
            Text(critical ? 'Brak ważnych wpisów' : 'Brak wpisów'),
          ],
        ),
      );
    }
    return ListView.separated(
      itemCount: entries.length,
      separatorBuilder: (_, _) => const Divider(height: 1),
      itemBuilder: (context, index) {
        final entry = jsonMap(entries[index]);
        final epoch = entry.integer('ts');
        final date = DateTime.fromMillisecondsSinceEpoch(
          epoch * 1000,
        ).toLocal();
        return ListTile(
          leading: Icon(
            critical ? Icons.error_outline_rounded : Icons.info_outline_rounded,
            color: critical ? Theme.of(context).colorScheme.error : null,
          ),
          title: Text(entry.text('message', 'Brak opisu')),
          subtitle: Text(
            '${entry.text('code', 'info')} · '
            '${date.day.toString().padLeft(2, '0')}.${date.month.toString().padLeft(2, '0')} '
            '${date.hour.toString().padLeft(2, '0')}:${date.minute.toString().padLeft(2, '0')}:${date.second.toString().padLeft(2, '0')}',
          ),
        );
      },
    );
  }
}
