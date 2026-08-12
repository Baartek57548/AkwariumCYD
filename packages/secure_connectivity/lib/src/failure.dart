enum AppFailureCode {
  cancelled,
  offline,
  timeout,
  tls,
  certificateChanged,
  authentication,
  permission,
  notFound,
  conflict,
  rateLimited,
  server,
  invalidResponse,
  unsupported,
  storage,
  unknown,
}

final class AppFailure implements Exception {
  const AppFailure({
    required this.code,
    required this.messageKey,
    this.statusCode,
    this.retryAfter,
    this.safeDetails,
  });

  final AppFailureCode code;
  final String messageKey;
  final int? statusCode;
  final Duration? retryAfter;
  final String? safeDetails;

  bool get retryable => switch (code) {
    AppFailureCode.offline ||
    AppFailureCode.timeout ||
    AppFailureCode.rateLimited ||
    AppFailureCode.server => true,
    _ => false,
  };

  @override
  String toString() => 'AppFailure($code, $messageKey)';
}

sealed class AppResult<T> {
  const AppResult();

  R fold<R>({
    required R Function(T value) success,
    required R Function(AppFailure failure) failure,
  }) => switch (this) {
    AppSuccess<T>(:final value) => success(value),
    AppFailureResult<T>(failure: final error) => failure(error),
  };
}

final class AppSuccess<T> extends AppResult<T> {
  const AppSuccess(this.value);

  final T value;
}

final class AppFailureResult<T> extends AppResult<T> {
  const AppFailureResult(this.failure);

  final AppFailure failure;
}
