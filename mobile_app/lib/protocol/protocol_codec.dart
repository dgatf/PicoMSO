import 'dart:typed_data';

import 'package:picomso/domain/models/device_info.dart';
import 'package:picomso/domain/models/capture_request.dart';
import 'package:picomso/domain/models/trigger_config.dart';
import 'package:picomso/protocol/protocol_constants.dart';

/// Encodes outgoing packets and decodes incoming packets.
///
/// All multi-byte values are little-endian, mirroring the C
/// __attribute__((packed)) structs in protocol.h and protocol_packets.h.
///
/// This class is stateless except for [_seq], which monotonically increments
/// to provide sequence numbers.
class ProtocolCodec {
  int _seq = 0;

  int _nextSeq() => _seq = (_seq + 1) & 0xFF;

  // -------------------------------------------------------------------------
  // Packet builder helpers
  // -------------------------------------------------------------------------

  /// Write an 8-byte packet header at [offset] in [bd].
  void _writeHeader(
    ByteData bd,
    int offset,
    int msgType,
    int seq,
    int payloadLen,
  ) {
    bd.setUint16(offset + 0, kPacketMagic, Endian.little);
    bd.setUint8(offset + 2, kProtocolVersionMajor);
    bd.setUint8(offset + 3, kProtocolVersionMinor);
    bd.setUint8(offset + 4, msgType);
    bd.setUint8(offset + 5, seq);
    bd.setUint16(offset + 6, payloadLen, Endian.little);
  }

  Uint8List _headerOnly(int msgType) {
    final buf = ByteData(kPacketHeaderSize);
    _writeHeader(buf, 0, msgType, _nextSeq(), 0);
    return buf.buffer.asUint8List();
  }

  // -------------------------------------------------------------------------
  // Request encoders
  // -------------------------------------------------------------------------

  /// Encode a GET_INFO request (header only, no payload).
  Uint8List encodeGetInfo() => _headerOnly(kMsgGetInfo);

  /// Encode a GET_CAPABILITIES request.
  Uint8List encodeGetCapabilities() => _headerOnly(kMsgGetCapabilities);

  /// Encode a GET_STATUS request.
  Uint8List encodeGetStatus() => _headerOnly(kMsgGetStatus);

  /// Encode a SET_MODE request.
  ///
  /// [streamsMask] is a bitmask of [kStreamLogic] / [kStreamScope].
  Uint8List encodeSetMode(int streamsMask) {
    const payloadLen = 1;
    final buf = ByteData(kPacketHeaderSize + payloadLen);
    _writeHeader(buf, 0, kMsgSetMode, _nextSeq(), payloadLen);
    buf.setUint8(kPacketHeaderSize, streamsMask & 0xFF);
    return buf.buffer.asUint8List();
  }

  /// Encode a REQUEST_CAPTURE request (25-byte form with analog_channels).
  ///
  /// Payload layout (all little-endian):
  ///   [0..3]   total_samples (uint32)
  ///   [4..7]   rate          (uint32, Hz)
  ///   [8..11]  pre_trigger_samples (uint32)
  ///   [12..23] trigger[4] – each 3 bytes: is_enabled, pin, match
  ///   [24]     analog_channels bitmask (uint8)
  Uint8List encodeRequestCapture(CaptureRequest req) {
    const payloadLen = 25;
    final buf = ByteData(kPacketHeaderSize + payloadLen);
    final seq = _nextSeq();
    _writeHeader(buf, 0, kMsgRequestCapture, seq, payloadLen);

    int p = kPacketHeaderSize;
    buf.setUint32(p, req.totalSamples, Endian.little);
    p += 4;
    buf.setUint32(p, req.sampleRateHz, Endian.little);
    p += 4;
    buf.setUint32(p, req.preTriggerSamples, Endian.little);
    p += 4;

    // Write 4 trigger slots (3 bytes each).
    for (int i = 0; i < kMaxTriggerCount; i++) {
      if (i < req.triggers.length) {
        final t = req.triggers[i];
        buf.setUint8(p, t.isEnabled ? 1 : 0);
        buf.setUint8(p + 1, t.pin & 0xFF);
        buf.setUint8(p + 2, t.match.wireValue & 0xFF);
      } else {
        buf.setUint8(p, 0);
        buf.setUint8(p + 1, 0);
        buf.setUint8(p + 2, 0);
      }
      p += 3;
    }

    buf.setUint8(p, req.analogChannelsMask & 0x07);
    return buf.buffer.asUint8List();
  }

