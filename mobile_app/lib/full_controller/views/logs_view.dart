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
  }

  @override
  void dispose() {
    tabController.dispose();
    super.dispose();
  }

  Future<void> _load() async {
    if (!widget.session.canIssueCommands) {
      if (mounted) {
        setState(() => message = widget.session.commandBlockReason);
      }
      return;
    }
    final authorized = await widget.ensureAdmin();
    if (!mounted || !authorized) return;
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
    final logsAvailable = logs.isNotEmpty;
    final canSynchronize = widget.session.canIssueCommands;
    return ControllerPageBody(
      children: [
        SectionHeader(
          title: 'Dziennik zdarzeń',
          description: logsAvailable
              ? 'Ostatnio pobrane informacje i ostrzeżenia sterownika.'
              : 'Dziennik pobierzesz na żądanie po odblokowaniu dostępu.',
          trailing: OutlinedButton.icon(
            key: const Key('load-controller-logs'),
            onPressed: loading || !canSynchronize ? null : _load,
            icon: loading
                ? const SizedBox.square(
                    dimension: 18,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : Icon(
                    logsAvailable
                        ? Icons.sync_rounded
                        : Icons.lock_open_rounded,
                  ),
            label: Text(
              logsAvailable
                  ? 'Synchronizuj'
                  : widget.session.isAdmin
                  ? 'Pobierz dziennik'
                  : 'Odblokuj i pobierz',
            ),
          ),
        ),
        if (!logsAvailable) ...[
          StatusBanner(
            icon: Icons.history_toggle_off_rounded,
            title: 'Logi nie zostały zapisane lokalnie',
            message: canSynchronize
                ? 'Użyj przycisku „Odblokuj i pobierz”, aby świadomie '
                      'zalogować administratora i zsynchronizować dziennik.'
                : 'Połącz sterownik, aby pobrać dziennik. Brak wpisów na '
                      'ekranie nie oznacza braku zdarzeń w urządzeniu.',
            isError: false,
          ),
          const SizedBox(height: 14),
        ],
        if (logsAvailable) ...[
          Card(
            clipBehavior: Clip.antiAlias,
            child: Column(
              children: [
                TabBar(
                  controller: tabController,
                  tabs: [
                    Tab(text: 'Informacje (${normal.length})'),
                    Tab(text: 'Ważne (${critical.length})'),
                  ],
                ),
                SizedBox(
                  height: 520,
                  child: TabBarView(
                    controller: tabController,
                    children: [
                      _LogList(
                        entries: normal,
                        critical: false,
                        dataAvailable: true,
                      ),
                      _LogList(
                        entries: critical,
                        critical: true,
                        dataAvailable: true,
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 12),
          Card(
            clipBehavior: Clip.antiAlias,
            child: ExpansionTile(
              key: const PageStorageKey<String>('log-tools'),
              leading: const Icon(Icons.build_outlined),
              title: const Text(
                'Narzędzia dziennika',
                style: TextStyle(fontWeight: FontWeight.w700),
              ),
              subtitle: const Text('Eksport i porządkowanie wpisów'),
              childrenPadding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
              children: [
                Align(
                  alignment: Alignment.centerLeft,
                  child: Wrap(
                    spacing: 10,
                    runSpacing: 10,
                    children: [
                      OutlinedButton.icon(
                        onPressed: _export,
                        icon: const Icon(Icons.download_rounded),
                        label: const Text('Eksportuj TXT'),
                      ),
                      FilledButton.tonalIcon(
                        onPressed: !canSynchronize || critical.isEmpty
                            ? null
                            : _clearCritical,
                        icon: const Icon(Icons.delete_sweep_outlined),
                        label: const Text('Wyczyść ważne logi'),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ],
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
  const _LogList({
    required this.entries,
    required this.critical,
    required this.dataAvailable,
  });

  final List<dynamic> entries;
  final bool critical;
  final bool dataAvailable;

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
            Text(
              !dataAvailable
                  ? 'Logi nie zostały zapisane'
                  : critical
                  ? 'Brak ważnych wpisów'
                  : 'Brak wpisów',
            ),
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
