import 'dart:async';

final class OperationCancelled implements Exception {
  const OperationCancelled([this.reason = 'Operation cancelled.']);

  final String reason;

  @override
  String toString() => reason;
}

final class CancellationToken {
  final Completer<void> _cancelled = Completer<void>();
  String _reason = 'Operation cancelled.';

  bool get isCancelled => _cancelled.isCompleted;
  Future<void> get whenCancelled => _cancelled.future;

  void cancel([String reason = 'Operation cancelled.']) {
    if (_cancelled.isCompleted) return;
    _reason = reason;
    _cancelled.complete();
  }

  void throwIfCancelled() {
    if (isCancelled) throw OperationCancelled(_reason);
  }

  Future<T> bind<T>(Future<T> operation) {
    throwIfCancelled();
    return Future.any<T>(<Future<T>>[
      operation,
      whenCancelled.then<T>((_) => throw OperationCancelled(_reason)),
    ]);
  }
}
