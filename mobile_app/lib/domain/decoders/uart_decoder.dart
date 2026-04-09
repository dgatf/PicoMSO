import 'package:picomso/domain/decoders/decoder_interface.dart';
import 'package:picomso/domain/models/capture_session.dart';
import 'package:picomso/domain/models/decoder_result.dart';

/// UART decoder configuration.
class UartDecoderConfig extends DecoderConfig {
  const UartDecoderConfig({
    required super.channelIndex,
    required this.baudRate,
    this.dataBits = 8,
    this.stopBits = 1,
    this.parity = UartParity.none,
    this.format = ByteFormat.hex,
    this.idleHigh = true,
  });

  final int baudRate;
  final int dataBits;
  final int stopBits;
  final UartParity parity;
  final ByteFormat format;

  /// True = idle state is logic high (standard UART).
  final bool idleHigh;

  @override
  String get name => 'UART';
}

enum UartParity { none, even, odd }

enum ByteFormat { hex, binary, ascii, decimal }

/// Stateless UART decoder.
///
/// Detects start bit (idle → active), samples [dataBits] bits at
/// baud-rate intervals, checks optional parity and stop bits.
class UartDecoder extends ProtocolDecoder<UartDecoderConfig> {
  const UartDecoder();

  @override
  String get name => 'UART';

  @override
  List<DecoderResult> decode(CaptureSession session, UartDecoderConfig config) {
    final track = session.digitalTracks.where(
      (t) => t.channelIndex == config.channelIndex,
    ).firstOrNull;
    if (track == null) return const [];

    final results = <DecoderResult>[];
    final samplePeriodNs = session.samplePeriodNs;
    final bitPeriodSamples = (1e9 / config.baudRate / samplePeriodNs).round();
    if (bitPeriodSamples < 1) return const [];

    final idle = config.idleHigh;
    int i = 0;
    final total = track.totalSamples;

    while (i < total) {
      // Look for start bit: transition from idle to active.
      final startBit = idle ? false : true;
      if (track.sampleAt(i) == startBit) {
        // Sample middle of start bit to align.
        final frameStart = i;
        int pos = i + bitPeriodSamples ~/ 2;

        // Sample data bits.
        int value = 0;
        bool parityBit = false;
        for (int bit = 0; bit < config.dataBits; bit++) {
          pos += bitPeriodSamples;
          if (pos >= total) break;
          final sample = track.sampleAt(pos);
          final logicHigh = idle ? sample : !sample;
          if (logicHigh) {
            value |= (1 << bit);
            parityBit = !parityBit;
          }
        }

        // Check parity if configured.
        DecoderSeverity severity = DecoderSeverity.normal;
        if (config.parity != UartParity.none) {
          pos += bitPeriodSamples;
          if (pos < total) {
            final parityRx = track.sampleAt(pos);
            final parityLogic = idle ? parityRx : !parityRx;
            final expectedParity = config.parity == UartParity.even
                ? parityBit
                : !parityBit;
            if (parityLogic != expectedParity) {
              severity = DecoderSeverity.error;
            }
          }
        }

        // Check stop bit.
        pos += bitPeriodSamples;
        if (pos < total) {
          final stopSample = track.sampleAt(pos);
          final stopHigh = idle ? stopSample : !stopSample;
          if (!stopHigh) {
            severity = DecoderSeverity.error;
          }
        }

        final frameEnd = pos + bitPeriodSamples ~/ 2;
        results.add(DecoderResult(
          channelIndex: config.channelIndex,
          startSample: frameStart,
          endSample: frameEnd.clamp(frameStart + 1, total),
          label: _formatByte(value, config.format),
          severity: severity,
        ));

        i = frameEnd.clamp(frameStart + 1, total);
      } else {
        i++;
      }
    }

    return results;
  }

  String _formatByte(int value, ByteFormat format) {
    switch (format) {
      case ByteFormat.hex:
        return '0x\${value.toRadixString(16).toUpperCase().padLeft(2, '0')}';
      case ByteFormat.binary:
        return '0b\${value.toRadixString(2).padLeft(8, '0')}';
      case ByteFormat.decimal:
        return '\$value';
      case ByteFormat.ascii:
        if (value >= 0x20 && value < 0x7F) {
          return "'\${String.fromCharCode(value)}'";
        }
        return '0x\${value.toRadixString(16).toUpperCase().padLeft(2, '0')}';
    }
  }
}
