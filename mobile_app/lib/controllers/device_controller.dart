import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/domain/models/device_info.dart';
import 'package:picomso/protocol/protocol_codec.dart';
import 'package:picomso/repository/device_repository.dart';
import 'package:picomso/transport/usb_transport.dart';

// ---------------------------------------------------------------------------
// Provider definitions
// ---------------------------------------------------------------------------

final _codecProvider = Provider<ProtocolCodec>((ref) => ProtocolCodec());

final _transportProvider = Provider<UsbTransport>((ref) {
  ref.onDispose(() async => UsbTransport.instance.dispose());
  return UsbTransport.instance;
});

final deviceRepositoryProvider = Provider<DeviceRepository>((ref) {
  return DeviceRepository(
    ref.watch(_transportProvider),
    ref.watch(_codecProvider),
  );
});

/// Connection state value object.
class ConnectionState {
  const ConnectionState({
    required this.isConnected,
    this.deviceInfo,
    this.capabilities,
    this.errorMessage,
  });

  final bool isConnected;
  final DeviceInfo? deviceInfo;
  final DeviceCapabilities? capabilities;
  final String? errorMessage;

  bool get hasError => errorMessage != null;
}

/// Notifier that manages the device connection lifecycle.
class DeviceController extends AsyncNotifier<ConnectionState> {
  @override
  Future<ConnectionState> build() async {
    return const ConnectionState(isConnected: false);
  }

  /// Attempt to connect and query device info/capabilities.
  Future<void> connect() async {
    state = const AsyncLoading();
    state = await AsyncValue.guard(() async {
      final transport = ref.read(_transportProvider);
      await transport.open();
      final repo = ref.read(deviceRepositoryProvider);
      final info = await repo.getInfo();
      final caps = await repo.getCapabilities();
      return ConnectionState(
        isConnected: true,
        deviceInfo: info,
        capabilities: caps,
      );
    });
  }

  /// Disconnect from the device.
  Future<void> disconnect() async {
    await ref.read(_transportProvider).dispose();
    state = const AsyncData(ConnectionState(isConnected: false));
  }
}

final deviceControllerProvider =
    AsyncNotifierProvider<DeviceController, ConnectionState>(DeviceController.new);
