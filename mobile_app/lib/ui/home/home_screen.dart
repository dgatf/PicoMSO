import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/controllers/device_controller.dart';
import 'package:picomso/controllers/capture_controller.dart';
import 'package:picomso/domain/models/capture_mode.dart';
import 'package:picomso/ui/shared/widgets/connection_state_chip.dart';
import 'package:picomso/ui/capture_setup/capture_setup_sheet.dart';

/// Entry point screen shown after (or before) device connection.
class HomeScreen extends ConsumerWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final captureState = ref.watch(captureControllerProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('PicoMSO'),
        centerTitle: false,
        actions: [
          const ConnectionStateChip(),
          const SizedBox(width: 8),
          IconButton(
            icon: const Icon(Icons.settings_outlined),
            tooltip: 'Settings',
            onPressed: () => Navigator.of(context).pushNamed('/settings'),
          ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const _LastCaptureCard(),
            const SizedBox(height: 32),
            const Text(
              'New Capture',
              style: TextStyle(
                fontSize: 13,
                fontWeight: FontWeight.w600,
                color: Color(0xFF8B949E),
                letterSpacing: 0.5,
              ),
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: _CaptureTypeButton(
                    icon: Icons.stacked_line_chart,
                    label: 'Logic Capture',
                    description: '16 digital channels',
                    onTap: () => _openSetupSheet(
                      context,
                      ref,
                      CaptureMode.logicOnly,
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: _CaptureTypeButton(
                    icon: Icons.show_chart,
                    label: 'Mixed Signal',
                    description: 'Digital + Analog',
                    onTap: () => _openSetupSheet(
                      context,
                      ref,
                      CaptureMode.mixedSignal,
                    ),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  void _openSetupSheet(
    BuildContext context,
    WidgetRef ref,
    CaptureMode mode,
  ) {
    showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      builder: (_) => ProviderScope(
        parent: ProviderScope.containerOf(context),
        child: CaptureSetupSheet(initialMode: mode),
      ),
    );
  }
}

class _LastCaptureCard extends ConsumerWidget {
  const _LastCaptureCard();

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final session = ref.watch(captureSessionProvider);
    if (session == null) {
      return Card(
        child: SizedBox(
          height: 120,
          child: Center(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: const [
                Icon(Icons.multiline_chart, size: 32, color: Color(0xFF30363D)),
                SizedBox(height: 8),
                Text(
                  'No recent capture',
                  style: TextStyle(color: Color(0xFF8B949E), fontSize: 13),
                ),
              ],
            ),
          ),
        ),
      );
    }

    final s = session;
    return InkWell(
      onTap: () => Navigator.of(context).pushNamed('/waveform'),
      borderRadius: BorderRadius.circular(8),
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              const Icon(Icons.multiline_chart,
                  size: 32, color: Color(0xFF58A6FF)),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      s.request.mode == CaptureMode.logicOnly
                          ? 'Logic Capture'
                          : 'Mixed Signal',
                      style: const TextStyle(fontWeight: FontWeight.w600),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      '${s.request.totalSamples} samples @ '
                      '${_formatRate(s.request.sampleRateHz)}',
                      style: const TextStyle(
                          color: Color(0xFF8B949E), fontSize: 12),
                    ),
                    Text(
                      _formatTime(s.capturedAt),
                      style: const TextStyle(
                          color: Color(0xFF8B949E), fontSize: 11),
                    ),
                  ],
                ),
              ),
              const Icon(Icons.chevron_right, color: Color(0xFF8B949E)),
            ],
          ),
        ),
      ),
    );
  }

  String _formatRate(int hz) {
    if (hz >= 1000000) return '${(hz / 1000000).toStringAsFixed(0)} MHz';
    if (hz >= 1000) return '${(hz / 1000).toStringAsFixed(0)} kHz';
    return '$hz Hz';
  }

  String _formatTime(DateTime t) {
    return '${t.hour.toString().padLeft(2, '0')}:'
        '${t.minute.toString().padLeft(2, '0')}:'
        '${t.second.toString().padLeft(2, '0')}';
  }
}

class _CaptureTypeButton extends StatelessWidget {
  const _CaptureTypeButton({
    required this.icon,
    required this.label,
    required this.description,
    required this.onTap,
  });

  final IconData icon;
  final String label;
  final String description;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(8),
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Icon(icon, color: const Color(0xFF58A6FF), size: 28),
              const SizedBox(height: 12),
              Text(
                label,
                style: const TextStyle(
                    fontWeight: FontWeight.w600, fontSize: 14),
              ),
              const SizedBox(height: 4),
              Text(
                description,
                style: const TextStyle(
                    color: Color(0xFF8B949E), fontSize: 12),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
