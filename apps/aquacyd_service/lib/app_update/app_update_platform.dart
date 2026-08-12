import 'package:flutter/services.dart';

import 'app_update_models.dart';

abstract interface class AppUpdatePlatform {
  Future<InstalledAppInfo> getInstallState();

  Future<String> getUpdateDirectory();

  Future<void> openUnknownSourcesSettings();

  Future<void> installApk({
    required String path,
    required String expectedSha256,
    required String expectedVersionName,
  });
}

class MethodChannelAppUpdatePlatform implements AppUpdatePlatform {
  const MethodChannelAppUpdatePlatform();

  static const MethodChannel _channel = MethodChannel(
    'pl.cydakwarium/app_update',
  );

  @override
  Future<InstalledAppInfo> getInstallState() async {
    try {
      final raw = await _channel.invokeMapMethod<Object?, Object?>(
        'getInstallState',
      );
      if (raw == null) {
        throw const AppUpdateException(
          'INVALID_PLATFORM_RESPONSE',
          'Android nie zwrócił informacji o zainstalowanej aplikacji.',
        );
      }
      return InstalledAppInfo(
        packageName: _requiredString(raw, 'packageName'),
        versionName: _requiredString(raw, 'versionName'),
        versionCode: _requiredInt(raw, 'versionCode'),
        sdkInt: _requiredInt(raw, 'sdkInt'),
        canRequestPackageInstalls: _requiredBool(
          raw,
          'canRequestPackageInstalls',
        ),
      );
    } on PlatformException catch (error) {
      throw _mapPlatformException(error);
    } on MissingPluginException catch (error) {
      throw AppUpdateException(
        'UNSUPPORTED_PLATFORM',
        'Aktualizacje APK są dostępne tylko w aplikacji na Androida.',
        error,
      );
    }
  }

  @override
  Future<String> getUpdateDirectory() async {
    try {
      final path = await _channel.invokeMethod<String>('getUpdateDirectory');
      if (path == null || path.trim().isEmpty) {
        throw const AppUpdateException(
          'INVALID_UPDATE_DIRECTORY',
          'Android nie przygotował katalogu aktualizacji.',
        );
      }
      return path;
    } on PlatformException catch (error) {
      throw _mapPlatformException(error);
    } on MissingPluginException catch (error) {
      throw AppUpdateException(
        'UNSUPPORTED_PLATFORM',
        'Aktualizacje APK są dostępne tylko w aplikacji na Androida.',
        error,
      );
    }
  }

  @override
  Future<void> openUnknownSourcesSettings() async {
    try {
      await _channel.invokeMethod<void>('openUnknownSourcesSettings');
    } on PlatformException catch (error) {
      throw _mapPlatformException(error);
    } on MissingPluginException catch (error) {
      throw AppUpdateException(
        'UNSUPPORTED_PLATFORM',
        'Nie można otworzyć ustawień instalowania aplikacji.',
        error,
      );
    }
  }

  @override
  Future<void> installApk({
    required String path,
    required String expectedSha256,
    required String expectedVersionName,
  }) async {
    try {
      await _channel.invokeMapMethod<Object?, Object?>('installApk', {
        'path': path,
        'sha256': expectedSha256,
        'versionName': expectedVersionName,
      });
    } on PlatformException catch (error) {
      throw _mapPlatformException(error);
    } on MissingPluginException catch (error) {
      throw AppUpdateException(
        'UNSUPPORTED_PLATFORM',
        'Instalator aktualizacji jest niedostępny.',
        error,
      );
    }
  }

  static String _requiredString(Map<Object?, Object?> map, String key) {
    final value = map[key];
    if (value is String && value.trim().isNotEmpty) return value.trim();
    throw AppUpdateException(
      'INVALID_PLATFORM_RESPONSE',
      'Android zwrócił nieprawidłowe pole $key.',
    );
  }

  static int _requiredInt(Map<Object?, Object?> map, String key) {
    final value = map[key];
    if (value is int && value >= 0) return value;
    throw AppUpdateException(
      'INVALID_PLATFORM_RESPONSE',
      'Android zwrócił nieprawidłowe pole $key.',
    );
  }

  static bool _requiredBool(Map<Object?, Object?> map, String key) {
    final value = map[key];
    if (value is bool) return value;
    throw AppUpdateException(
      'INVALID_PLATFORM_RESPONSE',
      'Android zwrócił nieprawidłowe pole $key.',
    );
  }

  static AppUpdateException _mapPlatformException(PlatformException error) {
    const messages = <String, String>{
      'INVALID_ARGUMENT': 'Instalator otrzymał nieprawidłowe dane.',
      'PATH_OUTSIDE_UPDATE_DIR':
          'Plik aktualizacji znajduje się poza bezpiecznym katalogiem.',
      'FILE_NOT_FOUND': 'Pobrany plik aktualizacji nie istnieje.',
      'APK_TOO_LARGE': 'Plik aktualizacji ma nieprawidłowy rozmiar.',
      'CHECKSUM_MISMATCH':
          'Suma kontrolna aktualizacji jest nieprawidłowa. Plik został odrzucony.',
      'INVALID_APK': 'Pobrany plik nie jest prawidłową aplikacją AquaCYD.',
      'PACKAGE_MISMATCH':
          'Aktualizacja jest przeznaczona dla innego wariantu aplikacji.',
      'VERSION_NAME_MISMATCH':
          'Wersja wewnątrz APK nie zgadza się z wydaniem GitHub.',
      'DOWNGRADE_NOT_ALLOWED':
          'Pobrana wersja nie jest nowsza od zainstalowanej.',
      'SIGNATURE_MISMATCH':
          'Podpis aktualizacji nie pasuje do aplikacji AquaCYD. Instalacja została zablokowana.',
      'INSTALL_PERMISSION_REQUIRED':
          'Android wymaga zgody na instalowanie aktualizacji z AquaCYD.',
      'NO_PACKAGE_INSTALLER':
          'Na urządzeniu nie znaleziono systemowego instalatora APK.',
      'INSTALL_LAUNCH_FAILED':
          'Nie udało się uruchomić systemowego instalatora.',
      'INSTALL_IN_PROGRESS': 'Instalator już przygotowuje aktualizację.',
      'UPDATE_DIRECTORY_ERROR': 'Nie można przygotować katalogu aktualizacji.',
    };
    return AppUpdateException(
      error.code,
      messages[error.code] ??
          'Android nie mógł przygotować instalacji aktualizacji.',
      error,
    );
  }
}
