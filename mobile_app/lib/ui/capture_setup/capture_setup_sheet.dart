import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/controllers/capture_controller.dart';
import 'package:picomso/domain/models/capture_mode.dart';
import 'package:picomso/domain/models/trigger_config.dart';
import 'package:picomso/protocol/capture_request_builder.dart';

/// Bottom sheet for configuring and launching a capture.
class CaptureSetupSheet extends ConsumerStatefulWidget {
  const CaptureSetupSheet({super.key, required this.initialMode});

  final CaptureMode initialMode;

  @override
  ConsumerState<CaptureSetupSheet> createState() => _CaptureSetupSheetState();
}

class _CaptureSetupSheetState extends ConsumerState<CaptureSetupSheet> {
  late CaptureMode _mode;
  int _perChannelSamples = 4096;
  int _sampleRateHz = 1000000;
  double _preTriggerFraction = 0.1;
  int _analogChannelsMask = 0x01;
  List<TriggerConfig> _triggers = [
    const TriggerConfig(isEnabled: false, pin: 0, match: TriggerMatch.edgeHigh),
  ];

  static const _sampleCounts = [1024, 4096, 16384, 65536, 131072];
  static const _sampleRates = [
    100000,   // 100 kHz
    500000,   // 500 kHz
    1000000,  // 1 MHz
    5000000,  // 5 MHz
    10000000, // 10 MHz
    50000000, // 50 MHz
    100000000,// 100 MHz
  ];

  @override
  void initState() {
    super.initState();
    _mode = widget.initialMode;
  }

  @override
  Widget build(BuildContext context) {
    return DraggableScrollableSheet(
      initialChildSize: 0.85,
      minChildSize: 0.4,
      maxChildSize: 0.95,
      expand: false,
      builder: (_, scrollController) => Column(
        children: [
          _SheetHandle(),
          Expanded(
            child: ListView(
              controller: scrollController,
              padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
              children: [
                _SectionTitle('Sample Configuration'),
                _SampleCountSelector(),
                const SizedBox(height: 8),
                _SampleRateSelector(),
                const SizedBox(height: 8),
                _PreTriggerSlider(),
                if (_mode == CaptureMode.mixedSignal) ...[
                  const SizedBox(height: 16),
                  _SectionTitle('Analog Channels'),
                  _AnalogChannelToggles(),
                ],
                const SizedBox(height: 16),
                _SectionTitle('Trigger'),
                ..._triggerRows(),
                const SizedBox(height: 8),
                if (_triggers.length < 4)
                  TextButton.icon(
                    icon: const Icon(Icons.add, size: 16),
                    label: const Text('Add trigger'),
                    onPressed: _addTrigger,
                  ),
                const SizedBox(height: 24),
                _ArmButton(onArm: _arm),
                const SizedBox(height: 16),
              ],
            ),
          ),
        ],
      ),
    );
  }

  // -------------------------------------------------------------------------
  // Sub-widgets built inline to avoid excessive file count
  // -------------------------------------------------------------------------

  Widget _SheetHandle() => Padding(
        padding: const EdgeInsets.symmetric(vertical: 8),
        child: Center(
          child: Container(
            width: 36,
            height: 4,
            decoration: BoxDecoration(
              color: const Color(0xFF30363D),
              borderRadius: BorderRadius.circular(2),
            ),
          ),
        ),
      );

  Widget _SectionTitle(String text) => Padding(
        padding: const EdgeInsets.only(bottom: 8, top: 4),
        child: Text(
          text,
          style: const TextStyle(
            fontSize: 12,
            fontWeight: FontWeight.w600,
            color: Color(0xFF8B949E),
            letterSpacing: 0.5,
          ),
        ),
      );

  Widget _SampleCountSelector() => Row(
        children: [
          const Text('Samples', style: TextStyle(fontSize: 13)),
          const SizedBox(width: 16),
          Expanded(
            child: DropdownButton<int>(
              value: _sampleCounts.contains(_perChannelSamples)
                  ? _perChannelSamples
                  : _sampleCounts.last,
              isExpanded: true,
              items: _sampleCounts.map((v) => DropdownMenuItem(
                    value: v,
                    child: Text(_formatSampleCount(v)),
                  )).toList(),
              onChanged: (v) {
                if (v != null) setState(() => _perChannelSamples = v);
              },
            ),
          ),
        ],
      );

  Widget _SampleRateSelector() => Row(
        children: [
          const Text('Rate', style: TextStyle(fontSize: 13)),
          const SizedBox(width: 16),
          Expanded(
            child: DropdownButton<int>(
              value: _sampleRates.contains(_sampleRateHz)
                  ? _sampleRateHz
                  : _sampleRates[2],
              isExpanded: true,
              items: _sampleRates.map((v) => DropdownMenuItem(
                    value: v,
                    child: Text(_formatRate(v)),
                  )).toList(),
              onChanged: (v) {
                if (v != null) setState(() => _sampleRateHz = v);
              },
            ),
          ),
        ],
      );

