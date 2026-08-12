import 'package:aquacyd_design_system/aquacyd_design_system.dart';
import 'package:flutter/material.dart';

import 'controller.dart';
import 'strings.dart';

final class HomeControlOnboarding extends StatelessWidget {
  const HomeControlOnboarding({required this.controller, super.key});

  final HomeControlController controller;

  @override
  Widget build(BuildContext context) => switch (controller.setupStep) {
    HomeSetupStep.sourceSelection => _SourceSelection(controller: controller),
    HomeSetupStep.homeAssistant => _HomeAssistantSetup(controller: controller),
  };
}

final class _SourceSelection extends StatelessWidget {
  const _SourceSelection({required this.controller});

  final HomeControlController controller;

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    return Scaffold(
      body: SafeArea(
        child: Center(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(ProductSpacing.lg),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 920),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: <Widget>[
                  const HomeControlMark(size: 72),
                  const SizedBox(height: ProductSpacing.lg),
                  Text(
                    strings.t('sourceTitle'),
                    style: Theme.of(context).textTheme.displaySmall?.copyWith(
                      fontWeight: FontWeight.w900,
                      letterSpacing: -1.2,
                    ),
                  ),
                  const SizedBox(height: ProductSpacing.sm),
                  ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: 680),
                    child: Text(
                      strings.t('sourceDescription'),
                      style: Theme.of(context).textTheme.bodyLarge?.copyWith(
                        color: Theme.of(context).colorScheme.onSurfaceVariant,
                        height: 1.45,
                      ),
                    ),
                  ),
                  const SizedBox(height: ProductSpacing.xl),
                  LayoutBuilder(
                    builder: (context, constraints) {
                      final columns = constraints.maxWidth >= 760 ? 3 : 1;
                      final width = columns == 1
                          ? constraints.maxWidth
                          : (constraints.maxWidth - 32) / columns;
                      return Wrap(
                        spacing: ProductSpacing.md,
                        runSpacing: ProductSpacing.md,
                        children: <Widget>[
                          SizedBox(
                            width: width,
                            child: _SourceCard(
                              icon: Icons.hub_rounded,
                              title: strings.t('aquaHub'),
                              description: strings.t('aquaHubDescription'),
                              badge: strings.t('recommended'),
                              onTap: controller.beginAquaHubSetup,
                            ),
                          ),
                          SizedBox(
                            width: width,
                            child: _SourceCard(
                              icon: Icons.home_work_rounded,
                              title: strings.t('homeAssistant'),
                              description: strings.t(
                                'homeAssistantDescription',
                              ),
                              onTap: controller.beginHomeAssistantSetup,
                            ),
                          ),
                          SizedBox(
                            width: width,
                            child: _SourceCard(
                              icon: Icons.science_rounded,
                              title: strings.t('demo'),
                              description: strings.t('demoDescription'),
                              onTap: controller.selectDemo,
                            ),
                          ),
                        ],
                      );
                    },
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

final class _SourceCard extends StatelessWidget {
  const _SourceCard({
    required this.icon,
    required this.title,
    required this.description,
    required this.onTap,
    this.badge,
  });

  final IconData icon;
  final String title;
  final String description;
  final VoidCallback onTap;
  final String? badge;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Semantics(
      button: true,
      label: '$title. $description',
      child: Card(
        clipBehavior: Clip.antiAlias,
        child: InkWell(
          onTap: onTap,
          child: ConstrainedBox(
            constraints: const BoxConstraints(minHeight: 230),
            child: Padding(
              padding: const EdgeInsets.all(ProductSpacing.lg),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: <Widget>[
                  Row(
                    children: <Widget>[
                      Container(
                        width: 54,
                        height: 54,
                        decoration: BoxDecoration(
                          color: scheme.primaryContainer,
                          borderRadius: BorderRadius.circular(17),
                        ),
                        child: Icon(icon, color: scheme.onPrimaryContainer),
                      ),
                      const Spacer(),
                      if (badge != null)
                        Chip(
                          visualDensity: VisualDensity.compact,
                          label: Text(badge!),
                        ),
                    ],
                  ),
                  const SizedBox(height: ProductSpacing.lg),
                  Text(
                    title,
                    style: Theme.of(context).textTheme.titleLarge?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  const SizedBox(height: ProductSpacing.xs),
                  Text(
                    description,
                    style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                      color: scheme.onSurfaceVariant,
                      height: 1.4,
                    ),
                  ),
                  const SizedBox(height: ProductSpacing.lg),
                  Align(
                    alignment: Alignment.centerRight,
                    child: Icon(
                      Icons.arrow_forward_rounded,
                      color: scheme.primary,
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

final class _HomeAssistantSetup extends StatefulWidget {
  const _HomeAssistantSetup({required this.controller});

  final HomeControlController controller;

  @override
  State<_HomeAssistantSetup> createState() => _HomeAssistantSetupState();
}

final class _HomeAssistantSetupState extends State<_HomeAssistantSetup> {
  final _formKey = GlobalKey<FormState>();
  final _url = TextEditingController();
  final _token = TextEditingController();
  bool _obscure = true;

  @override
  void dispose() {
    _url.dispose();
    _token.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final strings = HomeControlStrings.of(context);
    final scheme = Theme.of(context).colorScheme;
    return Scaffold(
      appBar: AppBar(
        leading: IconButton(
          tooltip: strings.t('back'),
          onPressed: widget.controller.cancelSetup,
          icon: const Icon(Icons.arrow_back_rounded),
        ),
        title: Text(strings.t('connectHa')),
      ),
      body: SafeArea(
        child: Center(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(ProductSpacing.lg),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 620),
              child: Form(
                key: _formKey,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: <Widget>[
                    const HomeControlMark(size: 66),
                    const SizedBox(height: ProductSpacing.lg),
                    Text(
                      strings.t('connectHa'),
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.headlineMedium
                          ?.copyWith(fontWeight: FontWeight.w900),
                    ),
                    const SizedBox(height: ProductSpacing.lg),
                    TextFormField(
                      controller: _url,
                      keyboardType: TextInputType.url,
                      autofillHints: const <String>[AutofillHints.url],
                      autocorrect: false,
                      textInputAction: TextInputAction.next,
                      decoration: InputDecoration(
                        labelText: strings.t('haUrl'),
                        hintText: strings.t('haUrlHint'),
                        prefixIcon: const Icon(Icons.dns_rounded),
                      ),
                      validator: (value) {
                        final uri = Uri.tryParse(value?.trim() ?? '');
                        if (uri == null ||
                            !uri.hasAuthority ||
                            (uri.scheme != 'http' && uri.scheme != 'https')) {
                          return strings.t('errorInvalidCredentials');
                        }
                        return null;
                      },
                    ),
                    const SizedBox(height: ProductSpacing.md),
                    TextFormField(
                      controller: _token,
                      obscureText: _obscure,
                      autocorrect: false,
                      enableSuggestions: false,
                      autofillHints: const <String>[AutofillHints.password],
                      textInputAction: TextInputAction.done,
                      onFieldSubmitted: (_) => _submit(),
                      decoration: InputDecoration(
                        labelText: strings.t('haToken'),
                        prefixIcon: const Icon(Icons.key_rounded),
                        suffixIcon: IconButton(
                          tooltip: strings.t(
                            _obscure ? 'showToken' : 'hideToken',
                          ),
                          onPressed: () => setState(() => _obscure = !_obscure),
                          icon: Icon(
                            _obscure
                                ? Icons.visibility_outlined
                                : Icons.visibility_off_outlined,
                          ),
                        ),
                      ),
                      validator: (value) => (value?.trim().length ?? 0) < 20
                          ? strings.t('errorInvalidCredentials')
                          : null,
                    ),
                    const SizedBox(height: ProductSpacing.md),
                    _InfoPanel(
                      icon: Icons.shield_outlined,
                      text: strings.t('secureStorageHint'),
                    ),
                    const SizedBox(height: ProductSpacing.sm),
                    _InfoPanel(
                      icon: Icons.account_tree_outlined,
                      text: strings.t('oauthHint'),
                    ),
                    if (widget.controller.failure
                        case final failure?) ...<Widget>[
                      const SizedBox(height: ProductSpacing.md),
                      Material(
                        color: scheme.errorContainer,
                        borderRadius: BorderRadius.circular(ProductRadius.card),
                        child: Padding(
                          padding: const EdgeInsets.all(ProductSpacing.md),
                          child: Row(
                            children: <Widget>[
                              Icon(
                                Icons.error_outline_rounded,
                                color: scheme.onErrorContainer,
                              ),
                              const SizedBox(width: ProductSpacing.sm),
                              Expanded(
                                child: Text(
                                  strings.t(failure.messageKey),
                                  style: TextStyle(
                                    color: scheme.onErrorContainer,
                                  ),
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                    ],
                    const SizedBox(height: ProductSpacing.lg),
                    FilledButton.icon(
                      onPressed: _submit,
                      icon: const Icon(Icons.verified_user_rounded),
                      label: Text(strings.t('testAndSave')),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }

  Future<void> _submit() async {
    FocusManager.instance.primaryFocus?.unfocus();
    if (!(_formKey.currentState?.validate() ?? false)) return;
    await widget.controller.configureHomeAssistant(
      baseUrl: _url.text,
      accessToken: _token.text,
    );
  }
}

final class _InfoPanel extends StatelessWidget {
  const _InfoPanel({required this.icon, required this.text});

  final IconData icon;
  final String text;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      padding: const EdgeInsets.all(ProductSpacing.md),
      decoration: BoxDecoration(
        color: scheme.secondaryContainer.withValues(alpha: 0.45),
        borderRadius: BorderRadius.circular(ProductRadius.card),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          Icon(icon, color: scheme.onSecondaryContainer),
          const SizedBox(width: ProductSpacing.sm),
          Expanded(
            child: Text(
              text,
              style: TextStyle(color: scheme.onSecondaryContainer, height: 1.4),
            ),
          ),
        ],
      ),
    );
  }
}
