import 'package:flutter/material.dart';
import 'package:picomso/domain/models/digital_track.dart';
import 'package:picomso/domain/models/viewport_state.dart';
import 'package:picomso/ui/shared/theme/app_theme.dart';

/// Paints all digital tracks in the visible viewport.
///
/// Uses the pre-computed transition index list on each [DigitalTrack] for
/// efficient rendering: binary search for the first visible transition, then
/// walk forward — never iterates all bits.
class DigitalTrackPainter extends CustomPainter {
  DigitalTrackPainter({
    required this.tracks,
    required this.viewport,
    required this.trackHeight,
    required this.trackOffsetY,
    required this.samplePeriodNs,
  }) : super(repaint: null);

  final List<DigitalTrack> tracks;
  final ViewportState viewport;

  /// Height allocated to each digital track row in logical pixels.
  final double trackHeight;

  /// Y offset of the first digital track row from canvas top.
  final double trackOffsetY;

  final double samplePeriodNs;

  // Pre-allocated Path — reused across paint() calls.
  final Path _path = Path();

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..strokeWidth = 1.5
      ..style = PaintingStyle.stroke;

    final firstSample = (viewport.visibleStartNs / samplePeriodNs)
        .floor()
        .clamp(0, double.maxFinite.toInt());
    final lastSample = (viewport.visibleEndNs / samplePeriodNs)
        .ceil()
        .clamp(firstSample, double.maxFinite.toInt());

    for (int ti = 0; ti < tracks.length; ti++) {
      final track = tracks[ti];
      final topY = trackOffsetY + ti * trackHeight + trackHeight * 0.15;
      final botY = trackOffsetY + ti * trackHeight + trackHeight * 0.85;

      paint.color = AppTheme.channelColor(track.channelIndex);
      _path.reset();

      _drawTrack(track, firstSample, lastSample, topY, botY);
      canvas.drawPath(_path, paint);
    }
  }

  void _drawTrack(
    DigitalTrack track,
    int firstSample,
    int lastSample,
    double topY,
    double botY,
  ) {
    if (track.totalSamples == 0) return;

    final clampedFirst = firstSample.clamp(0, track.totalSamples - 1);
    final clampedLast = lastSample.clamp(0, track.totalSamples - 1);

    bool curLevel = track.sampleAt(clampedFirst);
    double curX = viewport.nsToPixel(clampedFirst * samplePeriodNs);
    double curY = curLevel ? topY : botY;
    _path.moveTo(curX, curY);

    // Walk transitions only (fast path).
    int nextTransition = track.firstTransitionAtOrAfter(clampedFirst + 1);

    while (nextTransition <= clampedLast) {
      final transX = viewport.nsToPixel(nextTransition * samplePeriodNs);
      // Horizontal segment to transition.
      _path.lineTo(transX, curY);
      // Vertical edge.
      curLevel = !curLevel;
      curY = curLevel ? topY : botY;
      _path.lineTo(transX, curY);
      nextTransition = track.firstTransitionAtOrAfter(nextTransition + 1);
    }

    // Final horizontal segment to end of viewport.
    final endX = viewport.nsToPixel(clampedLast * samplePeriodNs);
    _path.lineTo(endX, curY);
  }

  @override
  bool shouldRepaint(DigitalTrackPainter old) =>
      old.viewport != viewport ||
      old.tracks != tracks ||
      old.trackHeight != trackHeight ||
      old.trackOffsetY != trackOffsetY;
}