  Widget _PreTriggerSlider() => Row(
        children: [
          const Text('Pre-trigger', style: TextStyle(fontSize: 13)),
          Expanded(
            child: Slider(
              value: _preTriggerFraction,
              min: 0,
              max: 0.9,
              divisions: 9,
              label: '${(_preTriggerFraction * 100).round()}%',
              onChanged: (v) => setState(() => _preTriggerFraction = v),
            ),
          ),
          Text(
            '${(_preTriggerFraction * 100).round()}%',
            style: const TextStyle(fontSize: 12, color: Color(0xFF8B949E)),
          ),
        ],
      );

  Widget _AnalogChannelToggles() => Column(
        children: [
          for (int i = 0; i < 3; i++)
            SwitchListTile(
              title: Text('A$i  (GPIO ${26 + i})'),
              value: (_analogChannelsMask >> i) & 1 == 1,
              onChanged: (v) => setState(() {
                if (v) {
                  _analogChannelsMask |= (1 << i);
                } else {
                  // At least one channel must remain enabled.
                  final next = _analogChannelsMask & ~(1 << i);
                  if (next != 0) _analogChannelsMask = next;
                }
              }),
              dense: true,
            ),
        ],
      );

  List<Widget> _triggerRows() => [
        for (int i = 0; i < _triggers.length; i++)
          _TriggerRow(
            index: i,
            config: _triggers[i],
            onChanged: (updated) => setState(() => _triggers[i] = updated),
            onRemove: _triggers.length > 1
                ? () => setState(() => _triggers.removeAt(i))
                : null,
          ),
      ];

  void _addTrigger() {
    if (_triggers.length < 4) {
      setState(() => _triggers.add(
            const TriggerConfig(
              isEnabled: false,
              pin: 0,
              match: TriggerMatch.edgeHigh,
            ),
          ));
    }
  }

  Widget _ArmButton({required VoidCallback onArm}) => SizedBox(
        width: double.infinity,
        child: FilledButton.icon(
          icon: const Icon(Icons.fiber_manual_record),
          label: const Text('Arm'),
          onPressed: onArm,
          style: FilledButton.styleFrom(
            padding: const EdgeInsets.symmetric(vertical: 16),
          ),
        ),
      );

  void _arm() {
    final builder = CaptureRequestBuilder()
      ..mode = _mode
      ..perChannelSamples = _perChannelSamples
      ..sampleRateHz = _sampleRateHz
      ..preTriggerFraction = _preTriggerFraction
      ..triggers = _triggers
      ..analogChannelsMask = _analogChannelsMask;

    final request = builder.build();
    Navigator.of(context).pop(); // close sheet
    ref.read(captureControllerProvider.notifier).arm(request);
    Navigator.of(context).pushNamed('/capture-run');
  }

  String _formatSampleCount(int n) {
    if (n >= 1000) return '${(n / 1000).round()}k';
    return '$n';
  }

  String _formatRate(int hz) {
    if (hz >= 1000000) return '${(hz / 1000000).round()} MHz';
    if (hz >= 1000) return '${(hz / 1000).round()} kHz';
    return '$hz Hz';
  }
}

class _TriggerRow extends StatelessWidget {
  const _TriggerRow({
    required this.index,
    required this.config,
    required this.onChanged,
    this.onRemove,
  });

  final int index;
  final TriggerConfig config;
  final ValueChanged<TriggerConfig> onChanged;
  final VoidCallback? onRemove;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          Checkbox(
            value: config.isEnabled,
            onChanged: (v) =>
                onChanged(config.copyWith(isEnabled: v ?? false)),
          ),
          const SizedBox(width: 4),
          // Pin selector
          SizedBox(
            width: 60,
            child: DropdownButton<int>(
              value: config.pin,
              isExpanded: true,
              items: List.generate(
                16,
                (i) => DropdownMenuItem(value: i, child: Text('D$i')),
              ),
              onChanged: (v) {
                if (v != null) onChanged(config.copyWith(pin: v));
              },
            ),
          ),
          const SizedBox(width: 8),
          // Match selector
          Expanded(
            child: DropdownButton<TriggerMatch>(
              value: config.match,
              isExpanded: true,
              items: TriggerMatch.values.map((m) => DropdownMenuItem(
                    value: m,
                    child: Text(m.displayName, style: const TextStyle(fontSize: 12)),
                  )).toList(),
              onChanged: (v) {
                if (v != null) onChanged(config.copyWith(match: v));
              },
            ),
          ),
          if (onRemove != null)
            IconButton(
              icon: const Icon(Icons.close, size: 16),
              onPressed: onRemove,
              padding: EdgeInsets.zero,
              constraints: const BoxConstraints(minWidth: 32, minHeight: 32),
            ),
        ],
      ),
    );
  }
}
