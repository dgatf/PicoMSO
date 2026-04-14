import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:picomso/domain/models/analog_track.dart';
import 'package:picomso/domain/models/min_max_pyramid.dart';
import 'package:picomso/domain/models/viewport_state.dart';
import 'package:picomso/ui/shared/theme/app_theme.dart';

/// Paints analog tracks using a min-max pyramid for efficient downsampling.
class AnalogTrackPainter extends CustomPainter {
  AnalogTrackPainter({
    required this.tracks,
    required this.pyramids,
    required this.viewport,
    required this.trackHeight,
    required this.samplePeriodNs,
  }) : super(repaint: null);

  final List<AnalogTrack> tracks;
  final List<MinMaxPyramid?> pyramids; // index-matched to tracks
  final ViewportState viewport;
  final double trackHeight;
  final double samplePeriodNs;

  // Cache key for pixel coordinate array.
  int? _cachedFirstSample;
  int? _cachedLastSample;
  Float32List? _cachedPoints;

  @override
  void paint(Canvas canvas, Size size) {
    for (int ti = 0; ti < tracks.length; ti++) {
      final track = tracks[ti];
      final pyramid = pyramids[ti];
      final topY = ti * trackHeight;
      final botY = topY + trackHeight;

      _drawBaseline(canvas, size, topY + trackHeight * 0.5);
      _drawAnalogTrack(canvas, track, pyramid, topY, botY);
      _drawYAxisLabel(canvas, track, topY, botY);
    }
  }

  void _drawBaseline(Canvas canvas, Size size, double y) {
    canvas.drawLine(
      Offset(0, y),
      Offset(size.width, y),
      Paint()
        ..color = AppTheme.analogBaseline
        ..strokeWidth = 0.5,
    );
  }

  void _drawAnalogTrack(
    Canvas canvas,
    AnalogTrack track,
    MinMaxPyramid? pyramid,
    double topY,
    double botY,
  ) {
    if (track.samples.isEmpty) return;

    final samplesPerPixel = viewport.nsPerPixel / samplePeriodNs;
    final color = AppTheme.analogColor(track.adcIndex);

    if (pyramid != null && samplesPerPixel > 2) {
      _drawWithPyramid(canvas, track, pyramid, topY, botY, samplesPerPixel, color);
    } else {
      _drawRaw(canvas, track, topY, botY, color);
    }
  }

  void _drawWithPyramid(
    Canvas canvas,
    AnalogTrack track,
    MinMaxPyramid pyramid,
    double topY,
    double botY,
    double samplesPerPixel,
    Color color,
  ) {
    final (levelIdx, windowSize) = pyramid.selectLevel(samplesPerPixel);
    final trackH = botY - topY;
    final midY = topY + trackH * 0.5;

    final linePaint = Paint()
      ..color = color
      ..strokeWidth = 1.5;

    final firstPixel = 0.0;
    final lastPixel = viewport.canvasWidthPx;

    final firstSample = (viewport.visibleStartNs / samplePeriodNs).floor();
    final firstWindow = (firstSample / windowSize).floor();
    final lastSample = (viewport.visibleEndNs / samplePeriodNs).ceil();
    final lastWindow = (lastSample / windowSize).ceil();
    final totalWindows = (track.totalSamples / windowSize).ceil();

    for (int w = firstWindow.clamp(0, totalWindows);
        w <= lastWindow.clamp(0, totalWindows - 1);
        w++) {
      final wStartNs = w * windowSize * samplePeriodNs;
      final wEndNs = wStartNs + windowSize * samplePeriodNs;
      final x = viewport.nsToPixel(wStartNs + windowSize * samplePeriodNs / 2);
      if (x < firstPixel - 1 || x > lastPixel + 1) continue;

      final (mn, mx) = pyramid.getMinMax(levelIdx, w);
      final yMin = midY - (mx - 0.5) * trackH * 0.9;
      final yMax = midY - (mn - 0.5) * trackH * 0.9;

      if ((yMax - yMin).abs() < 1.0) {
        canvas.drawLine(
          Offset(x, (yMin + yMax) / 2),
          Offset(x + 1, (yMin + yMax) / 2),
          linePaint,
        );
      } else {
        canvas.drawLine(Offset(x, yMin), Offset(x, yMax), linePaint);
      }
    }
  }

  void _drawRaw(
    Canvas canvas,
    AnalogTrack track,
    double topY,
    double botY,
    Color color,
  ) {
    final firstSample = (viewport.visibleStartNs / samplePeriodNs).floor().clamp(0, track.totalSamples - 1);
    final lastSample = (viewport.visibleEndNs / samplePeriodNs).ceil().clamp(firstSample, track.totalSamples - 1);
    final count = lastSample - firstSample + 1;
    if (count < 1) return;

    final trackH = botY - topY;
    final midY = topY + trackH * 0.5;

    final points = Float32List(count * 2);
    for (int i = 0; i < count; i++) {
      final s = firstSample + i;
      final ns = s * samplePeriodNs;
      points[i * 2] = viewport.nsToPixel(ns);
      points[i * 2 + 1] = midY - (track.samples[s] - 0.5) * trackH * 0.9;
    }

    canvas.drawRawPoints(
      PointMode.polygon,
      points,
      Paint()
        ..color = color
        ..strokeWidth = 1.5,
    );
  }

  void _drawYAxisLabel(Canvas canvas, AnalogTrack track, double topY, double botY) {
    final tp = TextPainter(
      text: TextSpan(
        text: '${track.vRef.toStringAsFixed(1)} V',
        style: const TextStyle(color: Color(0xFF8B949E), fontSize: 9),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(canvas, Offset(4, topY + 2));
  }

  @override
  bool shouldRepaint(AnalogTrackPainter old) =>
      old.viewport != viewport ||
      old.tracks != tracks ||
      old.pyramids != pyramids ||
      old.trackHeight != trackHeight;
}