  /// Encode a READ_DATA_BLOCK request (header only).
  Uint8List encodeReadDataBlock() => _headerOnly(kMsgReadDataBlock);

  // -------------------------------------------------------------------------
  // Response decoders
  // -------------------------------------------------------------------------

  /// Validate the 8-byte packet header in [data] and return the message type.
  ///
  /// Throws [ProtocolException] if the magic or length fields are invalid.
  _ParsedHeader _parseHeader(Uint8List data) {
    if (data.length < kPacketHeaderSize) {
      throw const ProtocolException('Response too short for header');
    }
    final bd = ByteData.sublistView(data);
    final magic = bd.getUint16(0, Endian.little);
    if (magic != kPacketMagic) {
      throw ProtocolException(
        'Bad magic: expected 0x${kPacketMagic.toRadixString(16)}, '
        'got 0x${magic.toRadixString(16)}',
      );
    }
    return _ParsedHeader(
      msgType: bd.getUint8(4),
      seq: bd.getUint8(5),
      payloadLen: bd.getUint16(6, Endian.little),
    );
  }

  /// Decode a GET_INFO ACK payload into a [DeviceInfo].
  DeviceInfo decodeGetInfoAck(Uint8List data) {
    final hdr = _parseHeader(data);
    _expectAck(hdr, data);
    // ACK payload: 1 byte status, then firmware fields start at offset 1.
    // But the ACK wraps the GET_INFO response which has:
    //   versionMajor (1), versionMinor (1), fwId (32)
    // The firmware ACK puts the info_response_t directly in the ACK payload.
    if (data.length < kPacketHeaderSize + 1 + 34) {
      throw const ProtocolException('GET_INFO ACK too short');
    }
    final bd = ByteData.sublistView(data);
    int p = kPacketHeaderSize + 1; // skip status byte
    final major = bd.getUint8(p);
    final minor = bd.getUint8(p + 1);
    // fwId is a 32-byte null-terminated UTF-8 string.
    final idBytes = data.sublist(p + 2, p + 2 + kFwIdMaxLen);
    final nullIdx = idBytes.indexOf(0);
    final fwId = String.fromCharCodes(
      nullIdx >= 0 ? idBytes.sublist(0, nullIdx) : idBytes,
    );
    return DeviceInfo(
      protocolVersionMajor: major,
      protocolVersionMinor: minor,
      firmwareId: fwId,
    );
  }

  /// Decode a GET_CAPABILITIES ACK payload.
  DeviceCapabilities decodeGetCapabilitiesAck(Uint8List data) {
    final hdr = _parseHeader(data);
    _expectAck(hdr, data);
    // picomso_capabilities_t starts right after ACK status byte.
    // Fields: version(1), size(1), cap_flags(1), max_logic_ch(1),
    //         max_analog_ch(1), max_rate_logic(4), max_rate_scope(4),
    //         max_samples_logic(4), max_samples_scope(4) = 21 bytes.
    if (data.length < kPacketHeaderSize + 1 + 21) {
      throw const ProtocolException('GET_CAPABILITIES ACK too short');
    }
    final bd = ByteData.sublistView(data);
    int p = kPacketHeaderSize + 1; // skip status byte
    final structVer = bd.getUint8(p);
    if (structVer != 1) {
      throw ProtocolException('Unsupported capabilities version: $structVer');
    }
    p += 2; // skip version + size
    // p now at cap_flags
    final capFlags = bd.getUint8(p); p++;
    final maxLogicCh = bd.getUint8(p); p++;
    final maxAnalogCh = bd.getUint8(p); p++;
    final maxRateLogic = bd.getUint32(p, Endian.little); p += 4;
    final maxRateScope = bd.getUint32(p, Endian.little); p += 4;
    final maxSamplesLogic = bd.getUint32(p, Endian.little); p += 4;
    final maxSamplesScope = bd.getUint32(p, Endian.little);
    return DeviceCapabilities(
      capabilityFlags: capFlags,
      maxLogicChannels: maxLogicCh,
      maxAnalogChannels: maxAnalogCh,
      maxSampleRateLogicHz: maxRateLogic,
      maxSampleRateScopeHz: maxRateScope,
      maxSamplesLogic: maxSamplesLogic,
      maxSamplesScope: maxSamplesScope,
    );
  }

