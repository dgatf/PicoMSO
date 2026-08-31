import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:picomso/protocol/protocol_codec.dart';
import 'package:picomso/protocol/protocol_constants.dart';
import 'package:picomso/domain/models/capture_mode.dart';
import 'package:picomso/domain/models/capture_request.dart';
import 'package:picomso/domain/models/trigger_config.dart';

void main() {
  group('ProtocolCodec', () {
    late ProtocolCodec codec;

    setUp(() => codec = ProtocolCodec());

    // -----------------------------------------------------------------------
    // Header encoding
    // -----------------------------------------------------------------------

    test('encodeGetInfo produces valid header', () {
      final bytes = codec.encodeGetInfo();
      expect(bytes.length, kPacketHeaderSize);
      final bd = ByteData.sublistView(bytes);
      expect(bd.getUint16(0, Endian.little), kPacketMagic,
          reason: 'magic mismatch');
      expect(bd.getUint8(2), kProtocolVersionMajor);
      expect(bd.getUint8(3), kProtocolVersionMinor);
      expect(bd.getUint8(4), kMsgGetInfo);
      expect(bd.getUint16(6, Endian.little), 0, reason: 'payload length should be 0');
    });

    test('encodeGetCapabilities produces valid header', () {
      final bytes = codec.encodeGetCapabilities();
      expect(bd(bytes).getUint8(4), kMsgGetCapabilities);
    });

    test('encodeGetStatus produces valid header', () {
      final bytes = codec.encodeGetStatus();
      expect(bd(bytes).getUint8(4), kMsgGetStatus);
    });

    test('encodeSetMode encodes stream mask correctly', () {
      final bytes = codec.encodeSetMode(kStreamLogic);
      expect(bytes.length, kPacketHeaderSize + 1);
      expect(bd(bytes).getUint8(4), kMsgSetMode);
      expect(bd(bytes).getUint8(kPacketHeaderSize), kStreamLogic);
    });

    test('encodeSetMode mixed-signal sets both bits', () {
      final bytes = codec.encodeSetMode(kStreamBoth);
      expect(bd(bytes).getUint8(kPacketHeaderSize), kStreamBoth);
    });

    // -----------------------------------------------------------------------
    // REQUEST_CAPTURE encoding
    // -----------------------------------------------------------------------

    test('encodeRequestCapture produces 25-byte payload', () {
      final req = CaptureRequest(
        mode: CaptureMode.logicOnly,
        totalSamples: 4096,
        sampleRateHz: 1000000,
        preTriggerSamples: 400,
        triggers: [
          const TriggerConfig(
            isEnabled: true,
            pin: 3,
            match: TriggerMatch.edgeHigh,
          ),
        ],
        analogChannelsMask: 0x00,
      );
      final bytes = codec.encodeRequestCapture(req);
      expect(bytes.length, kPacketHeaderSize + 25);
      final b = ByteData.sublistView(bytes);

      // total_samples at offset 8
      expect(b.getUint32(kPacketHeaderSize + 0, Endian.little), 4096);
      // rate
      expect(b.getUint32(kPacketHeaderSize + 4, Endian.little), 1000000);
      // pre_trigger
      expect(b.getUint32(kPacketHeaderSize + 8, Endian.little), 400);
      // trigger[0]: is_enabled=1, pin=3, match=edgeHigh=0x03
      expect(b.getUint8(kPacketHeaderSize + 12), 1); // is_enabled
      expect(b.getUint8(kPacketHeaderSize + 13), 3); // pin
      expect(b.getUint8(kPacketHeaderSize + 14), TriggerMatch.edgeHigh.wireValue);
      // trigger[1..3] all zeros
      for (int i = 1; i < 4; i++) {
        expect(b.getUint8(kPacketHeaderSize + 12 + i * 3), 0); // is_enabled
      }
      // analog_channels at offset 24
      expect(b.getUint8(kPacketHeaderSize + 24), 0x00);
    });

    test('encodeRequestCapture sets analog channels for mixed-signal', () {
      final req = CaptureRequest(
        mode: CaptureMode.mixedSignal,
        totalSamples: 8192,
        sampleRateHz: 500000,
        preTriggerSamples: 0,
        triggers: const [],
        analogChannelsMask: 0x05, // A0 + A2
      );
      final bytes = codec.encodeRequestCapture(req);
      final b = ByteData.sublistView(bytes);
      expect(b.getUint8(kPacketHeaderSize + 24), 0x05);
    });

    test('analog_channels clamped to 3 bits', () {
      final req = CaptureRequest(
        mode: CaptureMode.mixedSignal,
        totalSamples: 4096,
        sampleRateHz: 1000000,
        preTriggerSamples: 0,
        triggers: const [],
        analogChannelsMask: 0xFF, // out-of-range bits should be masked
      );
      final bytes = codec.encodeRequestCapture(req);
      final b = ByteData.sublistView(bytes);
      expect(b.getUint8(kPacketHeaderSize + 24), 0x07); // only bits 0-2
    });

    // -----------------------------------------------------------------------
    // Sequence number
    // -----------------------------------------------------------------------

    test('sequence number increments across calls', () {
      final a = codec.encodeGetInfo();
      final b2 = codec.encodeGetInfo();
      final seqA = ByteData.sublistView(a).getUint8(5);
      final seqB = ByteData.sublistView(b2).getUint8(5);
      expect(seqB, (seqA + 1) & 0xFF);
    });

    // -----------------------------------------------------------------------
    // ACK decoding
    // -----------------------------------------------------------------------

    test('decodeAck accepts valid ACK packet', () {
      final ack = _buildAck(seq: 1, status: kStatusOk);
      expect(() => codec.decodeAck(ack), returnsNormally);
    });

    test('decodeAck throws on ERROR packet', () {
      final err = _buildError(seq: 1, status: kStatusErrBadMode);
      expect(() => codec.decodeAck(err), throwsA(isA<ProtocolException>()));
    });

    test('decodeAck throws on wrong msg_type', () {
      final bad = _buildAck(seq: 1, status: kStatusOk);
      bad[4] = kMsgGetInfo; // corrupt msg_type
      expect(() => codec.decodeAck(bad), throwsA(isA<ProtocolException>()));
    });

    test('decodeAck throws on bad magic', () {
      final ack = _buildAck(seq: 1, status: kStatusOk);
      ack[0] = 0xFF;
      expect(() => codec.decodeAck(ack), throwsA(isA<ProtocolException>()));
    });

    // -----------------------------------------------------------------------
    // DATA_BLOCK decoding
    // -----------------------------------------------------------------------

    test('decodeDataBlock decodes stream_id, flags, block_id, data', () {
      final raw = _buildDataBlock(
        streamId: kStreamIdLogic,
        flags: kFlagTerminal,
        blockId: 42,
        data: Uint8List.fromList(List.generate(16, (i) => i)),
      );
      final block = codec.decodeDataBlock(raw);
      expect(block.streamId, kStreamIdLogic);
      expect(block.flags, kFlagTerminal);
      expect(block.blockId, 42);
      expect(block.payload.length, 16);
      expect(block.isTerminal, true);
    });

    test('DecodedDataBlock.isTerminal is false when flag not set', () {
      final raw = _buildDataBlock(
        streamId: kStreamIdLogic,
        flags: kFlagLogicFinalized,
        blockId: 0,
        data: Uint8List(0),
      );
      final block = codec.decodeDataBlock(raw);
      expect(block.isTerminal, false);
    });
  });
}

