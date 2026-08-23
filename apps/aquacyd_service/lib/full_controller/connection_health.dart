import 'package:flutter/foundation.dart';

enum ControllerConnectionPhase { connecting, online, reconnecting, offline }

@immutable
class ControllerConnectionHealth {
  const ControllerConnectionHealth({
    required this.phase,
    required this.failedAttempts,
    this.rssi,
    this.roundTrip,
    this.lastSync,
  });

  final ControllerConnectionPhase phase;
  final int failedAttempts;
  final int? rssi;
  final Duration? roundTrip;
  final DateTime? lastSync;

  bool get hasTelemetry => lastSync != null;

  bool get isOnline => phase == ControllerConnectionPhase.online;

  String get phaseLabel => switch (phase) {
    ControllerConnectionPhase.connecting => 'Łączenie',
    ControllerConnectionPhase.online => 'Online',
    ControllerConnectionPhase.reconnecting => 'Ponawianie',
    ControllerConnectionPhase.offline => 'Offline',
  };

  String get signalLabel {
    final value = rssi;
    if (value == null) return 'Brak danych';
    if (value >= -55) return 'Bardzo dobry';
    if (value >= -67) return 'Dobry';
    if (value >= -75) return 'Średni';
    return 'Słaby';
  }

  int get signalBars {
    final value = rssi;
    if (value == null) return 0;
    if (value >= -55) return 4;
    if (value >= -67) return 3;
    if (value >= -75) return 2;
    return 1;
  }

  String ageLabel(DateTime now) {
    final syncedAt = lastSync;
    if (syncedAt == null) return 'Oczekiwanie';
    final age = now.difference(syncedAt);
    if (age.isNegative || age.inSeconds < 2) return 'Przed chwilą';
    if (age.inSeconds < 60) return '${age.inSeconds} s temu';
    if (age.inMinutes < 60) return '${age.inMinutes} min temu';
    return '${age.inHours} h temu';
  }

  @override
  bool operator ==(Object other) {
    return other is ControllerConnectionHealth &&
        other.phase == phase &&
        other.failedAttempts == failedAttempts &&
        other.rssi == rssi &&
        other.roundTrip == roundTrip &&
        other.lastSync == lastSync;
  }

  @override
  int get hashCode =>
      Object.hash(phase, failedAttempts, rssi, roundTrip, lastSync);
}
