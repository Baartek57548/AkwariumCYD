import 'dart:async';
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

enum DisplayRefreshTier { standard, high, ultra }

@immutable
class DisplayModeInfo {
  const DisplayModeInfo({
    required this.modeId,
    required this.width,
    required this.height,
    required this.refreshRate,
  });

  factory DisplayModeInfo.fromMap(Map<Object?, Object?> map) {
    return DisplayModeInfo(
      modeId: _integer(map['modeId']),
      width: _integer(map['width']),
      height: _integer(map['height']),
      refreshRate: _finiteRate(map['refreshRate']),
    );
  }

  final int modeId;
  final int width;
  final int height;
  final double refreshRate;

  String get resolutionLabel => '$width×$height';
  String get refreshRateLabel => formatRefreshRate(refreshRate);
  String get label => '$resolutionLabel @ $refreshRateLabel Hz';

  @override
  bool operator ==(Object other) =>
      other is DisplayModeInfo &&
      modeId == other.modeId &&
      width == other.width &&
      height == other.height &&
      (refreshRate - other.refreshRate).abs() < 0.01;

  @override
  int get hashCode =>
      Object.hash(modeId, width, height, refreshRate.toStringAsFixed(2));
}

@immutable
class DisplayRefreshState {
  const DisplayRefreshState({
    required this.supportedModes,
    required this.requestedMode,
    required this.activeMode,
    required this.fallbackRefreshRate,
    this.error,
  });

  factory DisplayRefreshState.fallback(double refreshRate) {
    return DisplayRefreshState(
      supportedModes: const [],
      requestedMode: null,
      activeMode: null,
      fallbackRefreshRate: _finiteRate(refreshRate),
    );
  }

  final List<DisplayModeInfo> supportedModes;
  final DisplayModeInfo? requestedMode;
  final DisplayModeInfo? activeMode;
  final double fallbackRefreshRate;
  final String? error;

  double get activeRefreshRate =>
      activeMode?.refreshRate ?? fallbackRefreshRate;

  double? get requestedRefreshRate => requestedMode?.refreshRate;

  double? get maximumRefreshRate {
    if (supportedModes.isEmpty) return null;
    return supportedModes
        .map((mode) => mode.refreshRate)
        .reduce((first, second) => first > second ? first : second);
  }

  List<double> get groupedSupportedRates {
    final result = <double>[];
    for (final rate
        in supportedModes.map((mode) => mode.refreshRate).toList()..sort()) {
      if (result.every((existing) => (existing - rate).abs() > 0.5)) {
        result.add(rate);
      }
    }
    return List.unmodifiable(result);
  }

  String get supportedRatesLabel => groupedSupportedRates.isEmpty
      ? 'brak danych'
      : groupedSupportedRates.map(formatRefreshRate).join(' / ');

  DisplayRefreshState withFallback(double refreshRate) => DisplayRefreshState(
    supportedModes: supportedModes,
    requestedMode: requestedMode,
    activeMode: activeMode,
    fallbackRefreshRate: _finiteRate(refreshRate),
    error: error,
  );

  @override
  bool operator ==(Object other) =>
      other is DisplayRefreshState &&
      listEquals(supportedModes, other.supportedModes) &&
      requestedMode == other.requestedMode &&
      activeMode == other.activeMode &&
      (fallbackRefreshRate - other.fallbackRefreshRate).abs() < 0.1 &&
      error == other.error;

  @override
  int get hashCode => Object.hash(
    Object.hashAll(supportedModes),
    requestedMode,
    activeMode,
    fallbackRefreshRate.toStringAsFixed(1),
    error,
  );
}

@immutable
class DisplayRefreshProfile {
  const DisplayRefreshProfile._({
    required this.refreshRate,
    required this.tier,
    required this.transitionDuration,
    required this.shortAnimationDuration,
  });

  factory DisplayRefreshProfile.fromRefreshRate(double refreshRate) {
    final rate = _finiteRate(refreshRate);
    if (rate >= 132) {
      return DisplayRefreshProfile._(
        refreshRate: rate,
        tier: DisplayRefreshTier.ultra,
        transitionDuration: const Duration(milliseconds: 180),
        shortAnimationDuration: const Duration(milliseconds: 100),
      );
    }
    if (rate >= 105) {
      return DisplayRefreshProfile._(
        refreshRate: rate,
        tier: DisplayRefreshTier.high,
        transitionDuration: const Duration(milliseconds: 200),
        shortAnimationDuration: const Duration(milliseconds: 120),
      );
    }
    return DisplayRefreshProfile._(
      refreshRate: rate,
      tier: DisplayRefreshTier.standard,
      transitionDuration: const Duration(milliseconds: 240),
      shortAnimationDuration: const Duration(milliseconds: 140),
    );
  }

  static final fallback = DisplayRefreshProfile.fromRefreshRate(60);

  final double refreshRate;
  final DisplayRefreshTier tier;
  final Duration transitionDuration;
  final Duration shortAnimationDuration;

  int get roundedHertz => refreshRate.round();
  double get frameBudgetMilliseconds => 1000 / refreshRate;

  String get description => switch (tier) {
    DisplayRefreshTier.standard => 'tryb standardowy',
    DisplayRefreshTier.high => 'tryb wysokiej płynności',
    DisplayRefreshTier.ultra => 'tryb ultra płynny',
  };

  @override
  bool operator ==(Object other) =>
      other is DisplayRefreshProfile &&
      (other.refreshRate - refreshRate).abs() < 0.1 &&
      other.tier == tier;

