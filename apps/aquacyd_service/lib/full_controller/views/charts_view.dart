import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../controller_api.dart';
import '../controller_session.dart';
import '../data_access.dart';
import '../history_data.dart';
import '../widgets.dart';

class ChartsView extends StatefulWidget {
  const ChartsView({super.key, required this.session});

  final ControllerSession session;

  @override
  State<ChartsView> createState() => _ChartsViewState();
}

class _ChartsViewState extends State<ChartsView> {
  static const ranges = <int>[1, 3, 6, 12, 24, 72, 168];

  int rangeHours = 3;
  _HistoryMetric metric = _HistoryMetric.temperature;
  HistoryLoadResult? history;
  bool loading = false;
  String? message;

  @override
  void initState() {
    super.initState();
    unawaited(_load());
  }

  Future<void> _load() async {
    if (loading) return;
    setState(() {
      loading = true;
      message = null;
    });
    try {
      final result = await widget.session.loadHistory(
        Duration(hours: rangeHours),
      );
      if (mounted) {
        setState(() {
          history = result;
          message = result.warning;
        });
      }
    } on ControllerApiException catch (error) {
      if (mounted) setState(() => message = error.message);
    } on FormatException catch (error) {
      if (mounted) setState(() => message = error.message);
    } finally {
      if (mounted) setState(() => loading = false);
    }
  }

