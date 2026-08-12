import 'dart:math' as math;

import 'package:flutter/foundation.dart';

import 'entity_ids.dart';

@immutable
class HomeAssistantCredentials {
  const HomeAssistantCredentials({
    required this.baseUri,
    required this.accessToken,
  });

  final Uri baseUri;
  final String accessToken;

  static HomeAssistantCredentials parse({
    required String baseUrl,
    required String accessToken,
  }) {
    final trimmedUrl = baseUrl.trim();
    final token = accessToken.trim();
    final uri = Uri.tryParse(trimmedUrl);
    if (uri == null ||
        !uri.hasScheme ||
        !uri.hasAuthority ||
        (uri.scheme != 'http' && uri.scheme != 'https')) {
      throw const FormatException(
        'Podaj pełny adres HTTP lub HTTPS Home Assistanta.',
      );
    }
    if (uri.userInfo.isNotEmpty ||
        uri.query.isNotEmpty ||
        uri.fragment.isNotEmpty) {
      throw const FormatException(
        'Adres nie może zawierać loginu, parametrów ani fragmentu.',
      );
    }
    if (token.length < 20) {
      throw const FormatException('Token dostępu jest zbyt krótki.');
    }
    if (uri.scheme == 'http' && !_isLocalHost(uri.host)) {
      throw const FormatException(
        'Nieszyfrowane HTTP jest dozwolone tylko w sieci lokalnej.',
      );
    }
    final normalizedPath = uri.path == '/'
        ? ''
        : uri.path.replaceFirst(RegExp(r'/+$'), '');
    return HomeAssistantCredentials(
      baseUri: uri.replace(path: normalizedPath),
      accessToken: token,
    );
  }

  static bool _isLocalHost(String host) {
    final normalized = host.toLowerCase();
    if (normalized == 'localhost' ||
        normalized.endsWith('.local') ||
        normalized.endsWith('.lan')) {
      return true;
    }
    final parts = normalized.split('.');
    if (parts.length != 4) {
      return false;
    }
    final octets = parts.map(int.tryParse).toList(growable: false);
    if (octets.any((value) => value == null || value < 0 || value > 255)) {
      return false;
    }
    final first = octets[0]!;
    final second = octets[1]!;
    return first == 10 ||
        first == 127 ||
        (first == 192 && second == 168) ||
        (first == 172 && second >= 16 && second <= 31);
  }
}

@immutable
class HomeAssistantConfig {
  const HomeAssistantConfig({
    required this.locationName,
    required this.version,
    required this.timeZone,
  });

  factory HomeAssistantConfig.fromJson(Map<String, Object?> json) {
    return HomeAssistantConfig(
      locationName: json['location_name'] as String? ?? 'Home Assistant',
      version: json['version'] as String? ?? 'nieznana',
      timeZone: json['time_zone'] as String? ?? 'nieznana',
    );
  }

  final String locationName;
  final String version;
  final String timeZone;
}

@immutable
class HaEntityState {
  const HaEntityState({
    required this.entityId,
    required this.state,
    required this.attributes,
    required this.lastChanged,
    required this.lastUpdated,
  });

  factory HaEntityState.fromJson(Map<String, Object?> json) {
    final entityId = json['entity_id'] as String?;
    final state = json['state'] as String?;
    if (entityId == null || entityId.isEmpty || state == null) {
      throw const FormatException('Niepełny stan encji Home Assistant.');
    }
    final rawAttributes = json['attributes'];
    return HaEntityState(
      entityId: entityId,
      state: state,
      attributes: rawAttributes is Map
          ? Map<String, Object?>.from(rawAttributes)
          : const <String, Object?>{},
      lastChanged:
          _parseDate(json['last_changed']) ??
          DateTime.fromMillisecondsSinceEpoch(0),
      lastUpdated:
          _parseDate(json['last_updated']) ??
          _parseDate(json['last_changed']) ??
          DateTime.fromMillisecondsSinceEpoch(0),
    );
  }

  final String entityId;
  final String state;
  final Map<String, Object?> attributes;
  final DateTime lastChanged;
  final DateTime lastUpdated;

  bool get available => state != 'unknown' && state != 'unavailable';

  bool get isOn => state.toLowerCase() == 'on';

  double? get number {
    final value = double.tryParse(state.replaceAll(',', '.'));
    return value != null && value.isFinite ? value : null;
  }

  int? get integer => int.tryParse(state);

  String get friendlyName => attributes['friendly_name'] as String? ?? entityId;

