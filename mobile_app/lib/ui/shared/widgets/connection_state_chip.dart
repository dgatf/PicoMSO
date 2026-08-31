import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/controllers/device_controller.dart';

/// Small chip shown on HomeScreen indicating connection status.
class ConnectionStateChip extends ConsumerWidget {
  const ConnectionStateChip({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final state = ref.watch(deviceControllerProvider);
    return state.when(
      loading: () => _Chip(
        label: 'Connecting…',
        color: Colors.orange,
        icon: Icons.usb,
      ),
      error: (_, __) => _Chip(
        label: 'No device',
        color: Colors.red,
        icon: Icons.usb_off,
      ),
      data: (cs) => _Chip(
        label: cs.isConnected
            ? (cs.deviceInfo?.firmwareId ?? 'Connected')
            : 'No device',
        color: cs.isConnected ? Colors.green : Colors.grey,
        icon: cs.isConnected ? Icons.usb : Icons.usb_off,
      ),
    );
  }
}

class _Chip extends StatelessWidget {
  const _Chip({
    required this.label,
    required this.color,
    required this.icon,
  });

  final String label;
  final Color color;
  final IconData icon;

  @override
  Widget build(BuildContext context) => Chip(
        avatar: Icon(icon, color: color, size: 16),
        label: Text(label, style: TextStyle(color: color, fontSize: 12)),
        backgroundColor: color.withAlpha(26),
        side: BorderSide(color: color.withAlpha(77)),
        padding: const EdgeInsets.symmetric(horizontal: 4),
      );
}
