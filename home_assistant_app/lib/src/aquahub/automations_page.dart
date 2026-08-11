import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'controller.dart';
import 'domain.dart';

final class HubAutomationsPage extends StatelessWidget {
  const HubAutomationsPage({required this.controller, super.key});

  final HubController controller;

  @override
  Widget build(BuildContext context) {
    final collection = controller.automations;
    final canCreate =
        collection.rules.length < collection.capacity &&
        controller.entities.any((entity) => entity.state != null) &&
        controller.entities.any(
          (entity) => entity.writable && !entity.critical,
        );
    return ListView(
      padding: const EdgeInsets.fromLTRB(20, 16, 20, 36),
      children: <Widget>[
        Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 1040),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: <Widget>[
                Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: <Widget>[
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: <Widget>[
                          Text(
                            'Automatyzacje lokalne',
                            style: Theme.of(context).textTheme.headlineSmall
                                ?.copyWith(fontWeight: FontWeight.w800),
                          ),
                          const SizedBox(height: 6),
                          Text(
                            '${collection.rules.length}/${collection.capacity} reguł · działają w AquaHub bez telefonu i Internetu',
                            style: TextStyle(
                              color: Theme.of(
                                context,
                              ).colorScheme.onSurfaceVariant,
                            ),
                          ),
                        ],
                      ),
                    ),
                    FilledButton.icon(
                      onPressed: canCreate
                          ? () => _openEditor(context, controller, null)
                          : null,
                      icon: const Icon(Icons.add_rounded),
                      label: const Text('Nowa reguła'),
                    ),
                  ],
                ),
                const SizedBox(height: 18),
                if (collection.rules.isEmpty)
                  _AutomationEmpty(canCreate: canCreate)
                else
                  ...collection.rules.map(
                    (rule) => Padding(
                      padding: const EdgeInsets.only(bottom: 12),
                      child: _AutomationCard(
                        controller: controller,
                        rule: rule,
                      ),
                    ),
                  ),
              ],
            ),
          ),
        ),
      ],
    );
  }
}

final class _AutomationEmpty extends StatelessWidget {
  const _AutomationEmpty({required this.canCreate});

  final bool canCreate;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(30),
        child: Column(
          children: <Widget>[
            Icon(
              Icons.account_tree_outlined,
              size: 46,
              color: Theme.of(context).colorScheme.primary,
            ),
            const SizedBox(height: 14),
            Text(
              canCreate
                  ? 'Utwórz pierwszą automatyzację'
                  : 'Potrzebna jest encja pomiarowa i bezpieczna encja sterowalna',
              textAlign: TextAlign.center,
              style: Theme.of(
                context,
              ).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700),
            ),
            const SizedBox(height: 7),
            const Text(
              'Przykład: gdy temperatura wzrośnie powyżej progu, włącz dodatkowe napowietrzanie.',
              textAlign: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }
}

final class _AutomationCard extends StatelessWidget {
  const _AutomationCard({required this.controller, required this.rule});

  final HubController controller;
  final HubAutomationRule rule;

