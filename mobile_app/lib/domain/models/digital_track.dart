import 'dart:typed_data';

/// One digital channel from a capture session.
///
/// Samples are packed 1-bit per sample, LSB first within each byte.
/// [packedBits] length = ceil(totalSamples / 8).
class DigitalTrack {
  DigitalTrack({
    required this.channelIndex,
    required this.label,
    required this.packedBits,
    required this.totalSamples,
  }) : _transitionIndices = _buildTransitionIndices(packedBits, totalSamples);

  /// GPIO index (0-15).
  final int channelIndex;

  final String label;

  /// Packed bit array, 1 bit per sample, LSB first.
  final Uint8List packedBits;

  final int totalSamples;

  /// Pre-computed list of sample indices at which the signal transitions.
  /// Index 0 is implicit if the first sample is high.
  /// Used for fast rendering without iterating all bits.
  final List<int> _transitionIndices;

  /// Returns the logic level (true = high) at [sampleIndex].
  bool sampleAt(int sampleIndex) {
    if (sampleIndex < 0 || sampleIndex >= totalSamples) return false;
    final byteIdx = sampleIndex >> 3;
    final bitIdx = sampleIndex & 7;
    return (packedBits[byteIdx] >> bitIdx) & 1 == 1;
  }

  /// Returns the pre-computed transition index list (read-only view).
  List<int> get transitionIndices => _transitionIndices;

  /// First transition index at or after [fromSample] using binary search.
  /// Returns [totalSamples] if no transition is found.
  int firstTransitionAtOrAfter(int fromSample) {
    int lo = 0, hi = _transitionIndices.length;
    while (lo < hi) {
      final mid = (lo + hi) >> 1;
      if (_transitionIndices[mid] < fromSample) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return lo < _transitionIndices.length ? _transitionIndices[lo] : totalSamples;
  }

  static List<int> _buildTransitionIndices(Uint8List bits, int totalSamples) {
    final result = <int>[];
    if (totalSamples == 0) return result;
    bool prev = (bits[0] & 1) == 1;
    for (int i = 1; i < totalSamples; i++) {
      final cur = (bits[i >> 3] >> (i & 7)) & 1 == 1;
      if (cur != prev) {
        result.add(i);
        prev = cur;
      }
    }
    return result;
  }
}
