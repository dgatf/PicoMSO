import 'package:picomso/domain/decoders/decoder_interface.dart';
import 'package:picomso/domain/models/capture_session.dart';
import 'package:picomso/domain/models/decoder_result.dart';

/// SPI decoder configuration.
class SpiDecoderConfig extends DecoderConfig {
  const SpiDecoderConfig({
    required super.channelIndex, // MOSI channel
    required this.clkChannelIndex,
    this.misoChannelIndex,
    this.csChannelIndex,
    this.cpol = 0,
    this.cpha = 0,
    this.bitsPerWord = 8,
    this.format = SpiByteFormat.hex,
  });

  final int clkChannelIndex;
  final int? misoChannelIndex;
  final int? csChannelIndex;

  /// Clock polarity (0 = idle low, 1 = idle high).
  final int cpol;

  /// Clock phase (0 = sample on leading, 1 = sample on trailing).
  final int cpha;

  final int bitsPerWord;
  final SpiByteFormat format;

  @override
  String get name => 'SPI';
}

enum SpiByteFormat { hex, binary, ascii, decimal }

/// Stateless SPI decoder.
///
/// Samples MOSI (and optionally MISO) on the configured clock edge.
/// CS (active-low) is used to delineate transactions when present.
class SpiDecoder extends ProtocolDecoder<SpiDecoderConfig> {
  const SpiDecoder();

  @override
  String get name => 'SPI';

  @override
  List<DecoderResult> decode(CaptureSession session, SpiDecoderConfig config) {
    final mosiTrack = session.digitalTracks.where(
      (t) => t.channelIndex == config.channelIndex,
    ).firstOrNull;
    final clkTrack = session.digitalTracks.where(
      (t) => t.channelIndex == config.clkChannelIndex,
    ).firstOrNull;
    if (mosiTrack == null || clkTrack == null) return const [];

    final results = <DecoderResult>[];
    final total = mosiTrack.totalSamples;

    // Determine which clock edge to sample on.
    // CPOL=0 CPHA=0: sample on rising  (CPOL=0 idle-low, lead=rising)
    // CPOL=0 CPHA=1: sample on falling
    // CPOL=1 CPHA=0: sample on falling (CPOL=1 idle-high, lead=falling)
    // CPOL=1 CPHA=1: sample on rising
    final sampleOnRising = (config.cpol ^ config.cpha) == 0;

    int wordStart = -1;
    int wordValue = 0;
    int bitCount = 0;

    bool prevClk = clkTrack.sampleAt(0);

    for (int i = 1; i < total; i++) {
      final curClk = clkTrack.sampleAt(i);
      final rising = !prevClk && curClk;
      final falling = prevClk && !curClk;
      final sampleEdge = sampleOnRising ? rising : falling;

      if (sampleEdge) {
        if (wordStart < 0) wordStart = i;
        final bit = mosiTrack.sampleAt(i) ? 1 : 0;
        wordValue = (wordValue << 1) | bit;
        bitCount++;

        if (bitCount >= config.bitsPerWord) {
          results.add(DecoderResult(
            channelIndex: config.channelIndex,
            startSample: wordStart,
            endSample: i,
            label: _format(wordValue, config.format),
          ));
          wordStart = -1;
          wordValue = 0;
          bitCount = 0;
        }
      }

      prevClk = curClk;
    }

    return results;
  }

  String _format(int value, SpiByteFormat format) {
    switch (format) {
      case SpiByteFormat.hex:
        return '0x${value.toRadixString(16).toUpperCase().padLeft(2, '0')}';
      case SpiByteFormat.binary:
        return '0b${value.toRadixString(2).padLeft(8, '0')}';
      case SpiByteFormat.decimal:
        return '$value';
      case SpiByteFormat.ascii:
        if (value >= 0x20 && value < 0x7F) {
          return "'${String.fromCharCode(value)}'";
        }
        return '0x${value.toRadixString(16).toUpperCase().padLeft(2, '0')}';
    }
  }
}