  @override
  Widget build(BuildContext context) {
    final busy = controller.isEditingAutomation(rule.id);
    final trigger = _entityName(controller.entities, rule.trigger.entityId);
    final action = _entityName(controller.entities, rule.action.entityId);
    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(18, 14, 12, 14),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: <Widget>[
            Padding(
              padding: const EdgeInsets.only(top: 6),
              child: CircleAvatar(
                child: Icon(
                  rule.enabled ? Icons.bolt_rounded : Icons.pause_rounded,
                ),
              ),
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: <Widget>[
                  Text(
                    rule.name,
                    style: Theme.of(context).textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  const SizedBox(height: 5),
                  Text(
                    'Gdy $trigger ${_comparisonLabel(rule.trigger.comparison)}${_valueSuffix(rule.trigger.value)}',
                  ),
                  const SizedBox(height: 3),
                  Text(
                    'Wtedy $action → ${_formattedValue(rule.action.value)}',
                    style: TextStyle(
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                    ),
                  ),
                  if (rule.condition != null) ...<Widget>[
                    const SizedBox(height: 3),
                    Text(
                      'Warunek: ${_entityName(controller.entities, rule.condition!.entityId)} ${_comparisonLabel(rule.condition!.comparison)}${_valueSuffix(rule.condition!.value)}',
                      style: TextStyle(
                        color: Theme.of(context).colorScheme.onSurfaceVariant,
                      ),
                    ),
                  ],
                ],
              ),
            ),
            if (busy)
              const Padding(
                padding: EdgeInsets.all(12),
                child: SizedBox.square(
                  dimension: 22,
                  child: CircularProgressIndicator(strokeWidth: 2),
                ),
              )
            else ...<Widget>[
              Switch(
                value: rule.enabled,
                onChanged: (enabled) =>
                    controller.saveAutomation(rule.copyWith(enabled: enabled)),
              ),
              PopupMenuButton<String>(
                tooltip: 'Opcje automatyzacji',
                onSelected: (action) {
                  if (action == 'edit') {
                    _openEditor(context, controller, rule);
                  } else if (action == 'delete') {
                    _deleteRule(context, controller, rule);
                  }
                },
                itemBuilder: (context) => const <PopupMenuEntry<String>>[
                  PopupMenuItem<String>(
                    value: 'edit',
                    child: ListTile(
                      leading: Icon(Icons.edit_outlined),
                      title: Text('Edytuj'),
                    ),
                  ),
                  PopupMenuItem<String>(
                    value: 'delete',
                    child: ListTile(
                      leading: Icon(Icons.delete_outline),
                      title: Text('Usuń'),
                    ),
                  ),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }
}

Future<void> _openEditor(
  BuildContext context,
  HubController controller,
  HubAutomationRule? rule,
) async {
  final hasTrigger = controller.entities.any((entity) => entity.state != null);
  final hasAction = controller.entities.any(
    (entity) => entity.writable && !entity.critical,
  );
  if (!hasTrigger || !hasAction) {
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(
        content: Text(
          'Do edycji potrzebna jest encja pomiarowa i encja sterowalna.',
        ),
      ),
    );
    return;
  }
  await showDialog<void>(
    context: context,
    builder: (context) => Dialog.fullscreen(
      child: _AutomationEditor(controller: controller, initialRule: rule),
    ),
  );
}

Future<void> _deleteRule(
  BuildContext context,
  HubController controller,
  HubAutomationRule rule,
) async {
  final confirmed = await showDialog<bool>(
    context: context,
    builder: (context) => AlertDialog(
      title: const Text('Usunąć automatyzację?'),
      content: Text('Reguła „${rule.name}” przestanie działać natychmiast.'),
      actions: <Widget>[
        TextButton(
          onPressed: () => Navigator.pop(context, false),
          child: const Text('Anuluj'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(context, true),
          child: const Text('Usuń'),
        ),
      ],
    ),
  );
  if (confirmed == true) await controller.deleteAutomation(rule.id);
}

final class _AutomationEditor extends StatefulWidget {
  const _AutomationEditor({required this.controller, this.initialRule});

  final HubController controller;
  final HubAutomationRule? initialRule;

  @override
  State<_AutomationEditor> createState() => _AutomationEditorState();
}

final class _AutomationEditorState extends State<_AutomationEditor> {
  final formKey = GlobalKey<FormState>();
  late final TextEditingController nameController;
  late final TextEditingController triggerValueController;
  late final TextEditingController conditionValueController;
  late final TextEditingController actionValueController;
  late String triggerEntityId;
  late String actionEntityId;
  String? conditionEntityId;
  late HubAutomationComparison triggerComparison;
  HubAutomationComparison conditionComparison = HubAutomationComparison.equals;
  bool conditionEnabled = false;
  bool triggerBool = true;
  bool conditionBool = true;
  bool actionBool = true;
  String? triggerOption;
  String? conditionOption;
  String? actionOption;
  Duration cooldown = const Duration(seconds: 30);
  bool saving = false;

  List<HubEntity> get triggerEntities => widget.controller.entities
      .where((entity) => entity.state != null)
      .toList(growable: false);

  List<HubEntity> get actionEntities => widget.controller.entities
      .where((entity) => entity.writable && !entity.critical)
      .toList(growable: false);

  @override
  void initState() {
    super.initState();
    final rule = widget.initialRule;
    final triggers = triggerEntities;
    final actions = actionEntities;
    final requestedTriggerId = rule?.trigger.entityId;
    final requestedActionId = rule?.action.entityId;
    final requestedConditionId = rule?.condition?.entityId;
    triggerEntityId = triggers.any((entity) => entity.id == requestedTriggerId)
        ? requestedTriggerId!
        : triggers.first.id;
    actionEntityId = actions.any((entity) => entity.id == requestedActionId)
        ? requestedActionId!
        : actions.first.id;
    conditionEntityId =
        triggers.any((entity) => entity.id == requestedConditionId)
        ? requestedConditionId
        : triggers.first.id;
    triggerComparison =
        rule?.trigger.comparison ?? HubAutomationComparison.changed;
    conditionComparison =
        rule?.condition?.comparison ?? HubAutomationComparison.equals;
    conditionEnabled = rule?.condition != null;
    cooldown = rule?.cooldown ?? const Duration(seconds: 30);
    nameController = TextEditingController(text: rule?.name ?? 'Nowa reguła');
    triggerValueController = TextEditingController(
      text: rule?.trigger.value is num
          ? (rule!.trigger.value! as num).toString()
          : rule?.trigger.value is String
          ? rule!.trigger.value! as String
          : '',
    );
    conditionValueController = TextEditingController(
      text: rule?.condition?.value is num
          ? (rule!.condition!.value! as num).toString()
          : rule?.condition?.value is String
          ? rule!.condition!.value! as String
          : '',
    );
    actionValueController = TextEditingController(
      text: rule?.action.value is num
          ? (rule!.action.value as num).toString()
          : rule?.action.value is String
          ? rule!.action.value as String
          : '',
    );
    triggerBool = rule?.trigger.value is bool
        ? rule!.trigger.value! as bool
        : true;
    conditionBool = rule?.condition?.value is bool
        ? rule!.condition!.value! as bool
        : true;
    actionBool = rule?.action.value is bool ? rule!.action.value as bool : true;
    triggerOption = rule?.trigger.value is String
        ? rule!.trigger.value! as String
        : null;
    conditionOption = rule?.condition?.value is String
        ? rule!.condition!.value! as String
        : null;
    actionOption = rule?.action.value is String
        ? rule!.action.value as String
        : null;
  }

  @override
  void dispose() {
    nameController.dispose();
    triggerValueController.dispose();
    conditionValueController.dispose();
    actionValueController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final triggerEntity = _entityById(triggerEntities, triggerEntityId);
    final conditionEntity = _entityById(
      triggerEntities,
      conditionEntityId ?? triggerEntityId,
    );
    final actionEntity = _entityById(actionEntities, actionEntityId);
    return Scaffold(
      appBar: AppBar(
        leading: IconButton(
          onPressed: saving ? null : () => Navigator.pop(context),
          icon: const Icon(Icons.close_rounded),
        ),
        title: Text(
          widget.initialRule == null
              ? 'Nowa automatyzacja'
              : 'Edytuj automatyzację',
        ),
        actions: <Widget>[
          Padding(
            padding: const EdgeInsets.only(right: 12),
            child: FilledButton.icon(
              onPressed: saving ? null : _save,
              icon: saving
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.save_outlined),
              label: const Text('Zapisz'),
            ),
          ),
        ],
      ),
      body: Form(
        key: formKey,
        child: ListView(
          padding: const EdgeInsets.fromLTRB(20, 12, 20, 40),
          children: <Widget>[
            Center(
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 760),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: <Widget>[
                    _EditorSection(
                      number: '1',
                      title: 'Nazwa i zachowanie',
                      child: Column(
                        children: <Widget>[
                          TextFormField(
                            controller: nameController,
                            maxLength: 48,
                            decoration: const InputDecoration(
                              labelText: 'Nazwa reguły',
                              prefixIcon: Icon(Icons.label_outline_rounded),
                            ),
                            validator: (value) =>
                                value == null || value.trim().isEmpty
                                ? 'Podaj nazwę automatyzacji.'
                                : null,
                          ),
                          const SizedBox(height: 12),
                          DropdownButtonFormField<Duration>(
                            initialValue: cooldown,
                            decoration: const InputDecoration(
                              labelText: 'Minimalny odstęp między wykonaniami',
                              prefixIcon: Icon(Icons.timer_outlined),
                            ),
                            items: const <DropdownMenuItem<Duration>>[
                              DropdownMenuItem(
                                value: Duration.zero,
                                child: Text('Bez ograniczenia'),
                              ),
                              DropdownMenuItem(
                                value: Duration(seconds: 5),
                                child: Text('5 sekund'),
                              ),
                              DropdownMenuItem(
                                value: Duration(seconds: 30),
                                child: Text('30 sekund'),
                              ),
                              DropdownMenuItem(
                                value: Duration(minutes: 1),
                                child: Text('1 minuta'),
                              ),
                              DropdownMenuItem(
                                value: Duration(minutes: 5),
                                child: Text('5 minut'),
                              ),
                            ],
                            onChanged: (value) =>
                                setState(() => cooldown = value ?? cooldown),
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(height: 16),
                    _EditorSection(
                      number: '2',
                      title: 'Gdy wystąpi',
                      child: Column(
                        children: <Widget>[
                          _entityDropdown(
                            label: 'Encja wyzwalająca',
                            value: triggerEntityId,
                            entities: triggerEntities,
                            onChanged: (value) {
                              if (value == null) return;
                              setState(() {
                                triggerEntityId = value;
                                triggerComparison =
                                    HubAutomationComparison.changed;
                                triggerValueController.clear();
                                triggerOption = null;
                              });
                            },
                          ),
                          const SizedBox(height: 12),
                          _comparisonDropdown(
                            entity: triggerEntity,
                            value: triggerComparison,
                            condition: false,
                            onChanged: (value) =>
                                setState(() => triggerComparison = value),
                          ),
                          if (triggerComparison !=
                              HubAutomationComparison.changed) ...<Widget>[
                            const SizedBox(height: 12),
                            _valueEditor(
                              entity: triggerEntity,
                              controller: triggerValueController,
                              boolValue: triggerBool,
                              option: triggerOption,
                              onBool: (value) =>
                                  setState(() => triggerBool = value),
                              onOption: (value) =>
                                  setState(() => triggerOption = value),
                            ),
                          ],
                        ],
                      ),
                    ),
                    const SizedBox(height: 16),
                    _EditorSection(
                      number: '3',
                      title: 'Jeżeli dodatkowo',
                      child: Column(
                        children: <Widget>[
                          SwitchListTile(
                            contentPadding: EdgeInsets.zero,
                            title: const Text('Dodaj warunek'),
                            subtitle: const Text(
                              'Akcja wykona się tylko wtedy, gdy warunek jest spełniony.',
                            ),
                            value: conditionEnabled,
                            onChanged: (value) =>
                                setState(() => conditionEnabled = value),
                          ),
                          if (conditionEnabled) ...<Widget>[
                            const SizedBox(height: 8),
                            _entityDropdown(
                              label: 'Encja warunku',
                              value: conditionEntityId ?? triggerEntityId,
                              entities: triggerEntities,
                              onChanged: (value) => setState(() {
                                conditionEntityId = value;
                                conditionComparison =
                                    HubAutomationComparison.equals;
                                conditionValueController.clear();
                                conditionOption = null;
                              }),
                            ),
                            const SizedBox(height: 12),
                            _comparisonDropdown(
                              entity: conditionEntity,
                              value: conditionComparison,
                              condition: true,
                              onChanged: (value) =>
                                  setState(() => conditionComparison = value),
                            ),
                            const SizedBox(height: 12),
                            _valueEditor(
                              entity: conditionEntity,
                              controller: conditionValueController,
                              boolValue: conditionBool,
                              option: conditionOption,
                              onBool: (value) =>
                                  setState(() => conditionBool = value),
                              onOption: (value) =>
                                  setState(() => conditionOption = value),
                            ),
                          ],
                        ],
                      ),
                    ),
                    const SizedBox(height: 16),
                    _EditorSection(
                      number: '4',
                      title: 'Wykonaj akcję',
                      child: Column(
                        children: <Widget>[
                          _entityDropdown(
                            label: 'Encja sterowana',
                            value: actionEntityId,
                            entities: actionEntities,
                            onChanged: (value) {
                              if (value == null) return;
                              setState(() {
                                actionEntityId = value;
                                actionValueController.clear();
                                actionOption = null;
                              });
                            },
                          ),
                          const SizedBox(height: 12),
                          _valueEditor(
                            entity: actionEntity,
                            controller: actionValueController,
                            boolValue: actionBool,
                            option: actionOption,
                            action: true,
                            onBool: (value) =>
                                setState(() => actionBool = value),
                            onOption: (value) =>
                                setState(() => actionOption = value),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _entityDropdown({
    required String label,
    required String value,
    required List<HubEntity> entities,
    required ValueChanged<String?> onChanged,
  }) => DropdownButtonFormField<String>(
    key: ValueKey<String>('$label:$value'),
    initialValue: value,
    isExpanded: true,
    decoration: InputDecoration(
      labelText: label,
      prefixIcon: const Icon(Icons.sensors_rounded),
    ),
    items: entities
        .map(
          (entity) => DropdownMenuItem<String>(
            value: entity.id,
            child: Text(
              entity.name,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
          ),
        )
        .toList(growable: false),
    onChanged: onChanged,
  );

  Widget _comparisonDropdown({
    required HubEntity entity,
    required HubAutomationComparison value,
    required bool condition,
    required ValueChanged<HubAutomationComparison> onChanged,
  }) {
    final values = _comparisonsFor(entity, condition: condition);
    final selected = values.contains(value) ? value : values.first;
    if (selected != value) {
      if (condition) {
        conditionComparison = selected;
      } else {
        triggerComparison = selected;
      }
    }
    return DropdownButtonFormField<HubAutomationComparison>(
      key: ValueKey<String>('comparison:${entity.id}:${selected.wireName}'),
      initialValue: selected,
      decoration: const InputDecoration(
        labelText: 'Porównanie',
        prefixIcon: Icon(Icons.rule_rounded),
      ),
      items: values
          .map(
            (comparison) => DropdownMenuItem<HubAutomationComparison>(
              value: comparison,
              child: Text(_comparisonLabel(comparison)),
            ),
          )
          .toList(growable: false),
      onChanged: (next) {
        if (next != null) onChanged(next);
      },
    );
  }

  Widget _valueEditor({
    required HubEntity entity,
    required TextEditingController controller,
    required bool boolValue,
    required String? option,
    required ValueChanged<bool> onBool,
    required ValueChanged<String?> onOption,
    bool action = false,
  }) {
    if (entity.kind == HubEntityKind.button && action) {
      return const ListTile(
        contentPadding: EdgeInsets.zero,
        leading: Icon(Icons.touch_app_outlined),
        title: Text('Naciśnij przycisk urządzenia'),
      );
    }
    if (entity.kind.isBoolean) {
      return SegmentedButton<bool>(
        segments: const <ButtonSegment<bool>>[
          ButtonSegment<bool>(
            value: false,
            icon: Icon(Icons.power_settings_new_rounded),
            label: Text('Wyłącz'),
          ),
          ButtonSegment<bool>(
            value: true,
            icon: Icon(Icons.check_circle_outline_rounded),
            label: Text('Włącz'),
          ),
        ],
        selected: <bool>{boolValue},
        onSelectionChanged: (values) => onBool(values.first),
      );
    }
    if (entity.kind == HubEntityKind.select && entity.options.isNotEmpty) {
      final selected = entity.options.contains(option)
          ? option
          : entity.options.first;
      return DropdownButtonFormField<String>(
        initialValue: selected,
        decoration: const InputDecoration(
          labelText: 'Wartość',
          prefixIcon: Icon(Icons.list_alt_rounded),
        ),
        items: entity.options
            .map(
              (value) =>
                  DropdownMenuItem<String>(value: value, child: Text(value)),
            )
            .toList(growable: false),
        onChanged: onOption,
      );
    }
    final numeric =
        entity.kind == HubEntityKind.sensor ||
        entity.kind == HubEntityKind.number ||
        entity.state is num;
    return TextFormField(
      controller: controller,
      keyboardType: numeric
          ? const TextInputType.numberWithOptions(decimal: true, signed: true)
          : TextInputType.text,
      inputFormatters: numeric
          ? <TextInputFormatter>[
              FilteringTextInputFormatter.allow(RegExp(r'^-?\d*[.,]?\d*$')),
            ]
          : null,
      decoration: InputDecoration(
        labelText: 'Wartość',
        suffixText: entity.unit.isEmpty ? null : entity.unit,
        prefixIcon: const Icon(Icons.data_object_rounded),
      ),
      validator: (value) {
        if (value == null || value.trim().isEmpty) return 'Podaj wartość.';
        if (numeric && double.tryParse(value.replaceAll(',', '.')) == null) {
          return 'Podaj prawidłową liczbę.';
        }
        return null;
      },
    );
  }

  Future<void> _save() async {
    if (formKey.currentState?.validate() != true) return;
    final triggers = triggerEntities;
    final actions = actionEntities;
    final triggerEntity = _entityById(triggers, triggerEntityId);
    final actionEntity = _entityById(actions, actionEntityId);
    final conditionEntity = _entityById(
      triggers,
      conditionEntityId ?? triggerEntityId,
    );
    final triggerValue = triggerComparison == HubAutomationComparison.changed
        ? null
        : _readValue(
            triggerEntity,
            triggerValueController,
            triggerBool,
            triggerOption,
          );
    final conditionValue = conditionEnabled
        ? _readValue(
            conditionEntity,
            conditionValueController,
            conditionBool,
            conditionOption,
          )
        : null;
    final actionValue = _readValue(
      actionEntity,
      actionValueController,
      actionBool,
      actionOption,
      action: true,
    );
    final id =
        widget.initialRule?.id ??
        'auto_${DateTime.now().millisecondsSinceEpoch.toRadixString(36)}';
    final rule = HubAutomationRule(
      id: id,
      name: nameController.text.trim(),
      enabled: widget.initialRule?.enabled ?? true,
      cooldown: cooldown,
      trigger: HubAutomationClause(
        entityId: triggerEntityId,
        comparison: triggerComparison,
        value: triggerValue,
      ),
      condition: conditionEnabled
          ? HubAutomationClause(
              entityId: conditionEntityId ?? triggerEntityId,
              comparison: conditionComparison,
              value: conditionValue,
            )
          : null,
      action: HubAutomationAction(entityId: actionEntityId, value: actionValue),
    );
    setState(() => saving = true);
    final saved = await widget.controller.saveAutomation(rule);
    if (!mounted) return;
    setState(() => saving = false);
    if (saved) {
      Navigator.pop(context);
    } else {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            widget.controller.errorMessage ??
                'Nie udało się zapisać automatyzacji.',
          ),
        ),
      );
    }
  }
}

final class _EditorSection extends StatelessWidget {
  const _EditorSection({
    required this.number,
    required this.title,
    required this.child,
  });

  final String number;
  final String title;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: <Widget>[
            Row(
              children: <Widget>[
                CircleAvatar(radius: 16, child: Text(number)),
                const SizedBox(width: 10),
                Text(
                  title,
                  style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.w800,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 18),
            child,
          ],
        ),
      ),
    );
  }
}

HubEntity _entityById(List<HubEntity> entities, String id) => entities
    .firstWhere((entity) => entity.id == id, orElse: () => entities.first);

List<HubAutomationComparison> _comparisonsFor(
  HubEntity entity, {
  required bool condition,
}) {
  final values = <HubAutomationComparison>[
    if (!condition) HubAutomationComparison.changed,
    HubAutomationComparison.equals,
  ];
  if (entity.state is num ||
      entity.kind == HubEntityKind.sensor ||
      entity.kind == HubEntityKind.number) {
    values.addAll(const <HubAutomationComparison>[
      HubAutomationComparison.above,
      HubAutomationComparison.below,
    ]);
  }
  return values;
}

Object _readValue(
  HubEntity entity,
  TextEditingController controller,
  bool boolValue,
  String? option, {
  bool action = false,
}) {
  if (entity.kind == HubEntityKind.button && action) return true;
  if (entity.kind.isBoolean) return boolValue;
  if (entity.kind == HubEntityKind.select && entity.options.isNotEmpty) {
    return entity.options.contains(option) ? option! : entity.options.first;
  }
  if (entity.state is num ||
      entity.kind == HubEntityKind.sensor ||
      entity.kind == HubEntityKind.number) {
    return double.parse(controller.text.replaceAll(',', '.'));
  }
  return controller.text.trim();
}

String _entityName(List<HubEntity> entities, String id) =>
    entities.fold<String?>(
      null,
      (found, entity) => found ?? (entity.id == id ? entity.name : null),
    ) ??
    id;

String _comparisonLabel(HubAutomationComparison comparison) =>
    switch (comparison) {
      HubAutomationComparison.changed => 'zmieni stan',
      HubAutomationComparison.equals => 'jest równe',
      HubAutomationComparison.above => 'jest powyżej',
      HubAutomationComparison.below => 'jest poniżej',
    };

String _valueSuffix(Object? value) =>
    value == null ? '' : ' ${_formattedValue(value)}';

String _formattedValue(Object value) => switch (value) {
  true => 'Włączone',
  false => 'Wyłączone',
  num() => _formattedNumber(value),
  _ => value.toString(),
};

String _formattedNumber(num value) => value == value.roundToDouble()
    ? value.toStringAsFixed(0)
    : value.toStringAsFixed(2);
