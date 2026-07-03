import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../controller_api.dart';
import '../controller_session.dart';
import '../data_access.dart';
import '../widgets.dart';

class ChartsView extends StatefulWidget {
  const ChartsView({super.key, required this.session});

  final ControllerSession session;

  @override
  State<ChartsView> createState() => _ChartsViewState();
}

class _ChartsViewState extends State<ChartsView> {
  int rangeHours = 3;
  bool loadingArchives = false;
  String? message;

  @override
  void initState() {
    super.initState();
    unawaited(widget.session.refresh(includeHistory: true));
  }

  Future<void> _exportCsv() async {
    try {
      final bytes = await widget.session.downloadCurrentHistory();
      final path = await FilePicker.saveFile(
        dialogTitle: 'Zapisz historię cydAkwarium',
        fileName: 'cydAkwarium-history.csv',
        bytes: bytes,
      );
      if (mounted) {
        setState(
          () => message = path == null
              ? 'Eksport anulowany.'
              : 'Historia została zapisana.',
        );
      }
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => message = error.message);
    }
  }

  Future<void> _loadArchives() async {
    setState(() => loadingArchives = true);
    try {
      await widget.session.loadHistoryFiles();
      if (mounted) {
        setState(() => message = 'Lista archiwów została odświeżona.');
      }
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => message = error.message);
    } finally {
      if (mounted) setState(() => loadingArchives = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final status = widget.session.status;
    final temperature = status.section('temperature');
    final sensors = status.section('sensors');
    final rawSamples = temperature.list('history');
    final cutoff =
        DateTime.now().millisecondsSinceEpoch ~/ 1000 - rangeHours * 3600;
    final samples = rawSamples
        .map(jsonMap)
        .where((item) => item.integer('epoch') >= cutoff)
        .map(
          (item) => _ChartSample(item.integer('epoch'), item.number('value')),
        )
        .toList(growable: false);
    final values = samples.map((item) => item.value).toList(growable: false);
    final minimum = values.isEmpty
        ? 0.0
        : values.reduce((a, b) => a < b ? a : b);
    final maximum = values.isEmpty
        ? 0.0
        : values.reduce((a, b) => a > b ? a : b);
    final average = values.isEmpty
        ? 0.0
        : values.reduce((a, b) => a + b) / values.length;

    return ControllerPageBody(
      children: [
        SectionHeader(
          title: 'Historia pomiarów',
          description:
              'Wykres wykorzystuje ten sam bufor /api/status?history=1 co panel WWW.',
          trailing: IconButton(
            onPressed: () => widget.session.refresh(includeHistory: true),
            icon: const Icon(Icons.refresh_rounded),
            tooltip: 'Odśwież historię',
          ),
        ),
        ResponsiveGrid(
          children: [
            MetricTile(
              icon: Icons.thermostat_rounded,
              label: 'Aktualna',
              value: sensors.flag('temp_valid')
                  ? '${sensors.number('temp_c').toStringAsFixed(2)} °C'
                  : '--',
              detail:
                  'Cel ${temperature.number('target').toStringAsFixed(1)} °C',
            ),
            MetricTile(
              icon: Icons.south_rounded,
              label: 'Minimum',
              value: values.isEmpty ? '--' : '${minimum.toStringAsFixed(2)} °C',
              detail: 'Wybrany zakres $rangeHours h',
            ),
            MetricTile(
              icon: Icons.horizontal_rule_rounded,
              label: 'Średnia',
              value: values.isEmpty ? '--' : '${average.toStringAsFixed(2)} °C',
              detail: '${values.length} próbek',
            ),
            MetricTile(
              icon: Icons.north_rounded,
              label: 'Maksimum',
              value: values.isEmpty ? '--' : '${maximum.toStringAsFixed(2)} °C',
              detail:
                  'Interwał ${temperature.integer('historyIntervalMinutes', 1)} min',
            ),
          ],
        ),
        const SizedBox(height: 14),
        Card(
          child: Padding(
            padding: const EdgeInsets.fromLTRB(16, 16, 16, 10),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: [
                    for (final hours in const [1, 3, 6, 12, 24])
                      ChoiceChip(
                        label: Text('${hours}H'),
                        selected: rangeHours == hours,
                        onSelected: (_) => setState(() => rangeHours = hours),
                      ),
                  ],
                ),
                const SizedBox(height: 16),
                SizedBox(
                  height: 300,
                  child: samples.length < 2
                      ? const Center(
                          child: Text(
                            'Brak wystarczającej liczby próbek w wybranym zakresie.',
                          ),
                        )
                      : CustomPaint(
                          painter: _TemperatureChartPainter(
                            samples: samples,
                            target: temperature.number('target', 25),
                            lineColor: Theme.of(context).colorScheme.primary,
                            targetColor: Theme.of(context).colorScheme.tertiary,
                            gridColor: Theme.of(
                              context,
                            ).colorScheme.outlineVariant,
                            textColor: Theme.of(
                              context,
                            ).colorScheme.onSurfaceVariant,
                          ),
                        ),
                ),
              ],
            ),
          ),
        ),
        const SectionHeader(
          title: 'Bieżące próbki',
          description: 'Ostatnie wartości zapisane w pamięci sterownika.',
        ),
        Card(
          child: SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: DataTable(
              columns: const [
                DataColumn(label: Text('Czas')),
                DataColumn(label: Text('Temperatura')),
                DataColumn(label: Text('Odchylenie od celu')),
              ],
              rows: [
                for (final sample in samples.reversed.take(20))
                  DataRow(
                    cells: [
                      DataCell(Text(_formatEpoch(sample.epoch))),
                      DataCell(Text('${sample.value.toStringAsFixed(2)} °C')),
                      DataCell(
                        Text(
                          '${(sample.value - temperature.number('target', 25)).toStringAsFixed(2)} °C',
                        ),
                      ),
                    ],
                  ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 12),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            FilledButton.icon(
              onPressed: _exportCsv,
              icon: const Icon(Icons.download_rounded),
              label: const Text('Eksportuj CSV'),
            ),
            OutlinedButton.icon(
              onPressed: loadingArchives ? null : _loadArchives,
              icon: loadingArchives
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.folder_copy_outlined),
              label: const Text('Odśwież archiwa SD'),
            ),
          ],
        ),
        if (message != null)
          Padding(
            padding: const EdgeInsets.only(top: 10),
            child: Text(message!),
          ),
        if (widget.session.historyFiles.isNotEmpty) ...[
          const SectionHeader(title: 'Archiwa miesięczne'),
          Card(
            child: Column(
              children: [
                for (
                  var index = 0;
                  index < widget.session.historyFiles.length;
                  index++
                ) ...[
                  Builder(
                    builder: (context) {
                      final file = jsonMap(widget.session.historyFiles[index]);
                      return ListTile(
                        leading: const Icon(Icons.insert_drive_file_outlined),
                        title: Text(file.text('name', 'archiwum')),
                        subtitle: Text(formatBytes(file.integer('size'))),
                      );
                    },
                  ),
                  if (index < widget.session.historyFiles.length - 1)
                    const Divider(height: 1),
                ],
              ],
            ),
          ),
        ],
      ],
    );
  }

  String _formatEpoch(int epoch) {
    final date = DateTime.fromMillisecondsSinceEpoch(epoch * 1000).toLocal();
    return '${date.day.toString().padLeft(2, '0')}.${date.month.toString().padLeft(2, '0')} '
        '${date.hour.toString().padLeft(2, '0')}:${date.minute.toString().padLeft(2, '0')}';
  }
}