  @override
  int get hashCode => Object.hash(refreshRate.toStringAsFixed(1), tier);
}

class DisplayRefreshRateController extends ChangeNotifier
    with WidgetsBindingObserver {
  DisplayRefreshRateController({MethodChannel? channel})
    : _channel = channel ?? const MethodChannel('pl.cydakwarium/display_mode') {
    WidgetsBinding.instance.addObserver(this);
    final fallback = _readFlutterRefreshRate();
    _state = DisplayRefreshState.fallback(fallback);
    _channel.setMethodCallHandler(_handleNativeCall);
    unawaited(initialize());
  }

  final MethodChannel _channel;
  late DisplayRefreshState _state;
  bool _disposed = false;

  DisplayRefreshState get state => _state;
  DisplayRefreshProfile get profile =>
      DisplayRefreshProfile.fromRefreshRate(_state.activeRefreshRate);

  Future<void> initialize() async {
    if (defaultTargetPlatform != TargetPlatform.android) {
      _refreshFlutterFallback();
      return;
    }
    try {
      final response = await _channel.invokeMethod<Object?>(
        'requestMaximumRefreshRate',
      );
      _applyNativeResponse(response);
    } on MissingPluginException {
      _refreshFlutterFallback();
    } on PlatformException catch (error) {
      _applyError(error.message ?? error.code);
    }
  }

  @override
  void didChangeMetrics() => _refreshFlutterFallback();

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) unawaited(initialize());
  }

  Future<void> _handleNativeCall(MethodCall call) async {
    if (call.method == 'displayInfoChanged') {
      _applyNativeResponse(call.arguments);
    }
  }

  void _applyNativeResponse(Object? raw) {
    if (_disposed || raw is! Map) return;
    try {
      final map = raw.cast<Object?, Object?>();
      final supported = <DisplayModeInfo>[];
      final rawModes = map['supportedModes'];
      if (rawModes is List) {
        for (final item in rawModes) {
          if (item is Map) {
            supported.add(
              DisplayModeInfo.fromMap(item.cast<Object?, Object?>()),
            );
          }
        }
      }
      final requested = _readMode(map['requestedMode']);
      final active = _readMode(map['activeMode']);
      final next = DisplayRefreshState(
        supportedModes: List.unmodifiable(supported),
        requestedMode: requested,
        activeMode: active,
        fallbackRefreshRate: _readFlutterRefreshRate(),
        error: map['error']?.toString(),
      );
      _setState(next);
    } on FormatException catch (error) {
      _applyError(error.message);
    }
  }

  DisplayModeInfo? _readMode(Object? raw) {
    if (raw is! Map) return null;
    return DisplayModeInfo.fromMap(raw.cast<Object?, Object?>());
  }

  void _applyError(String message) {
    _setState(
      DisplayRefreshState(
        supportedModes: _state.supportedModes,
        requestedMode: _state.requestedMode,
        activeMode: _state.activeMode,
        fallbackRefreshRate: _readFlutterRefreshRate(),
        error: message,
      ),
    );
  }

  void _refreshFlutterFallback() {
    _setState(_state.withFallback(_readFlutterRefreshRate()));
  }

  void _setState(DisplayRefreshState next) {
    if (_disposed || next == _state) return;
    _state = next;
    notifyListeners();
  }

  static double _readFlutterRefreshRate() {
    final views = WidgetsBinding.instance.platformDispatcher.views;
    ui.FlutterView? view;
    for (final candidate in views) {
      view = candidate;
      break;
    }
    return _finiteRate(view?.display.refreshRate ?? 60);
  }

  @override
  void dispose() {
    _disposed = true;
    WidgetsBinding.instance.removeObserver(this);
    _channel.setMethodCallHandler(null);
    super.dispose();
  }
}

class DisplayRefreshRateScope extends InheritedWidget {
  const DisplayRefreshRateScope({
    super.key,
    required this.profile,
    required this.state,
    required super.child,
  });

  final DisplayRefreshProfile profile;
  final DisplayRefreshState state;

  static DisplayRefreshRateScope? maybeOf(BuildContext context) =>
      context.dependOnInheritedWidgetOfExactType<DisplayRefreshRateScope>();

  static DisplayRefreshProfile of(BuildContext context) =>
      maybeOf(context)?.profile ?? DisplayRefreshProfile.fallback;

  static DisplayRefreshState stateOf(BuildContext context) =>
      maybeOf(context)?.state ?? DisplayRefreshState.fallback(60);

  @override
  bool updateShouldNotify(DisplayRefreshRateScope oldWidget) =>
      profile != oldWidget.profile || state != oldWidget.state;
}

String formatRefreshRate(double refreshRate) {
  final rate = _finiteRate(refreshRate);
  final rounded = rate.roundToDouble();
  return (rate - rounded).abs() <= 0.5
      ? rounded.toInt().toString()
      : rate.toStringAsFixed(2);
}

double _finiteRate(Object? value) {
  final rate = switch (value) {
    num number => number.toDouble(),
    _ => double.tryParse(value?.toString() ?? ''),
  };
  return rate != null && rate.isFinite && rate >= 30
      ? rate.clamp(30, 360).toDouble()
      : 60;
}

int _integer(Object? value) {
  if (value is int) return value;
  if (value is num && value.isFinite) return value.toInt();
  final parsed = int.tryParse(value?.toString() ?? '');
  if (parsed == null) throw const FormatException('Nieprawidłowy tryb ekranu.');
  return parsed;
}
