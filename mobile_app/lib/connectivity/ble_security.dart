import 'dart:io';

import 'package:flutter/services.dart';

enum BleLinkSecurityPhase { idle, pairing, verifying, secured, failed }

enum BleSecurityFailureCode {
  pairingRejected,
  pairingTimeout,
  bluetoothDisabled,
  permissionDenied,
  invalidDevice,
  pairingBusy,
  pairingFailed,
  handshakeTimeout,
  invalidDeviceInfo,
  insecurePeripheral,
  linkClosed,
}

class BleSecurityException implements Exception {
  const BleSecurityException({
    required this.code,
    required this.message,
    this.cause,
  });

  final BleSecurityFailureCode code;
  final String message;
  final Object? cause;

  String get protocolCode => switch (code) {
    BleSecurityFailureCode.pairingRejected => 'ble_pairing_rejected',
    BleSecurityFailureCode.pairingTimeout => 'ble_pairing_timeout',
    BleSecurityFailureCode.bluetoothDisabled => 'ble_bluetooth_disabled',
    BleSecurityFailureCode.permissionDenied => 'ble_permission_denied',
    BleSecurityFailureCode.invalidDevice => 'ble_invalid_device',
    BleSecurityFailureCode.pairingBusy => 'ble_pairing_busy',
    BleSecurityFailureCode.pairingFailed => 'ble_pairing_failed',
    BleSecurityFailureCode.handshakeTimeout => 'ble_handshake_timeout',
    BleSecurityFailureCode.invalidDeviceInfo => 'ble_invalid_device_info',
    BleSecurityFailureCode.insecurePeripheral => 'ble_insecure_peripheral',
    BleSecurityFailureCode.linkClosed => 'ble_link_closed',
  };

  @override
  String toString() => message;
}

typedef BleBondRequester =
    Future<void> Function({
      required String deviceId,
      required Duration timeout,
    });

/// Starts Android bonding explicitly. On Apple platforms CoreBluetooth starts
/// pairing when the first authenticated characteristic is accessed.
class PlatformBleBondCoordinator {
  PlatformBleBondCoordinator._();

  static const MethodChannel _channel = MethodChannel(
    'pl.cydakwarium/ble_bond',
  );

  static Future<void> ensureBonded({
    required String deviceId,
    required Duration timeout,
  }) async {
    if (!Platform.isAndroid) {
      return;
    }

    try {
      await _channel.invokeMapMethod<String, dynamic>('ensureBonded', {
        'deviceId': deviceId,
        'timeoutMs': timeout.inMilliseconds,
      });
    } on MissingPluginException {
      // A protected GATT read remains the source of truth and can trigger the
      // system pairing flow on builds predating the native helper.
    } on PlatformException catch (error) {
      throw _mapPlatformError(error);
    }
  }

  static BleSecurityException _mapPlatformError(PlatformException error) {
    final code = switch (error.code) {
      'bond_rejected' => BleSecurityFailureCode.pairingRejected,
      'bond_timeout' => BleSecurityFailureCode.pairingTimeout,
      'bluetooth_disabled' => BleSecurityFailureCode.bluetoothDisabled,
      'permission_denied' => BleSecurityFailureCode.permissionDenied,
      'invalid_device_id' => BleSecurityFailureCode.invalidDevice,
      'bond_busy' => BleSecurityFailureCode.pairingBusy,
      _ => BleSecurityFailureCode.pairingFailed,
    };
    final fallback = switch (code) {
      BleSecurityFailureCode.pairingRejected =>
        'Parowanie Bluetooth zostało odrzucone. Spróbuj ponownie i wpisz kod z ekranu sterownika.',
      BleSecurityFailureCode.pairingTimeout =>
        'Upłynął czas parowania Bluetooth. Uruchom parowanie ponownie.',
      BleSecurityFailureCode.bluetoothDisabled =>
        'Bluetooth jest wyłączony. Włącz Bluetooth i spróbuj ponownie.',
      BleSecurityFailureCode.permissionDenied =>
        'Aplikacja nie ma uprawnienia do połączenia Bluetooth.',
      BleSecurityFailureCode.invalidDevice =>
        'System nie rozpoznaje wybranego sterownika Bluetooth.',
      BleSecurityFailureCode.pairingBusy =>
        'Inne parowanie Bluetooth jest już w toku.',
      _ => 'Nie udało się bezpiecznie sparować sterownika Bluetooth.',
    };
    final details = error.message?.trim();
    return BleSecurityException(
      code: code,
      message: details == null || details.isEmpty ? fallback : details,
      cause: error,
    );
  }
}