ByteData bd(Uint8List bytes) => ByteData.sublistView(bytes);

/// Build a minimal ACK packet.
Uint8List _buildAck({required int seq, required int status}) {
  final buf = ByteData(kPacketHeaderSize + 1);
  buf.setUint16(0, kPacketMagic, Endian.little);
  buf.setUint8(2, kProtocolVersionMajor);
  buf.setUint8(3, kProtocolVersionMinor);
  buf.setUint8(4, kMsgAck);
  buf.setUint8(5, seq);
  buf.setUint16(6, 1, Endian.little);
  buf.setUint8(8, status);
  return buf.buffer.asUint8List();
}

/// Build a minimal ERROR packet.
Uint8List _buildError({required int seq, required int status}) {
  final buf = ByteData(kPacketHeaderSize + 2);
  buf.setUint16(0, kPacketMagic, Endian.little);
  buf.setUint8(2, kProtocolVersionMajor);
  buf.setUint8(3, kProtocolVersionMinor);
  buf.setUint8(4, kMsgError);
  buf.setUint8(5, seq);
  buf.setUint16(6, 2, Endian.little);
  buf.setUint8(8, status);
  buf.setUint8(9, 0); // msg_len
  return buf.buffer.asUint8List();
}

/// Build a DATA_BLOCK response.
Uint8List _buildDataBlock({
  required int streamId,
  required int flags,
  required int blockId,
  required Uint8List data,
}) {
  const headerSize = kPacketHeaderSize;
  const blockMeta = 6; // stream_id(1) + flags(1) + block_id(2) + data_len(2)
  final total = headerSize + blockMeta + data.length;
  final buf = ByteData(total);
  buf.setUint16(0, kPacketMagic, Endian.little);
  buf.setUint8(2, kProtocolVersionMajor);
  buf.setUint8(3, kProtocolVersionMinor);
  buf.setUint8(4, kMsgDataBlock);
  buf.setUint8(5, 1);
  buf.setUint16(6, blockMeta + data.length, Endian.little);
  int p = headerSize;
  buf.setUint8(p, streamId); p++;
  buf.setUint8(p, flags); p++;
  buf.setUint16(p, blockId, Endian.little); p += 2;
  buf.setUint16(p, data.length, Endian.little); p += 2;
  for (int i = 0; i < data.length; i++) {
    buf.setUint8(p + i, data[i]);
  }
  return buf.buffer.asUint8List();
}
