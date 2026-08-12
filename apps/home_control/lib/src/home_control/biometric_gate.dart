import 'package:flutter/foundation.dart';
import 'package:local_auth/local_auth.dart';

enum BiometricAvailability { available, unavailable, failed }

enum BiometricAuthorization {
  authorized,
  cancelled,
  unavailable,
  lockedOut,
  failed,
}

abstract interface class BiometricAuthenticator {
  Future<BiometricAvailability> availability();

  Future<BiometricAuthorization> authenticate({
    required String localizedReason,
  });
}

final class DeviceBiometricAuthenticator implements BiometricAuthenticator {
  DeviceBiometricAuthenticator({LocalAuthentication? localAuthentication})
    : _localAuthentication = localAuthentication ?? LocalAuthentication();

  final LocalAuthentication _localAuthentication;

  bool get _platformSupported =>
      !kIsWeb &&
      switch (defaultTargetPlatform) {
        TargetPlatform.android ||
        TargetPlatform.iOS ||
        TargetPlatform.macOS ||
        TargetPlatform.windows => true,
        _ => false,
      };

  @override
  Future<BiometricAvailability> availability() async {
    if (!_platformSupported) return BiometricAvailability.unavailable;
    try {
      if (!await _localAuthentication.canCheckBiometrics) {
        return BiometricAvailability.unavailable;
      }
      final enrolled = await _localAuthentication.getAvailableBiometrics();
      return enrolled.isEmpty
          ? BiometricAvailability.unavailable
          : BiometricAvailability.available;
    } on LocalAuthException catch (error) {
      return _availabilityFor(error.code);
    } on Object {
      return BiometricAvailability.failed;
    }
  }

  @override
  Future<BiometricAuthorization> authenticate({
    required String localizedReason,
  }) async {
    if (!_platformSupported) return BiometricAuthorization.unavailable;
    try {
      final authenticated = await _localAuthentication.authenticate(
        localizedReason: localizedReason,
        biometricOnly: true,
        sensitiveTransaction: true,
        persistAcrossBackgrounding: true,
      );
      return authenticated
          ? BiometricAuthorization.authorized
          : BiometricAuthorization.cancelled;
    } on LocalAuthException catch (error) {
      return _authorizationFor(error.code);
    } on Object {
      return BiometricAuthorization.failed;
    }
  }

  static BiometricAvailability _availabilityFor(LocalAuthExceptionCode code) =>
      switch (code) {
        LocalAuthExceptionCode.noCredentialsSet ||
        LocalAuthExceptionCode.noBiometricsEnrolled ||
        LocalAuthExceptionCode.noBiometricHardware =>
          BiometricAvailability.unavailable,
        _ => BiometricAvailability.failed,
      };

  static BiometricAuthorization _authorizationFor(
    LocalAuthExceptionCode code,
  ) => switch (code) {
    LocalAuthExceptionCode.userCanceled ||
    LocalAuthExceptionCode.timeout ||
    LocalAuthExceptionCode.systemCanceled ||
    LocalAuthExceptionCode.userRequestedFallback =>
      BiometricAuthorization.cancelled,
    LocalAuthExceptionCode.noCredentialsSet ||
    LocalAuthExceptionCode.noBiometricsEnrolled ||
    LocalAuthExceptionCode.noBiometricHardware =>
      BiometricAuthorization.unavailable,
    LocalAuthExceptionCode.temporaryLockout ||
    LocalAuthExceptionCode.biometricLockout => BiometricAuthorization.lockedOut,
    _ => BiometricAuthorization.failed,
  };
}
