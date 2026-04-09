import 'package:picomso/domain/models/capture_session.dart';
import 'package:picomso/domain/models/decoder_result.dart';

/// Configuration for one decoder plugin.
abstract class DecoderConfig {
  const DecoderConfig({required this.channelIndex});

  /// Primary digital track this decoder operates on.
  final int channelIndex;

  /// Human-readable decoder name (e.g. "UART", "SPI").
  String get name;
}

/// A protocol decoder plugin.
///
/// Implementations are stateless.  Call [decode] with the [CaptureSession]
/// and a [DecoderConfig] and receive a list of [DecoderResult] annotations.
///
/// Decoders run off the UI thread via [Isolate.run] in [DecoderController].
abstract class ProtocolDecoder<C extends DecoderConfig> {
  const ProtocolDecoder();

  /// Return the display name for this decoder type (e.g. "UART").
  String get name;

  /// Decode [session] using [config] and return annotations.
  ///
  /// This method is called from an isolate; do not access Flutter state.
  List<DecoderResult> decode(CaptureSession session, C config);
}
