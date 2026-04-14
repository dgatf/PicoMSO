import 'dart:typed_data';

import 'package:picomso/domain/models/device_info.dart';
import 'package:picomso/protocol/protocol_codec.dart';
import 'package:picomso/transport/transport_interface.dart';

/// Provides access to device-level commands:
/// GET_INFO, GET_CAPABILITIES, GET_STATUS.
class DeviceRepository {
  DeviceRepository(this._transport, this._codec);

  final Transport _transport;
  final ProtocolCodec _codec;

  /// Send GET_INFO and decode the response.
  Future<DeviceInfo> getInfo() async {
    final req = _codec.encodeGetInfo();
    await _transport.sendControl(req);
    final resp = await _readResponse();
    return _codec.decodeGetInfoAck(resp);
  }

  /// Send GET_CAPABILITIES and decode the response.
  Future<DeviceCapabilities> getCapabilities() async {
    final req = _codec.encodeGetCapabilities();
    await _transport.sendControl(req);
    final resp = await _readResponse();
    return _codec.decodeGetCapabilitiesAck(resp);
  }

  /// Send GET_STATUS and decode the response.
  Future<DeviceStatus> getStatus() async {
    final req = _codec.encodeGetStatus();
    await _transport.sendControl(req);
    final resp = await _readResponse();
    return _codec.decodeGetStatusAck(resp);
  }

  /// Read a response packet from the device.
  ///
  /// The response arrives on EP6 IN as a single block.
  Future<Uint8List> _readResponse() => _transport.readDataBlock();
}
