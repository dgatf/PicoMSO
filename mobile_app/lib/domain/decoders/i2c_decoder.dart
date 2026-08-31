import 'package:picomso/domain/decoders/decoder_interface.dart';
import 'package:picomso/domain/models/capture_session.dart';
import 'package:picomso/domain/models/decoder_result.dart';

/// I2C decoder configuration.
class I2cDecoderConfig extends DecoderConfig {
  const I2cDecoderConfig({
    required super.channelIndex, // SDA channel
    required this.sclChannelIndex,
    this.format = I2cByteFormat.hex,
  });

  final int sclChannelIndex;
  final I2cByteFormat format;

  @override
  String get name => 'I2C';
}

enum I2cByteFormat { hex, binary, ascii, decimal }

/// Stateless I2C decoder.
///
/// Detects START/STOP conditions, address bytes, data bytes, and ACK/NACK.
/// Supports both 7-bit addressing and clock stretching (SCL held low).
class I2cDecoder extends ProtocolDecoder<I2cDecoderConfig> {
  const I2cDecoder();

  @override
  String get name => 'I2C';

  @override
  List<DecoderResult> decode(CaptureSession session, I2cDecoderConfig config) {
    final sdaTrack = session.digitalTracks.where(
      (t) => t.channelIndex == config.channelIndex,
    ).firstOrNull;
    final sclTrack = session.digitalTracks.where(
      (t) => t.channelIndex == config.sclChannelIndex,
    ).firstOrNull;
    if (sdaTrack == null || sclTrack == null) return const [];

    final results = <DecoderResult>[];
    final total = sdaTrack.totalSamples;

    bool prevScl = sclTrack.sampleAt(0);
    bool prevSda = sdaTrack.sampleAt(0);

    // State machine
    _I2cState state = _I2cState.idle;
    int bitCount = 0;
    int byteValue = 0;
    int byteStartSample = 0;
    bool isAddressByte = true;

    for (int i = 1; i < total; i++) {
      final scl = sclTrack.sampleAt(i);
      final sda = sdaTrack.sampleAt(i);

      // Detect START: SDA falls while SCL is high.
      if (scl && prevScl && !sda && prevSda) {
        results.add(DecoderResult(
          channelIndex: config.channelIndex,
          startSample: i,
          endSample: i + 1,
          label: 'START',
        ));
        state = _I2cState.data;
        bitCount = 0;
        byteValue = 0;
        isAddressByte = true;
        byteStartSample = i;
      }
      // Detect STOP: SDA rises while SCL is high.
      else if (scl && prevScl && sda && !prevSda && state != _I2cState.idle) {
        results.add(DecoderResult(
          channelIndex: config.channelIndex,
          startSample: i,
          endSample: i + 1,
          label: 'STOP',
        ));
        state = _I2cState.idle;
        isAddressByte = true;
      }
      // Sample on rising SCL edge.
      else if (!prevScl && scl && state == _I2cState.data) {
        if (bitCount < 8) {
          byteValue = (byteValue << 1) | (sda ? 1 : 0);
          bitCount++;
          if (bitCount == 1) byteStartSample = i;
        } else {
          // 9th bit = ACK/NACK
          final isAck = !sda; // ACK = SDA pulled low
          String label;
          if (isAddressByte) {
            final addr = byteValue >> 1;
            final isRead = (byteValue & 1) == 1;
            label =
                '0x${addr.toRadixString(16).toUpperCase()} ${isRead ? "R" : "W"} ${isAck ? "ACK" : "NACK"}';
          } else {
            label =
                '${_formatByte(byteValue, config.format)} ${isAck ? "ACK" : "NACK"}';
          }
          results.add(DecoderResult(
            channelIndex: config.channelIndex,
            startSample: byteStartSample,
            endSample: i,
            label: label,
            severity: (!isAck && isAddressByte)
                ? DecoderSeverity.warning
                : DecoderSeverity.normal,
          ));
          bitCount = 0;
          byteValue = 0;
          isAddressByte = false;
        }
      }

      prevScl = scl;
      prevSda = sda;
    }

    return results;
  }

  String _formatByte(int value, I2cByteFormat format) {
    switch (format) {
      case I2cByteFormat.hex:
        return '0x${value.toRadixString(16).toUpperCase().padLeft(2, '0')}';
      case I2cByteFormat.binary:
        return '0b${value.toRadixString(2).padLeft(8, '0')}';
      case I2cByteFormat.decimal:
        return '$value';
      case I2cByteFormat.ascii:
        if (value >= 0x20 && value < 0x7F) {
          return "'${String.fromCharCode(value)}'";
        }
        return '0x${value.toRadixString(16).toUpperCase().padLeft(2, '0')}';
    }
  }
}

enum _I2cState { idle, data }
