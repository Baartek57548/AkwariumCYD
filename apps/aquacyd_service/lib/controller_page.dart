import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:webview_flutter/webview_flutter.dart';
import 'package:webview_flutter_android/webview_flutter_android.dart';

import 'controller_address.dart';
import 'controller_preferences.dart';

class ControllerPage extends StatefulWidget {
  const ControllerPage({super.key, this.preferences});

  final ControllerPreferences? preferences;

  @override
  State<ControllerPage> createState() => _ControllerPageState();
}

class _ControllerPageState extends State<ControllerPage> {
  static const int _maximumDownloadBytes = 64 * 1024 * 1024;

  late final ControllerPreferences _preferences;
  WebViewController? _webViewController;
  Uri? _controllerAddress;
  String? _startupError;
  String? _pageError;
  int _loadingProgress = 0;
  Brightness? _themeBrightness;

  bool get _isLoading => _loadingProgress < 100 && _pageError == null;

  @override
  void initState() {
    super.initState();
    _preferences = widget.preferences ?? ControllerPreferences();
    _initialize();
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final brightness = Theme.of(context).brightness;
    if (_themeBrightness == brightness) return;
    _themeBrightness = brightness;
    final controller = _webViewController;
    if (controller != null) {
      unawaited(controller.setBackgroundColor(_webBackground(brightness)));
    }
  }

  Future<void> _initialize() async {
    try {
      final address = await _preferences.loadAddress();
      final controller = _createWebViewController(address);
      if (!mounted) {
        return;
      }
      setState(() {
        _controllerAddress = address;
        _webViewController = controller;
        _startupError = null;
      });
      await controller.loadRequest(address);
    } catch (error) {
      if (!mounted) {
        return;
      }
      setState(() {
        _startupError = 'Nie udało się uruchomić panelu: $error';
      });
    }
  }

  WebViewController _createWebViewController(Uri address) {
    final brightness =
        _themeBrightness ??
        WidgetsBinding.instance.platformDispatcher.platformBrightness;
    final controller = WebViewController();
    controller
      ..setJavaScriptMode(JavaScriptMode.unrestricted)
      ..setBackgroundColor(_webBackground(brightness))
      ..enableZoom(true)
      ..addJavaScriptChannel(
        'MobileDownloads',
        onMessageReceived: _saveBlobDownload,
      )
      ..setNavigationDelegate(
        NavigationDelegate(
          onProgress: (progress) {
            if (mounted) {
              setState(() => _loadingProgress = progress);
            }
          },
          onPageStarted: (_) {
            if (mounted) {
              setState(() {
                _pageError = null;
                _loadingProgress = 0;
              });
            }
          },
          onPageFinished: (_) {
            if (mounted) {
              setState(() => _loadingProgress = 100);
            }
            unawaited(_installBlobDownloadBridge(controller));
          },
          onWebResourceError: (error) {
            if (error.isForMainFrame != true || !mounted) {
              return;
            }
            setState(() {
              _pageError = _friendlyWebError(error);
              _loadingProgress = 100;
            });
          },
          onNavigationRequest: (request) {
            final target = Uri.tryParse(request.url);
            if (target != null && _isBlobFromController(target, address)) {
              return NavigationDecision.navigate;
            }
            if (target == null ||
                !ControllerAddress.isSameController(target, address)) {
              _showMessage('Zablokowano przejście poza panel sterownika.');
              return NavigationDecision.prevent;
            }
            if (_isDownloadEndpoint(target)) {
              unawaited(_downloadFromController(target));
              return NavigationDecision.prevent;
            }
            return NavigationDecision.navigate;
          },
        ),
      );

    final platformController = controller.platform;
    if (platformController is AndroidWebViewController) {
      platformController.setMediaPlaybackRequiresUserGesture(false);
      platformController.setOnShowFileSelector(_pickFiles);
      if (kDebugMode) {
        AndroidWebViewController.enableDebugging(true);
      }
    }
    return controller;
  }

  Color _webBackground(Brightness brightness) => brightness == Brightness.dark
      ? const Color(0xFF080909)
      : const Color(0xFFF1F4F4);

