/// One decoded symbol or annotation on a digital track.
class DecoderResult {
  const DecoderResult({
    required this.channelIndex,
    required this.startSample,
    required this.endSample,
    required this.label,
    this.severity = DecoderSeverity.normal,
  });

  /// Digital track channel index this annotation belongs to.
  final int channelIndex;

  final int startSample;
  final int endSample;

  /// Display label (e.g. "0xAB", "SDA:ACK", "UART:0x0D").
  final String label;

  final DecoderSeverity severity;

  int get durationSamples => endSample - startSample;

  @override
  bool operator ==(Object other) =>
      other is DecoderResult &&
      other.channelIndex == channelIndex &&
      other.startSample == startSample &&
      other.endSample == endSample &&
      other.label == label &&
      other.severity == severity;

  @override
  int get hashCode =>
      Object.hash(channelIndex, startSample, endSample, label, severity);
}

/// Severity level for a decoder annotation.
enum DecoderSeverity {
  /// Normal decoded symbol.
  normal,

  /// Unexpected but non-fatal condition (e.g. framing warning).
  warning,

  /// Protocol error (e.g. bad checksum, NACK when ACK expected).
  error,
}
