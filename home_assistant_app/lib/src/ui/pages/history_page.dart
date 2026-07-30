import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:intl/intl.dart' as intl;

import '../../design/app_theme.dart';
import '../../domain/entity_ids.dart';
import '../../domain/models.dart';
import '../../state/aquacyd_controller.dart';
import '../widgets/common.dart';

class HistoryPage extends StatefulWidget {
  const HistoryPage({required this.controller, super.key});

  final AquaCydController controller;

  @override
  State<HistoryPage> createState() => _HistoryPageState();
}

class _HistoryPageState extends State<HistoryPage> {
  static const _metrics = <_HistoryMetric>[
    _HistoryMetric(
      AquaEntityIds.temperature,
      'Temperatura',
      '°C',
      AquaColors.red,
    ),
    _HistoryMetric(AquaEntityIds.ph, 'Odczyn pH', '', AquaColors.blue),
    _HistoryMetric(
      AquaEntityIds.ec,
      'Przewodność EC',
      'µS/cm',
      AquaColors.amber,
    ),
    _HistoryMetric(
      AquaEntityIds.ldr,
      'Natężenie światła',
      'lx',
      AquaColors.green,
    ),
  ];

  static const _periods = <({Duration duration, String label})>[
    (duration: Duration(hours: 6), label: '6 h'),
    (duration: Duration(hours: 24), label: '24 h'),
    (duration: Duration(days: 7), label: '7 dni'),
  ];