  bool _isBlobFromController(Uri target, Uri controller) {
    if (target.scheme != 'blob') {
      return false;
    }
    final nested = Uri.tryParse(target.path);
    return nested != null &&
        ControllerAddress.isSameController(nested, controller);
  }

  bool _isDownloadEndpoint(Uri target) {
    return target.path == '/download' || target.path == '/history.csv';
  }

  Future<void> _installBlobDownloadBridge(WebViewController controller) async {
    try {
      await controller.runJavaScript(r'''
        (() => {
          if (window.__cydMobileDownloadBridgeInstalled) return;
          window.__cydMobileDownloadBridgeInstalled = true;
          document.addEventListener('click', async (event) => {
            const anchor = event.target instanceof Element
              ? event.target.closest('a[download]')
              : null;
            if (!anchor || !anchor.href.startsWith('blob:')) return;
            event.preventDefault();
            try {
              const response = await fetch(anchor.href);
              const blob = await response.blob();
              const reader = new FileReader();
              reader.onloadend = () => {
                const encoded = String(reader.result || '').split(',', 2)[1] || '';
                MobileDownloads.postMessage(JSON.stringify({
                  name: anchor.download || 'cydAkwarium-export.csv',
                  data: encoded
                }));
              };
              reader.readAsDataURL(blob);
            } catch (error) {
              MobileDownloads.postMessage(JSON.stringify({
                error: String(error)
              }));
            }
          }, true);
        })();
      ''');
    } catch (error) {
      if (mounted) {
        _showMessage('Nie udało się włączyć eksportu plików: $error');
      }
    }
  }

  Future<void> _saveBlobDownload(JavaScriptMessage message) async {
    try {
      final payload = jsonDecode(message.message);
      if (payload is! Map<String, dynamic>) {
        throw const FormatException('Nieprawidłowa odpowiedź panelu.');
      }
      final webError = payload['error'];
      if (webError is String && webError.isNotEmpty) {
        throw StateError(webError);
      }
      final encoded = payload['data'];
      if (encoded is! String || encoded.isEmpty) {
        throw const FormatException('Panel nie zwrócił zawartości pliku.');
      }
      if (encoded.length > (_maximumDownloadBytes * 4 ~/ 3) + 4) {
        throw const FormatException('Plik przekracza limit 64 MB.');
      }
      final bytes = base64Decode(encoded);
      await _saveBytes(bytes, _safeFileName(payload['name'] as String?));
    } catch (error) {
      if (mounted) {
        _showMessage('Nie udało się zapisać eksportu: $error');
      }
    }
  }

  Future<void> _downloadFromController(Uri uri) async {
    final client = HttpClient()
      ..connectionTimeout = const Duration(seconds: 10)
      ..idleTimeout = const Duration(seconds: 20);
    try {
      _showMessage('Pobieranie pliku…');
      final request = await client.getUrl(uri);
      request.headers.set(HttpHeaders.acceptHeader, '*/*');
      final response = await request.close();
      if (response.statusCode < 200 || response.statusCode >= 300) {
        throw HttpException(
          'Sterownik zwrócił HTTP ${response.statusCode}.',
          uri: uri,
        );
      }
      final builder = BytesBuilder(copy: false);
      var length = 0;
      await for (final chunk in response) {
        length += chunk.length;
        if (length > _maximumDownloadBytes) {
          throw const FormatException('Plik przekracza limit 64 MB.');
        }
        builder.add(chunk);
      }
      final disposition = response.headers.value('content-disposition');
      await _saveBytes(
        builder.takeBytes(),
        _fileNameFromResponse(disposition, uri),
      );
    } catch (error) {
      if (mounted) {
        _showMessage('Nie udało się pobrać pliku: $error');
      }
    } finally {
      client.close(force: true);
    }
  }

  String _fileNameFromResponse(String? contentDisposition, Uri uri) {
    if (contentDisposition != null) {
      final match = RegExp(
        r'''filename\*?=(?:UTF-8''|\")?([^\";]+)''',
        caseSensitive: false,
      ).firstMatch(contentDisposition);
      if (match != null) {
        return _safeFileName(Uri.decodeComponent(match.group(1)!.trim()));
      }
    }
    final requestedPath = uri.queryParameters['path'];
    if (requestedPath != null && requestedPath.isNotEmpty) {
      return _safeFileName(Uri.parse(requestedPath).pathSegments.lastOrNull);
    }
    return 'cydAkwarium-history.csv';
  }

