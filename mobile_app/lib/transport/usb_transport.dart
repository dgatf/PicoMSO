import 'dart:typed_data';

import 'package:picomso/transport/transport_interface.dart';

/// USB transport implementation.
///
/// This class wraps platform-specific USB communication.  On Android the
/// recommended underlying library is `usb_serial` (or a custom platform
/// channel) that provides EP0 vendor control transfers and EP6 bulk IN reads.
///
/// The current implementation is a **stub** that throws [UnimplementedError].
/// Replace [sendControl] and [readDataBlock] with real USB calls once the
/// platform channel is integrated.
class UsbTransport implements Transport {
  UsbTransport._();

  static UsbTransport? _instance;

  /// Returns the singleton transport, creating it if necessary.
  static UsbTransport get instance {
    _instance ??= UsbTransport._();
    return _instance!;
  }

  bool _connected = false;

  @override
  bool get isConnected => _connected;

  /// Attempt to open the first attached PicoMSO device.
  ///
  /// Sets [isConnected] to true on success.
  Future<void> open() async {
    // TODO: Use a platform channel to open the USB device with VID/PID
    // PicoMSO VID = 0x2E8A (Raspberry Pi), PID = device-specific.
    // Claim interface 0, set bConfigurationValue=1.
    _connected = true;
  }

  @override
  Future<void> sendControl(Uint8List data) async {
    if (!_connected) throw const TransportException('Not connected');
    // TODO: Send as bmRequestType=0x40 (vendor OUT), bRequest=0x01,
    // wValue=0, wIndex=0, data=data via platform channel.
    throw UnimplementedError('USB control transfer not implemented');
  }

  @override
  Future<Uint8List> readDataBlock() async {
    if (!_connected) throw const TransportException('Not connected');
    // TODO: Issue a bulk IN transfer on EP6 with a 64-byte buffer.
    // Return the received bytes (may be shorter on the terminal block).
    throw UnimplementedError('USB bulk read not implemented');
  }

  @override
  Future<void> dispose() async {
    _connected = false;
    _instance = null;
  }
}