  /// Decode a GET_STATUS ACK payload.
  DeviceStatus decodeGetStatusAck(Uint8List data) {
    final hdr = _parseHeader(data);
    _expectAck(hdr, data);
    if (data.length < kPacketHeaderSize + 3) {
      throw const ProtocolException('GET_STATUS ACK too short');
    }
    final bd = ByteData.sublistView(data);
    int p = kPacketHeaderSize + 1; // skip status byte
    final streams = bd.getUint8(p);
    final captureState = bd.getUint8(p + 1);
    return DeviceStatus(
      streamsMask: streams,
      isCapturing: captureState == kCaptureRunning,
    );
  }

  /// Decode a generic ACK (e.g. for SET_MODE, REQUEST_CAPTURE).
  ///
  /// Returns the status byte. Throws [ProtocolException] if not an ACK.
  int decodeAck(Uint8List data) {
    final hdr = _parseHeader(data);
    _expectAck(hdr, data);
    final bd = ByteData.sublistView(data);
    return bd.getUint8(kPacketHeaderSize); // status byte
  }

  /// Decode a DATA_BLOCK response.
  ///
  /// Returns a [DecodedDataBlock] with stream_id, flags, block_id, and data.
  DecodedDataBlock decodeDataBlock(Uint8List data) {
    final hdr = _parseHeader(data);
    if (hdr.msgType != kMsgDataBlock) {
      throw ProtocolException(
        'Expected DATA_BLOCK (0x${kMsgDataBlock.toRadixString(16)}), '
        'got 0x${hdr.msgType.toRadixString(16)}',
      );
    }
    if (data.length < kPacketHeaderSize + 6) {
      throw const ProtocolException('DATA_BLOCK response too short');
    }
    final bd = ByteData.sublistView(data);
    int p = kPacketHeaderSize;
    final streamId = bd.getUint8(p); p++;
    final flags = bd.getUint8(p); p++;
    final blockId = bd.getUint16(p, Endian.little); p += 2;
    final dataLen = bd.getUint16(p, Endian.little); p += 2;
    if (data.length < p + dataLen) {
      throw const ProtocolException('DATA_BLOCK data truncated');
    }
    return DecodedDataBlock(
      streamId: streamId,
      flags: flags,
      blockId: blockId,
      payload: Uint8List.fromList(data.sublist(p, p + dataLen)),
    );
  }

  // -------------------------------------------------------------------------
  // Private helpers
  // -------------------------------------------------------------------------

  void _expectAck(_ParsedHeader hdr, Uint8List data) {
    if (hdr.msgType == kMsgError) {
      // Try to extract the error message.
      if (data.length >= kPacketHeaderSize + 2) {
        final bd = ByteData.sublistView(data);
        final status = bd.getUint8(kPacketHeaderSize);
        final msgLen = bd.getUint8(kPacketHeaderSize + 1);
        final msgEnd = kPacketHeaderSize + 2 + msgLen;
        String errMsg = 'status=0x${status.toRadixString(16)}';
        if (data.length >= msgEnd && msgLen > 0) {
          errMsg += ': ${String.fromCharCodes(data.sublist(kPacketHeaderSize + 2, msgEnd))}';
        }
        throw ProtocolException('Device error: $errMsg');
      }
      throw const ProtocolException('Device returned ERROR');
    }
    if (hdr.msgType != kMsgAck) {
      throw ProtocolException(
        'Expected ACK (0x${kMsgAck.toRadixString(16)}), '
        'got 0x${hdr.msgType.toRadixString(16)}',
      );
    }
  }
}

// ---------------------------------------------------------------------------
// Internal parsed-header value type
// ---------------------------------------------------------------------------

class _ParsedHeader {
  const _ParsedHeader({
    required this.msgType,
    required this.seq,
    required this.payloadLen,
  });
  final int msgType;
  final int seq;
  final int payloadLen;
}

// ---------------------------------------------------------------------------
// Decoded DATA_BLOCK
// ---------------------------------------------------------------------------

class DecodedDataBlock {
  const DecodedDataBlock({
    required this.streamId,
    required this.flags,
    required this.blockId,
    required this.payload,
  });

  final int streamId;
  final int flags;
  final int blockId;
  final Uint8List payload;

  bool get isTerminal => (flags & 0x04) != 0; // kFlagTerminal

  bool get isLogicFinalized => (flags & 0x01) != 0;
  bool get isScopeFinalized => (flags & 0x02) != 0;
}

// ---------------------------------------------------------------------------
// Protocol-level exception
// ---------------------------------------------------------------------------

class ProtocolException implements Exception {
  const ProtocolException(this.message);

  final String message;

  @override
  String toString() => 'ProtocolException: $message';
}