  String _safeFileName(String? input) {
    final sanitized = (input ?? '')
        .replaceAll(RegExp(r'[\\/:*?"<>|\x00-\x1F]'), '_')
        .trim();
    if (sanitized.isEmpty || sanitized == '.' || sanitized == '..') {
      return 'cydAkwarium-export.bin';
    }
    return sanitized.length <= 120 ? sanitized : sanitized.substring(0, 120);
  }

  Future<void> _saveBytes(Uint8List bytes, String fileName) async {
    final savedPath = await FilePicker.saveFile(
      dialogTitle: 'Zapisz plik cydAkwarium',
      fileName: fileName,
      bytes: bytes,
    );
    if (mounted && savedPath != null) {
      _showMessage('Plik został zapisany.');
    }
  }

  Future<List<String>> _pickFiles(FileSelectorParams params) async {
    try {
      final result = await FilePicker.pickFiles(
        allowMultiple: params.mode == FileSelectorMode.openMultiple,
        withData: false,
      );
      if (result == null) {
        return const [];
      }
      return result.paths
          .whereType<String>()
          .where((path) => path.isNotEmpty)
          .map((path) => Uri.file(path).toString())
          .toList(growable: false);
    } catch (error) {
      if (mounted) {
        _showMessage('Nie udało się otworzyć pliku: $error');
      }
      return const [];
    }
  }

  String _friendlyWebError(WebResourceError error) {
    final description = error.description.trim();
    if (description.isEmpty) {
      return 'Sterownik jest niedostępny. Sprawdź Wi-Fi i adres urządzenia.';
    }
    return 'Sterownik jest niedostępny. $description';
  }

  void _showMessage(String message) {
    final messenger = ScaffoldMessenger.maybeOf(context);
    if (messenger == null) {
      return;
    }
    messenger
      ..hideCurrentSnackBar()
      ..showSnackBar(SnackBar(content: Text(message)));
  }

  Future<void> _reload() async {
    final controller = _webViewController;
    if (controller == null) {
      await _initialize();
      return;
    }
    setState(() {
      _pageError = null;
      _loadingProgress = 0;
    });
    await controller.reload();
  }

  Future<void> _changeAddress() async {
    final currentAddress =
        _controllerAddress ?? Uri.parse(ControllerAddress.defaultValue);
    final updatedAddress = await showDialog<Uri>(
      context: context,
      builder: (context) => _AddressDialog(initialAddress: currentAddress),
    );
    if (updatedAddress == null || updatedAddress == currentAddress) {
      return;
    }

    try {
      await _preferences.saveAddress(updatedAddress);
      final controller = _createWebViewController(updatedAddress);
      if (!mounted) {
        return;
      }
      setState(() {
        _controllerAddress = updatedAddress;
        _webViewController = controller;
        _pageError = null;
        _loadingProgress = 0;
      });
      await controller.loadRequest(updatedAddress);
    } catch (error) {
      if (mounted) {
        _showMessage('Nie udało się zapisać adresu: $error');
      }
    }
  }

  Future<void> _handleBack() async {
    final controller = _webViewController;
    if (controller != null && await controller.canGoBack()) {
      await controller.goBack();
      return;
    }
    if (mounted) {
      Navigator.of(context).pop();
    }
  }