  String get unit => attributes['unit_of_measurement'] as String? ?? '';

  static DateTime? _parseDate(Object? value) {
    return value is String ? DateTime.tryParse(value)?.toLocal() : null;
  }
}

@immutable
class HistorySample {
  const HistorySample({required this.time, required this.value});

  final DateTime time;
  final double value;
}

@immutable
class AquaSchedule {
  const AquaSchedule({
    required this.target,
    required this.mode,
    required this.profile,
    required this.startMinute,
    required this.endMinute,
  });

  factory AquaSchedule.fromEntities(
    String target,
    Map<String, HaEntityState> entities,
  ) {
    int read(String field, int fallback) {
      return entities[AquaEntityIds.schedule(target, field)]?.integer ??
          fallback;
    }

    return AquaSchedule(
      target: target,
      mode: read('mode', 0).clamp(0, 2),
      profile: read('profile', 0).clamp(0, 3),
      startMinute: read('start', 600).clamp(0, 1439),
      endMinute: read('end', 1200).clamp(0, 1439),
    );
  }

  final String target;
  final int mode;
  final int profile;
  final int startMinute;
  final int endMinute;

  String get startText => _formatMinutes(startMinute);
  String get endText => _formatMinutes(endMinute);

  static String _formatMinutes(int minute) {
    final hours = minute ~/ 60;
    final minutes = minute % 60;
    return '${hours.toString().padLeft(2, '0')}:'
        '${minutes.toString().padLeft(2, '0')}';
  }
}

@immutable
class AquariumSnapshot {
  const AquariumSnapshot({
    required this.entities,
    required this.schedules,
    required this.lastUpdated,
  });

  factory AquariumSnapshot.fromEntities(Map<String, HaEntityState> entities) {
    final relevant = entities.values.where(
      (entity) => AquaEntityIds.all.contains(entity.entityId),
    );
    var lastUpdated = DateTime.fromMillisecondsSinceEpoch(0);
    for (final entity in relevant) {
      if (entity.lastUpdated.isAfter(lastUpdated)) {
        lastUpdated = entity.lastUpdated;
      }
    }
    return AquariumSnapshot(
      entities: Map<String, HaEntityState>.unmodifiable(entities),
      schedules: {
        for (final target in AquaEntityIds.scheduleTargets)
          target: AquaSchedule.fromEntities(target, entities),
      },
      lastUpdated: lastUpdated,
    );
  }

  final Map<String, HaEntityState> entities;
  final Map<String, AquaSchedule> schedules;
  final DateTime lastUpdated;

  HaEntityState? entity(String id) => entities[id];
  double? number(String id) => entities[id]?.number;
  bool? binary(String id) {
    final value = entities[id];
    return value == null || !value.available ? null : value.isOn;
  }

  double? get temperature => number(AquaEntityIds.temperature);
  double? get ph => number(AquaEntityIds.ph);
  double? get ec => number(AquaEntityIds.ec);
  double? get ldr => number(AquaEntityIds.ldr);
  double? get targetTemperature => number(AquaEntityIds.targetTemperature);
  double? get hysteresis => number(AquaEntityIds.temperatureHysteresis);
  int get alarmFlags => entities[AquaEntityIds.alarms]?.integer ?? 0;
  bool? get safe => binary(AquaEntityIds.controllerSafe);
  bool? get leak => binary(AquaEntityIds.leak);
  bool? get waterLow => binary(AquaEntityIds.waterLow);
  bool? get configValid => binary(AquaEntityIds.configurationValid);

  bool get hasCriticalAlarm => leak == true || waterLow == true;

  bool get stale {
    if (lastUpdated.millisecondsSinceEpoch == 0) {
      return true;
    }
    return DateTime.now().difference(lastUpdated) > const Duration(seconds: 45);
  }

  int get availableEntityCount =>
      entities.values.where((entity) => entity.available).length;

  static List<HistorySample> normalizeHistory(
    Iterable<HistorySample> values, {
    int maximum = 180,
  }) {
    final sorted =
        values.where((point) => point.value.isFinite).toList(growable: false)
          ..sort((a, b) => a.time.compareTo(b.time));
    if (sorted.length <= maximum) {
      return sorted;
    }
    final result = <HistorySample>[];
    final step = sorted.length / maximum;
    for (var index = 0; index < maximum; index++) {
      final sourceIndex = math.min((index * step).floor(), sorted.length - 1);
      result.add(sorted[sourceIndex]);
    }
    return result;
  }
}
