import 'dart:typed_data';

/// Abstract USB/transport interface.
///
/// Implementations send framed packets to the device (EP0 vendor OUT control
/// transfers) and stream raw 64-byte data blocks back from the device (EP6
/// bulk IN).
abstract class Transport {
  /// Send [data] as a vendor OUT control transfer (EP0).
  ///
  /// Returns when the transfer is acknowledged by the host controller.
  /// Throws [TransportException] on failure.
  Future<void> sendControl(Uint8List data);

  /// Read one raw data block from the device (EP6 IN, up to 64 bytes).
  ///
  /// Returns the raw bytes of the block payload.
  /// Throws [TransportException] on failure or timeout.
  Future<Uint8List> readDataBlock();

  /// True when the transport layer reports the device is attached and
  /// responsive (i.e. GET_INFO has succeeded at least once).
  bool get isConnected;

  /// Release all resources.
  Future<void> dispose();
}

/// Thrown when a transport-level error occurs (device disconnected, timeout,
/// bulk transfer error, etc.).
class TransportException implements Exception {
  const TransportException(this.message);

  final String message;

  @override
  String toString() => 'TransportException: $message';
}