  @override
  Widget build(BuildContext context) {
    final controller = _webViewController;
    return PopScope(
      canPop: false,
      onPopInvokedWithResult: (didPop, _) {
        if (!didPop) {
          _handleBack();
        }
      },
      child: Scaffold(
        body: SafeArea(
          child: Stack(
            children: [
              if (controller != null) WebViewWidget(controller: controller),
              if (controller == null && _startupError == null)
                const Center(child: CircularProgressIndicator()),
              if (_startupError != null)
                _FailureView(
                  message: _startupError!,
                  onRetry: _initialize,
                  onChangeAddress: _changeAddress,
                ),
              if (_pageError != null)
                _FailureView(
                  message: _pageError!,
                  onRetry: _reload,
                  onChangeAddress: _changeAddress,
                ),
              if (_isLoading)
                Align(
                  alignment: Alignment.topCenter,
                  child: LinearProgressIndicator(value: _loadingProgress / 100),
                ),
              if (_startupError == null)
                Positioned(
                  right: 10,
                  bottom: 12,
                  child: _ControllerMenu(
                    onReload: _reload,
                    onChangeAddress: _changeAddress,
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

class _ControllerMenu extends StatelessWidget {
  const _ControllerMenu({
    required this.onReload,
    required this.onChangeAddress,
  });

  final VoidCallback onReload;
  final VoidCallback onChangeAddress;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Theme.of(context).colorScheme.surface.withValues(alpha: 0.92),
      elevation: 4,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.all(Radius.circular(5)),
      ),
      child: PopupMenuButton<_MenuAction>(
        tooltip: 'Opcje połączenia',
        icon: const Icon(Icons.more_horiz),
        onSelected: (action) {
          switch (action) {
            case _MenuAction.reload:
              onReload();
            case _MenuAction.changeAddress:
              onChangeAddress();
          }
        },
        itemBuilder: (context) => const [
          PopupMenuItem(
            value: _MenuAction.reload,
            child: ListTile(
              leading: Icon(Icons.refresh),
              title: Text('Odśwież panel'),
              contentPadding: EdgeInsets.zero,
            ),
          ),
          PopupMenuItem(
            value: _MenuAction.changeAddress,
            child: ListTile(
              leading: Icon(Icons.router_outlined),
              title: Text('Adres sterownika'),
              contentPadding: EdgeInsets.zero,
            ),
          ),
        ],
      ),
    );
  }
}

enum _MenuAction { reload, changeAddress }

class _FailureView extends StatelessWidget {
  const _FailureView({
    required this.message,
    required this.onRetry,
    required this.onChangeAddress,
  });

  final String message;
  final VoidCallback onRetry;
  final VoidCallback onChangeAddress;

  @override
  Widget build(BuildContext context) {
    return ColoredBox(
      color: Theme.of(context).colorScheme.surface,
      child: Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(28),
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 460),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(
                  Icons.wifi_off_rounded,
                  size: 64,
                  color: Theme.of(context).colorScheme.error,
                ),
                const SizedBox(height: 20),
                Text(
                  'Brak połączenia z cydAkwarium',
                  style: Theme.of(context).textTheme.headlineSmall,
                  textAlign: TextAlign.center,
                ),
                const SizedBox(height: 12),
                Text(message, textAlign: TextAlign.center),
                const SizedBox(height: 24),
                FilledButton.icon(
                  onPressed: onRetry,
                  icon: const Icon(Icons.refresh),
                  label: const Text('Spróbuj ponownie'),
                ),
                TextButton.icon(
                  onPressed: onChangeAddress,
                  icon: const Icon(Icons.router_outlined),
                  label: const Text('Zmień adres sterownika'),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _AddressDialog extends StatefulWidget {
  const _AddressDialog({required this.initialAddress});

  final Uri initialAddress;

  @override
  State<_AddressDialog> createState() => _AddressDialogState();
}

class _AddressDialogState extends State<_AddressDialog> {
  late final TextEditingController _controller;
  String? _errorText;

  @override
  void initState() {
    super.initState();
    _controller = TextEditingController(text: widget.initialAddress.toString());
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  void _submit() {
    try {
      final address = ControllerAddress.parse(_controller.text);
      Navigator.of(context).pop(address);
    } on FormatException catch (error) {
      setState(() => _errorText = error.message);
    }
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('Adres sterownika'),
      content: TextField(
        controller: _controller,
        autofocus: true,
        keyboardType: TextInputType.url,
        autocorrect: false,
        enableSuggestions: false,
        decoration: InputDecoration(
          labelText: 'URL lub adres IP',
          hintText: ControllerAddress.defaultValue,
          helperText: 'Telefon i sterownik muszą być w tej samej sieci Wi-Fi.',
          errorText: _errorText,
        ),
        onSubmitted: (_) => _submit(),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Anuluj'),
        ),
        FilledButton(onPressed: _submit, child: const Text('Połącz')),
      ],
    );
  }
}