  Future<void> _changeRange(int hours) async {
    if (hours == rangeHours) return;
    setState(() => rangeHours = hours);
    await _load();
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

  @override
  Widget build(BuildContext context) {
    final status = widget.session.status;
    final temperature = status.section('temperature');
    final config = status.section('config');
    final targetTemperature =
        temperature.nullableNumber('target') ??
        config.nullableNumber('target_temp');
    final targetPh = config.nullableNumber('co2TargetPh');
    final allSamples = history?.samples ?? const <HistorySample>[];
    final points = allSamples
        .map((sample) => _PlotPoint(sample.epoch, metric.value(sample)))
        .where((point) => point.value != null)
        .map((point) => _PlotPoint(point.epoch, point.value!))
        .toList(growable: false);
    final values = points.map((point) => point.value!).toList(growable: false);
    final minimum = values.isEmpty
        ? null
        : values.reduce((first, second) => first < second ? first : second);
    final maximum = values.isEmpty
        ? null
        : values.reduce((first, second) => first > second ? first : second);
    final average = values.isEmpty
        ? null
        : values.reduce((first, second) => first + second) / values.length;
    final chartPoints = _downsample(points, 360);
    final target = switch (metric) {
      _HistoryMetric.temperature => targetTemperature,
      _HistoryMetric.ph => targetPh,
      _HistoryMetric.ldr => null,
    };

    return ControllerPageBody(
      maxWidth: 900,
      padding: const EdgeInsets.fromLTRB(12, 10, 12, 28),
      onRefresh: _load,
      children: [
        SectionHeader(
          title: 'Pomiary w czasie',
          description: history?.usedArchive == true
              ? 'Dane bieżące i archiwum SD sterownika.'
              : 'Dane dostępne przez ${widget.session.displayName}.',
          trailing: IconButton(
            onPressed: loading ? null : _load,
            icon: loading
                ? const SizedBox.square(
                    dimension: 20,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : const Icon(Icons.refresh_rounded),
            tooltip: 'Odśwież historię',
          ),
        ),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(10),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                SingleChildScrollView(
                  scrollDirection: Axis.horizontal,
                  child: Row(
                    children: [
                      for (final hours in ranges) ...[
                        ChoiceChip(
                          label: Text(_rangeLabel(hours)),
                          selected: rangeHours == hours,
                          onSelected: loading
                              ? null
                              : (_) => unawaited(_changeRange(hours)),
                          visualDensity: VisualDensity.compact,
                        ),
                        const SizedBox(width: 6),
                      ],
                    ],
                  ),
                ),
                const SizedBox(height: 8),
                SegmentedButton<_HistoryMetric>(
                  showSelectedIcon: false,
                  segments: [
                    for (final value in _HistoryMetric.values)
                      ButtonSegment(
                        value: value,
                        label: Text(value.shortLabel),
                        icon: Icon(value.icon, size: 18),
                      ),
                  ],
                  selected: {metric},
                  onSelectionChanged: (selection) =>
                      setState(() => metric = selection.single),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 10),
        _CompactStatistics(
          metric: metric,
          minimum: minimum,
          average: average,
          maximum: maximum,
          sampleCount: values.length,
          availableRange: history?.availableRange ?? Duration.zero,
        ),
        const SizedBox(height: 10),
        Card(
          child: Padding(
            padding: const EdgeInsets.fromLTRB(10, 12, 10, 8),
            child: SizedBox(
              height: 220,
              child: loading && history == null
                  ? const Center(child: CircularProgressIndicator())
                  : chartPoints.length < 2
                  ? Center(
                      child: Text(
                        'Brak wystarczającej liczby próbek ${metric.label}.',
                        textAlign: TextAlign.center,
                      ),
                    )
                  : RepaintBoundary(
                      child: CustomPaint(
                        painter: _HistoryChartPainter(
                          points: chartPoints,
                          target: target,
                          valueDecimals: metric.decimals,
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
            ),
          ),
        ),
        if (message != null) ...[
          const SizedBox(height: 10),
          StatusBanner(
            icon: Icons.info_outline_rounded,
            title: 'Zakres danych',
            message: message!,
            isError: false,
          ),
        ],
        const SizedBox(height: 10),
        Card(
          clipBehavior: Clip.antiAlias,
          child: ExpansionTile(
            key: const PageStorageKey<String>('history-data-tools'),
            leading: const Icon(Icons.table_rows_outlined),
            title: const Text(
              'Dane źródłowe i eksport',
              style: TextStyle(fontWeight: FontWeight.w700),
            ),
            subtitle: Text(
              '${values.length} wartości · zakres ${_rangeLabel(rangeHours)}',
            ),
            childrenPadding: const EdgeInsets.fromLTRB(12, 0, 12, 12),
            children: [
              _DenseHistoryTable(
                samples: allSamples.reversed.take(80).toList(growable: false),
                metric: metric,
                target: target,
                embedded: true,
              ),
              const SizedBox(height: 10),
              Align(
                alignment: Alignment.centerLeft,
                child: FilledButton.icon(
                  onPressed: widget.session.supportsFileDownload
                      ? _exportCsv
                      : null,
                  icon: const Icon(Icons.download_rounded),
                  label: Text(
                    widget.session.supportsFileDownload
                        ? 'Eksportuj CSV'
                        : 'Eksport wymaga Wi‑Fi',
                  ),
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

enum _HistoryMetric {
  temperature('Temperatura', 'Temp.', '°C', 2, Icons.thermostat_rounded),
  ph('Odczyn pH', 'pH', '', 2, Icons.science_outlined),
  ldr('Jasność względna', 'LDR', ' ADC', 0, Icons.light_mode_outlined);

  const _HistoryMetric(
    this.label,
    this.shortLabel,
    this.unit,
    this.decimals,
    this.icon,
  );

  final String label;
  final String shortLabel;
  final String unit;
  final int decimals;
  final IconData icon;

  double? value(HistorySample sample) => switch (this) {
    temperature => sample.temperature,
    ph => sample.ph,
    ldr => sample.ldr?.toDouble(),
  };

  String format(double? value) =>
      value == null ? '—' : '${value.toStringAsFixed(decimals)}$unit';
}

class _CompactStatistics extends StatelessWidget {
  const _CompactStatistics({
    required this.metric,
    required this.minimum,
    required this.average,
    required this.maximum,
    required this.sampleCount,
    required this.availableRange,
  });

  final _HistoryMetric metric;
  final double? minimum;
  final double? average;
  final double? maximum;
  final int sampleCount;
  final Duration availableRange;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: LayoutBuilder(
        builder: (context, constraints) {
          final columns = constraints.maxWidth >= 620 ? 4 : 2;
          final entries = [
            ('Minimum', metric.format(minimum)),
            ('Średnia', metric.format(average)),
            ('Maksimum', metric.format(maximum)),
            ('Dane', '$sampleCount · ${_compactDuration(availableRange)}'),
          ];
          return GridView.count(
            padding: const EdgeInsets.all(8),
            physics: const NeverScrollableScrollPhysics(),
            shrinkWrap: true,
            crossAxisCount: columns,
            childAspectRatio: columns == 4 ? 1.75 : 2.2,
            children: [
              for (final entry in entries)
                Padding(
                  padding: const EdgeInsets.all(6),
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Text(
                        entry.$1,
                        style: Theme.of(context).textTheme.labelMedium
                            ?.copyWith(
                              color: Theme.of(
                                context,
                              ).colorScheme.onSurfaceVariant,
                            ),
                      ),
                      const SizedBox(height: 3),
                      FittedBox(
                        child: Text(
                          entry.$2,
                          style: const TextStyle(
                            fontSize: 17,
                            fontWeight: FontWeight.w800,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
            ],
          );
        },
      ),
    );
  }
}

class _DenseHistoryTable extends StatelessWidget {
  const _DenseHistoryTable({
    required this.samples,
    required this.metric,
    required this.target,
    this.embedded = false,
  });

  final List<HistorySample> samples;
  final _HistoryMetric metric;
  final double? target;
  final bool embedded;

  @override
  Widget build(BuildContext context) {
    final divider = Theme.of(context).dividerColor;
    final table = Column(
      children: [
        _DenseRow(
          cells: const ['Czas', 'Wartość', 'Odchylenie'],
          header: true,
          divider: divider,
        ),
        if (samples.isEmpty)
          const Padding(
            padding: EdgeInsets.all(18),
            child: Text('Brak próbek w wybranym zakresie.'),
          )
        else
          for (final sample in samples)
            _DenseRow(
              cells: [
                _formatEpoch(sample.epoch),
                metric.format(metric.value(sample)),
                _deviation(metric.value(sample), target, metric),
              ],
              divider: divider,
            ),
      ],
    );
    if (embedded) {
      return DecoratedBox(
        decoration: BoxDecoration(
          border: Border.all(color: divider),
          borderRadius: BorderRadius.circular(12),
        ),
        child: ClipRRect(borderRadius: BorderRadius.circular(12), child: table),
      );
    }
    return Card(clipBehavior: Clip.antiAlias, child: table);
  }

  static String _deviation(
    double? value,
    double? target,
    _HistoryMetric metric,
  ) {
    if (value == null || target == null) return '—';
    final delta = value - target;
    final prefix = delta > 0 ? '+' : '';
    return '$prefix${delta.toStringAsFixed(metric.decimals)}${metric.unit}';
  }
}

class _DenseRow extends StatelessWidget {
  const _DenseRow({
    required this.cells,
    required this.divider,
    this.header = false,
  });

  final List<String> cells;
  final Color divider;
  final bool header;

  @override
  Widget build(BuildContext context) {
    final style = Theme.of(context).textTheme.bodySmall?.copyWith(
      fontWeight: header ? FontWeight.w800 : FontWeight.w500,
    );
    return Container(
      constraints: BoxConstraints(minHeight: header ? 42 : 38),
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 7),
      decoration: BoxDecoration(
        border: Border(bottom: BorderSide(color: divider, width: 0.6)),
      ),
      child: Row(
        children: [
          for (var index = 0; index < cells.length; index++)
            Expanded(
              flex: index == 0 ? 34 : 33,
              child: Text(
                cells[index],
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                textAlign: index == 0 ? TextAlign.start : TextAlign.end,
                style: style,
              ),
            ),
        ],
      ),
    );
  }
}

class _PlotPoint {
  const _PlotPoint(this.epoch, this.value);

  final int epoch;
  final double? value;
}

class _HistoryChartPainter extends CustomPainter {
  const _HistoryChartPainter({
    required this.points,
    required this.target,
    required this.valueDecimals,
    required this.lineColor,
    required this.targetColor,
    required this.gridColor,
    required this.textColor,
  });

  final List<_PlotPoint> points;
  final double? target;
  final int valueDecimals;
  final Color lineColor;
  final Color targetColor;
  final Color gridColor;
  final Color textColor;

  @override
  void paint(Canvas canvas, Size size) {
    const left = 42.0;
    const right = 8.0;
    const top = 8.0;
    const bottom = 24.0;
    final chart = Rect.fromLTRB(
      left,
      top,
      size.width - right,
      size.height - bottom,
    );
    final numeric = points.map((point) => point.value!).toList();
    if (target != null) numeric.add(target!);
    var minimum = numeric.reduce((a, b) => a < b ? a : b);
    var maximum = numeric.reduce((a, b) => a > b ? a : b);
    final padding = ((maximum - minimum).abs() * 0.12).clamp(0.2, 200.0);
    minimum -= padding;
    maximum += padding;
    final gridPaint = Paint()
      ..color = gridColor
      ..strokeWidth = 1;
    final textPainter = TextPainter(textDirection: TextDirection.ltr);
    for (var index = 0; index <= 4; index++) {
      final y = chart.top + chart.height * index / 4;
      canvas.drawLine(Offset(chart.left, y), Offset(chart.right, y), gridPaint);
      final value = maximum - (maximum - minimum) * index / 4;
      textPainter.text = TextSpan(
        text: value.toStringAsFixed(valueDecimals > 1 ? 1 : valueDecimals),
        style: TextStyle(color: textColor, fontSize: 10),
      );
      textPainter.layout();
      textPainter.paint(canvas, Offset(1, y - textPainter.height / 2));
    }
    double yFor(double value) =>
        chart.bottom - (value - minimum) / (maximum - minimum) * chart.height;
    if (target != null) {
      final targetY = yFor(target!);
      canvas.drawLine(
        Offset(chart.left, targetY),
        Offset(chart.right, targetY),
        Paint()
          ..color = targetColor
          ..strokeWidth = 1.5,
      );
    }
    final firstEpoch = points.first.epoch;
    final lastEpoch = points.last.epoch;
    final span = (lastEpoch - firstEpoch).clamp(1, 1 << 31);
    final path = Path();
    for (var index = 0; index < points.length; index++) {
      final point = points[index];
      final x = chart.left + (point.epoch - firstEpoch) / span * chart.width;
      final y = yFor(point.value!);
      index == 0 ? path.moveTo(x, y) : path.lineTo(x, y);
    }
    canvas.drawPath(
      path,
      Paint()
        ..color = lineColor
        ..strokeWidth = 2
        ..style = PaintingStyle.stroke
        ..strokeJoin = StrokeJoin.round,
    );
    final labels = [firstEpoch, firstEpoch + span ~/ 2, lastEpoch];
    for (var index = 0; index < labels.length; index++) {
      textPainter.text = TextSpan(
        text: _formatChartTime(labels[index]),
        style: TextStyle(color: textColor, fontSize: 9),
      );
      textPainter.layout();
      final x = chart.left + chart.width * index / 2;
      final offset = index == 0
          ? 0.0
          : index == 2
          ? -textPainter.width
          : -textPainter.width / 2;
      textPainter.paint(canvas, Offset(x + offset, chart.bottom + 5));
    }
  }

  @override
  bool shouldRepaint(covariant _HistoryChartPainter oldDelegate) =>
      oldDelegate.points != points ||
      oldDelegate.target != target ||
      oldDelegate.lineColor != lineColor;
}

List<_PlotPoint> _downsample(List<_PlotPoint> source, int maximumPoints) {
  if (source.length <= maximumPoints) return source;
  final result = <_PlotPoint>[];
  final step = (source.length - 1) / (maximumPoints - 1);
  for (var index = 0; index < maximumPoints; index++) {
    result.add(source[(index * step).round().clamp(0, source.length - 1)]);
  }
  return result;
}

String _rangeLabel(int hours) {
  if (hours >= 24 && hours % 24 == 0) return '${hours ~/ 24}D';
  return '${hours}H';
}

String _compactDuration(Duration duration) {
  if (duration.inDays >= 1) return '${duration.inDays} d';
  if (duration.inHours >= 1) return '${duration.inHours} h';
  return '${duration.inMinutes} min';
}

String _formatEpoch(int epoch) {
  final date = DateTime.fromMillisecondsSinceEpoch(epoch * 1000).toLocal();
  return '${date.day.toString().padLeft(2, '0')}.${date.month.toString().padLeft(2, '0')} '
      '${date.hour.toString().padLeft(2, '0')}:${date.minute.toString().padLeft(2, '0')}';
}

String _formatChartTime(int epoch) {
  final date = DateTime.fromMillisecondsSinceEpoch(epoch * 1000).toLocal();
  return '${date.hour.toString().padLeft(2, '0')}:${date.minute.toString().padLeft(2, '0')}';
}