class _ChartSample {
  const _ChartSample(this.epoch, this.value);

  final int epoch;
  final double value;
}

class _TemperatureChartPainter extends CustomPainter {
  const _TemperatureChartPainter({
    required this.samples,
    required this.target,
    required this.lineColor,
    required this.targetColor,
    required this.gridColor,
    required this.textColor,
  });

  final List<_ChartSample> samples;
  final double target;
  final Color lineColor;
  final Color targetColor;
  final Color gridColor;
  final Color textColor;

  @override
  void paint(Canvas canvas, Size size) {
    const left = 48.0;
    const right = 12.0;
    const top = 12.0;
    const bottom = 28.0;
    final chart = Rect.fromLTRB(
      left,
      top,
      size.width - right,
      size.height - bottom,
    );
    final values = samples.map((item) => item.value).followedBy([
      target,
    ]).toList();
    var minimum = values.reduce((a, b) => a < b ? a : b) - 0.5;
    var maximum = values.reduce((a, b) => a > b ? a : b) + 0.5;
    if ((maximum - minimum).abs() < 0.2) {
      minimum -= 0.5;
      maximum += 0.5;
    }
    final gridPaint = Paint()
      ..color = gridColor
      ..strokeWidth = 1;
    final textPainter = TextPainter(textDirection: TextDirection.ltr);
    for (var index = 0; index <= 4; index++) {
      final y = chart.top + chart.height * index / 4;
      canvas.drawLine(Offset(chart.left, y), Offset(chart.right, y), gridPaint);
      final value = maximum - (maximum - minimum) * index / 4;
      textPainter.text = TextSpan(
        text: value.toStringAsFixed(1),
        style: TextStyle(color: textColor, fontSize: 11),
      );
      textPainter.layout();
      textPainter.paint(canvas, Offset(2, y - textPainter.height / 2));
    }
    double yFor(double value) =>
        chart.bottom - (value - minimum) / (maximum - minimum) * chart.height;
    final targetY = yFor(target);
    canvas.drawLine(
      Offset(chart.left, targetY),
      Offset(chart.right, targetY),
      Paint()
        ..color = targetColor
        ..strokeWidth = 1.5,
    );
    final firstEpoch = samples.first.epoch;
    final lastEpoch = samples.last.epoch;
    final epochSpan = (lastEpoch - firstEpoch).clamp(1, 1 << 31);
    final path = Path();
    for (var index = 0; index < samples.length; index++) {
      final sample = samples[index];
      final x =
          chart.left + (sample.epoch - firstEpoch) / epochSpan * chart.width;
      final y = yFor(sample.value);
      if (index == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }
    canvas.drawPath(
      path,
      Paint()
        ..color = lineColor
        ..strokeWidth = 2.5
        ..style = PaintingStyle.stroke
        ..strokeJoin = StrokeJoin.round,
    );
  }

  @override
  bool shouldRepaint(covariant _TemperatureChartPainter oldDelegate) =>
      oldDelegate.samples != samples ||
      oldDelegate.target != target ||
      oldDelegate.lineColor != lineColor;
}
