import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';

enum DisplayRefreshTier { standard, high, ultra }

@immutable
class DisplayRefreshProfile {
  const DisplayRefreshProfile._({
    required this.refreshRate,
    required this.tier,
    required this.transitionDuration,
    required this.shortAnimationDuration,
  });

  factory DisplayRefreshProfile.fromRefreshRate(double refreshRate) {
    final rate = refreshRate.isFinite && refreshRate >= 30
        ? refreshRate.clamp(30, 360).toDouble()
        : 60.0;
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
  DisplayRefreshRateController() {
    WidgetsBinding.instance.addObserver(this);
    _profile = _readProfile();
  }

  late DisplayRefreshProfile _profile;
  bool _disposed = false;

  DisplayRefreshProfile get profile => _profile;

  @override
  void didChangeMetrics() => _refresh();

  void _refresh() {
    if (_disposed) return;
    final next = _readProfile();
    if (next == _profile) return;
    _profile = next;
    notifyListeners();
  }

  static DisplayRefreshProfile _readProfile() {
    final views = WidgetsBinding.instance.platformDispatcher.views;
    ui.FlutterView? view;
    for (final candidate in views) {
      view = candidate;
      break;
    }
    return DisplayRefreshProfile.fromRefreshRate(
      view?.display.refreshRate ?? 60,
    );
  }

  @override
  void dispose() {
    _disposed = true;
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }
}

class DisplayRefreshRateScope extends InheritedWidget {
  const DisplayRefreshRateScope({
    super.key,
    required this.profile,
    required super.child,
  });

  final DisplayRefreshProfile profile;

  static DisplayRefreshProfile of(BuildContext context) {
    return context
            .dependOnInheritedWidgetOfExactType<DisplayRefreshRateScope>()
            ?.profile ??
        DisplayRefreshProfile.fallback;
  }

  @override
  bool updateShouldNotify(DisplayRefreshRateScope oldWidget) =>
      profile != oldWidget.profile;
}
