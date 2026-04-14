import 'dart:typed_data';
import 'dart:isolate';

/// Min-max downsampling pyramid for an [AnalogTrack].
///
/// Level 0 is the raw [Float32List].  Each subsequent level stores
/// interleaved [min, max] pairs for windows of size 4^k:
///
///   Level k window size: 4^k
///   Level k pair count: ceil(N / 4^k)
///   Level k data length: pair_count * 2 floats
///
/// At render time, pick the level where window size ≈ samplesPerPixel, then
/// for each visible pixel read one [min, max] pair and draw a vertical line.
class MinMaxPyramid {
  MinMaxPyramid._({
    required this.rawSamples,
    required this.levels,
  });

  /// Level 0: raw normalized samples (same reference as [AnalogTrack.samples]).
  final Float32List rawSamples;

  /// Levels 1..k: interleaved [min0, max0, min1, max1, ...].
  final List<Float32List> levels;

  int get totalSamples => rawSamples.length;
  int get levelCount => levels.length + 1; // include level 0

  /// Build the pyramid in the current isolate synchronously.
  ///
  /// Callers should use [buildAsync] to avoid blocking the UI thread.
  factory MinMaxPyramid.buildSync(Float32List raw) {
    final pyramidLevels = <Float32List>[];
    Float32List current = raw;
    const windowFactor = 4;

    while (current.length > windowFactor) {
      final pairCount = (current.length / windowFactor).ceil();
      final level = Float32List(pairCount * 2);
      for (int i = 0; i < pairCount; i++) {
        final start = i * windowFactor;
        final end = (start + windowFactor).clamp(0, current.length);
        double mn = current[start];
        double mx = current[start];
        for (int j = start + 1; j < end; j++) {
          if (current[j] < mn) mn = current[j];
          if (current[j] > mx) mx = current[j];
        }
        level[i * 2] = mn;
        level[i * 2 + 1] = mx;
      }
      pyramidLevels.add(level);
      // Compress current to max values only for next level computation.
      final next = Float32List(pairCount);
      for (int i = 0; i < pairCount; i++) {
        next[i] = level[i * 2 + 1]; // use max as proxy
      }
      current = next;
    }

    return MinMaxPyramid._(rawSamples: raw, levels: pyramidLevels);
  }

  /// Build the pyramid in a separate [Isolate] and return when complete.
  static Future<MinMaxPyramid> buildAsync(Float32List raw) {
    return Isolate.run(() => MinMaxPyramid.buildSync(raw));
  }

  /// Select the appropriate pyramid level for a given [samplesPerPixel] ratio.
  ///
  /// Returns the (levelIndex, windowSize) pair where windowSize ≈ samplesPerPixel.
  /// levelIndex 0 = raw samples (use [rawSamples] directly).
  (int levelIndex, int windowSize) selectLevel(double samplesPerPixel) {
    if (samplesPerPixel <= 1.0) return (0, 1);
    int windowSize = 1;
    for (int k = 0; k < levels.length; k++) {
      final nextWindow = windowSize * 4;
      if (nextWindow > samplesPerPixel) {
        return (k + 1, windowSize);
      }
      windowSize = nextWindow;
    }
    return (levels.length, windowSize);
  }

  /// Get [min, max] for the window at [windowIndex] in [levelIndex].
  ///
  /// For level 0 the min and max are both the raw sample value.
  (double min, double max) getMinMax(int levelIndex, int windowIndex) {
    if (levelIndex == 0) {
      final v = rawSamples[windowIndex];
      return (v, v);
    }
    final lvl = levels[levelIndex - 1];
    final i = windowIndex * 2;
    if (i + 1 >= lvl.length) return (0, 0);
    return (lvl[i], lvl[i + 1]);
  }
}
