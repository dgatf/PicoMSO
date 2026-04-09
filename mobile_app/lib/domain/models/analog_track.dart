import 'dart:typed_data';

/// One analog channel from a capture session.
///
/// Samples are normalized to [0.0, 1.0].  The firmware returns 12-bit ADC
/// values (0-4095); the repository normalizes them on ingest by dividing by
/// 4095.0.
class AnalogTrack {
  const AnalogTrack({
    required this.adcIndex,
    required this.label,
    required this.samples,
    required this.vRef,
  });

  /// ADC input index (0 = A0/GPIO26, 1 = A1/GPIO27, 2 = A2/GPIO28).
  final int adcIndex;

  final String label;

  /// Normalized samples in [0.0, 1.0].
  final Float32List samples;

  /// Reference voltage used to convert normalized values to volts.
  final double vRef;

  int get totalSamples => samples.length;

  /// Returns the voltage at [sampleIndex].
  double voltageAt(int sampleIndex) {
    if (sampleIndex < 0 || sampleIndex >= samples.length) return 0;
    return samples[sampleIndex] * vRef;
  }
}