  var _selectedMetric = _metrics.first;
  var _selectedPeriod = const Duration(hours: 24);
  var _requestedInitialData = false;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (!_requestedInitialData) {
      _requestedInitialData = true;
      WidgetsBinding.instance.addPostFrameCallback((_) => _load());
    }
  }

  @override
  Widget build(BuildContext context) {
    final history = widget.controller.history;
    return AquaPage(
      children: <Widget>[
        const SectionTitle(
          title: 'Historia pomiarów',
          subtitle: 'Dane z rejestratora Home Assistant',
        ),
        const SizedBox(height: 16),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(18),
            child: Wrap(
              spacing: 14,
              runSpacing: 14,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: <Widget>[
                SizedBox(
                  width: 250,
                  child: DropdownButtonFormField<_HistoryMetric>(
                    isExpanded: true,
                    initialValue: _selectedMetric,
                    decoration: const InputDecoration(
                      labelText: 'Parametr',
                      prefixIcon: Icon(Icons.monitor_heart_outlined),
                    ),
                    items: [
                      for (final metric in _metrics)
                        DropdownMenuItem<_HistoryMetric>(
                          value: metric,
                          child: Text(metric.label),
                        ),
                    ],
                    onChanged: (metric) {
                      if (metric != null && metric != _selectedMetric) {
                        setState(() => _selectedMetric = metric);
                        _load();
                      }
                    },
                  ),
                ),
                SegmentedButton<Duration>(
                  segments: [
                    for (final entry in _periods)
                      ButtonSegment<Duration>(
                        value: entry.duration,
                        label: Text(entry.label),
                      ),
                  ],
                  selected: <Duration>{_selectedPeriod},
                  onSelectionChanged: (values) {
                    setState(() => _selectedPeriod = values.first);
                    _load();
                  },
                ),
                IconButton.filledTonal(
                  tooltip: 'Odśwież wykres',
                  onPressed: widget.controller.loadingHistory ? null : _load,
                  icon: const Icon(Icons.refresh_rounded),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 16),
        _HistoryCard(
          metric: _selectedMetric,
          period: _selectedPeriod,
          samples: history,
          loading: widget.controller.loadingHistory,
        ),
      ],
    );
  }

  Future<void> _load() {
    return widget.controller.loadHistory(
      _selectedMetric.entityId,
      _selectedPeriod,
    );
  }
}

class _HistoryCard extends StatelessWidget {
  const _HistoryCard({
    required this.metric,
    required this.period,
    required this.samples,
    required this.loading,
  });

  final _HistoryMetric metric;
  final Duration period;
  final List<HistorySample> samples;
  final bool loading;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(18, 20, 18, 18),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: <Widget>[
            Row(
              children: <Widget>[
                Container(
                  width: 11,
                  height: 11,
                  decoration: BoxDecoration(
                    color: metric.color,
                    shape: BoxShape.circle,
                  ),
                ),
                const SizedBox(width: 9),
                Expanded(
                  child: Text(
                    metric.label,
                    style: Theme.of(context).textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                ),
                Text(
                  '${samples.length} próbek',
                  style: Theme.of(context).textTheme.labelMedium?.copyWith(
                    color: Theme.of(context).colorScheme.onSurfaceVariant,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 18),
            SizedBox(
              height: 300,
              child: AnimatedSwitcher(
                duration: const Duration(milliseconds: 250),
                child: loading
                    ? const Center(child: CircularProgressIndicator())
                    : samples.isEmpty
                    ? const _EmptyHistory()
                    : CustomPaint(
                        painter: _HistoryChartPainter(
                          samples: samples,
                          color: metric.color,
                          unit: metric.unit,
                          period: period,
                          textColor: Theme.of(
                            context,
                          ).colorScheme.onSurfaceVariant,
                          gridColor: Theme.of(
                            context,
                          ).colorScheme.outlineVariant,
                        ),
                        child: const SizedBox.expand(),
                      ),
              ),
            ),
            if (!loading && samples.isNotEmpty) ...<Widget>[
              const Divider(height: 32),
              _Statistics(samples: samples, metric: metric),
            ],
          ],
        ),
      ),
    );
  }
}

class _Statistics extends StatelessWidget {
  const _Statistics({required this.samples, required this.metric});

  final List<HistorySample> samples;
  final _HistoryMetric metric;

  @override
  Widget build(BuildContext context) {
    var minimum = samples.first.value;
    var maximum = samples.first.value;
    var sum = 0.0;
    for (final sample in samples) {
      minimum = math.min(minimum, sample.value);
      maximum = math.max(maximum, sample.value);
      sum += sample.value;
    }
    final average = sum / samples.length;
    return Row(
      children: <Widget>[
        _Statistic(label: 'Minimum', value: _format(minimum)),
        _Statistic(label: 'Średnia', value: _format(average)),
        _Statistic(label: 'Maksimum', value: _format(maximum)),
      ],
    );
  }

  String _format(double value) {
    final decimals = metric.entityId == AquaEntityIds.ph ? 2 : 1;
    return '${value.toStringAsFixed(decimals)} ${metric.unit}'.trim();
  }
}

class _Statistic extends StatelessWidget {
  const _Statistic({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Expanded(
      child: Column(
        children: <Widget>[
          Text(
            value,
            textAlign: TextAlign.center,
            style: const TextStyle(fontWeight: FontWeight.w800),
          ),
          const SizedBox(height: 3),
          Text(
            label,
            style: Theme.of(context).textTheme.labelSmall?.copyWith(
              color: Theme.of(context).colorScheme.onSurfaceVariant,
            ),
          ),
        ],
      ),
    );
  }
}

class _EmptyHistory extends StatelessWidget {
  const _EmptyHistory();

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: <Widget>[
          Icon(
            Icons.query_stats_rounded,
            size: 46,
            color: Theme.of(context).colorScheme.outline,
          ),
          const SizedBox(height: 12),
          const Text(
            'Brak próbek w wybranym okresie',
            style: TextStyle(fontWeight: FontWeight.w700),
          ),
          const SizedBox(height: 4),
          Text(
            'Sprawdź, czy encja jest rejestrowana przez HA.',
            style: Theme.of(context).textTheme.bodySmall,
          ),
        ],
      ),
    );
  }
}

class _HistoryChartPainter extends CustomPainter {
  const _HistoryChartPainter({
    required this.samples,
    required this.color,
    required this.unit,
    required this.period,
    required this.textColor,
    required this.gridColor,
  });

  final List<HistorySample> samples;
  final Color color;
  final String unit;
  final Duration period;
  final Color textColor;
  final Color gridColor;

