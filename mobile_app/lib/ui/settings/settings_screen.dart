import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/controllers/device_controller.dart';

/// Minimal settings screen: theme, defaults, device info.
class SettingsScreen extends ConsumerWidget {
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final conn = ref.watch(deviceControllerProvider);

    return Scaffold(
      appBar: AppBar(title: const Text('Settings')),
      body: ListView(
        children: [
          const _SectionHeader('Device'),
          conn.when(
            loading: () => const ListTile(
              title: Text('Connecting…'),
              leading: SizedBox(
                width: 24,
                height: 24,
                child: CircularProgressIndicator(strokeWidth: 2),
              ),
            ),
            error: (e, _) => ListTile(
              title: const Text('Not connected'),
              subtitle: Text(e.toString()),
              leading: const Icon(Icons.usb_off, color: Colors.red),
            ),
            data: (cs) => cs.isConnected
                ? Column(
                    children: [
                      ListTile(
                        title: const Text('Firmware'),
                        subtitle: Text(cs.deviceInfo?.firmwareId ?? '—'),
                        leading: const Icon(Icons.developer_board),
                      ),
                      ListTile(
                        title: const Text('Protocol'),
                        subtitle: Text(cs.deviceInfo?.versionString ?? '—'),
                        leading: const Icon(Icons.lan),
                      ),
                      if (cs.capabilities != null) ...[
                        ListTile(
                          title: const Text('Max logic rate'),
                          subtitle: Text(_formatHz(cs.capabilities!.maxSampleRateLogicHz)),
                          leading: const Icon(Icons.speed),
                        ),
                        ListTile(
                          title: const Text('Max scope rate'),
                          subtitle: Text(_formatHz(cs.capabilities!.maxSampleRateScopeHz)),
                          leading: const Icon(Icons.speed),
                        ),
                        ListTile(
                          title: const Text('Max logic samples'),
                          subtitle: Text('${cs.capabilities!.maxSamplesLogic}'),
                          leading: const Icon(Icons.memory),
                        ),
                      ],
                      ListTile(
                        title: const Text('Disconnect'),
                        leading: const Icon(Icons.usb_off, color: Colors.red),
                        onTap: () =>
                            ref.read(deviceControllerProvider.notifier).disconnect(),
                      ),
                    ],
                  )
                : ListTile(
                    title: const Text('Connect'),
                    leading: const Icon(Icons.usb, color: Color(0xFF58A6FF)),
                    onTap: () =>
                        ref.read(deviceControllerProvider.notifier).connect(),
                  ),
          ),
          const Divider(),
          const _SectionHeader('About'),
          const ListTile(
            title: Text('PicoMSO'),
            subtitle: Text('Mixed-signal instrument mobile app'),
            leading: Icon(Icons.info_outline),
          ),
        ],
      ),
    );
  }

  String _formatHz(int hz) {
    if (hz >= 1000000) return '${(hz / 1000000).round()} MHz';
    if (hz >= 1000) return '${(hz / 1000).round()} kHz';
    return '$hz Hz';
  }
}

class _SectionHeader extends StatelessWidget {
  const _SectionHeader(this.text);
  final String text;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.fromLTRB(16, 16, 16, 4),
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
}