  @override
  void paint(Canvas canvas, Size size) {
    if (samples.isEmpty || size.isEmpty) {
      return;
    }
    const left = 54.0;
    const right = 12.0;
    const top = 10.0;
    const bottom = 28.0;
    final chart = Rect.fromLTRB(
      left,
      top,
      size.width - right,
      size.height - bottom,
    );
    if (chart.width <= 0 || chart.height <= 0) {
      return;
    }

    var minimum = samples.first.value;
    var maximum = samples.first.value;
    for (final sample in samples) {
      minimum = math.min(minimum, sample.value);
      maximum = math.max(maximum, sample.value);
    }
    if ((maximum - minimum).abs() < 0.0001) {
      final padding = maximum.abs() < 1 ? 0.5 : maximum.abs() * 0.05;
      minimum -= padding;
      maximum += padding;
    } else {
      final padding = (maximum - minimum) * 0.08;
      minimum -= padding;
      maximum += padding;
    }

    final gridPaint = Paint()
      ..color = gridColor.withValues(alpha: 0.6)
      ..strokeWidth = 1;
    for (var row = 0; row <= 4; row++) {
      final y = chart.top + chart.height * row / 4;
      canvas.drawLine(Offset(chart.left, y), Offset(chart.right, y), gridPaint);
      final value = maximum - (maximum - minimum) * row / 4;
      _drawText(
        canvas,
        value.toStringAsFixed(unit == 'µS/cm' || unit == 'lx' ? 0 : 1),
        Offset(0, y - 7),
        width: left - 7,
        align: TextAlign.right,
      );
    }

    final firstTime = samples.first.time.millisecondsSinceEpoch;
    final lastTime = samples.last.time.millisecondsSinceEpoch;
    final timeSpan = math.max(1, lastTime - firstTime);
    final line = Path();
    final fill = Path();
    Offset? firstPoint;
    Offset? lastPoint;
    for (var index = 0; index < samples.length; index++) {
      final sample = samples[index];
      final x =
          chart.left +
          chart.width *
              (sample.time.millisecondsSinceEpoch - firstTime) /
              timeSpan;
      final y =
          chart.bottom -
          chart.height * (sample.value - minimum) / (maximum - minimum);
      final point = Offset(x, y);
      if (index == 0) {
        line.moveTo(point.dx, point.dy);
        firstPoint = point;
      } else {
        line.lineTo(point.dx, point.dy);
      }
      lastPoint = point;
    }

    if (firstPoint != null && lastPoint != null) {
      fill
        ..moveTo(firstPoint.dx, chart.bottom)
        ..lineTo(firstPoint.dx, firstPoint.dy)
        ..addPath(line, Offset.zero)
        ..lineTo(lastPoint.dx, chart.bottom)
        ..close();
      canvas.drawPath(
        fill,
        Paint()
          ..shader = LinearGradient(
            begin: Alignment.topCenter,
            end: Alignment.bottomCenter,
            colors: <Color>[
              color.withValues(alpha: 0.28),
              color.withValues(alpha: 0.01),
            ],
          ).createShader(chart),
      );
    }
    canvas.drawPath(
      line,
      Paint()
        ..color = color
        ..strokeWidth = 2.5
        ..style = PaintingStyle.stroke
        ..strokeCap = StrokeCap.round
        ..strokeJoin = StrokeJoin.round,
    );

    final format = period >= const Duration(days: 2)
        ? intl.DateFormat('d.MM', 'pl')
        : intl.DateFormat('HH:mm', 'pl');
    _drawText(
      canvas,
      format.format(samples.first.time),
      Offset(chart.left, chart.bottom + 7),
      width: chart.width / 2,
    );
    _drawText(
      canvas,
      format.format(samples.last.time),
      Offset(chart.center.dx, chart.bottom + 7),
      width: chart.width / 2,
      align: TextAlign.right,
    );
  }

  void _drawText(
    Canvas canvas,
    String text,
    Offset offset, {
    required double width,
    TextAlign align = TextAlign.left,
  }) {
    final painter = TextPainter(
      text: TextSpan(
        text: text,
        style: TextStyle(color: textColor, fontSize: 10),
      ),
      textDirection: TextDirection.ltr,
      textAlign: align,
    )..layout(maxWidth: width);
    painter.paint(canvas, offset);
  }

  @override
  bool shouldRepaint(covariant _HistoryChartPainter oldDelegate) {
    return oldDelegate.samples != samples ||
        oldDelegate.color != color ||
        oldDelegate.textColor != textColor ||
        oldDelegate.gridColor != gridColor ||
        oldDelegate.period != period;
  }
}

class _HistoryMetric {
  const _HistoryMetric(this.entityId, this.label, this.unit, this.color);

  final String entityId;
  final String label;
  final String unit;
  final Color color;
}
