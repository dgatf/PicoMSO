import 'package:flutter/material.dart';
import 'package:picomso/domain/models/viewport_state.dart';
import 'package:picomso/ui/shared/theme/app_theme.dart';

/// Draws the time-axis grid lines and time labels.
///
/// Repaints only when [viewport] changes (time or canvas size).
class GridPainter extends CustomPainter {
  GridPainter({required this.viewport}) : super(repaint: null);

  final ViewportState viewport;

  // Pre-allocated Paint objects — never allocate in paint().
  final Paint _linePaint = Paint()
    ..color = AppTheme.gridLine
    ..strokeWidth = 1.0;

  final Paint _strongLinePaint = Paint()
    ..color = AppTheme.gridLineStrong
    ..strokeWidth = 1.0;

  @override
  void paint(Canvas canvas, Size size) {
    _drawBackground(canvas, size);
    _drawTimeGrid(canvas, size);
    _drawTimeLabels(canvas, size);
  }

  void _drawBackground(Canvas canvas, Size size) {
    canvas.drawRect(
      Rect.fromLTWH(0, 0, size.width, size.height),
      Paint()..color = AppTheme.canvasBackground,
    );
  }

  void _drawTimeGrid(Canvas canvas, Size size) {
    final gridIntervalNs = _selectGridInterval();
    final startAligned = (viewport.visibleStartNs / gridIntervalNs).floor() *
        gridIntervalNs.toDouble();

    double t = startAligned;
    while (t <= viewport.visibleEndNs) {
      final x = viewport.nsToPixel(t);
      if (x >= 0 && x <= size.width) {
        // Major grid line every 5 intervals.
        final isMajor =
            ((t / gridIntervalNs).round() % 5) == 0;
        canvas.drawLine(
          Offset(x, 0),
          Offset(x, size.height),
          isMajor ? _strongLinePaint : _linePaint,
        );
      }
      t += gridIntervalNs;
    }
  }

  void _drawTimeLabels(Canvas canvas, Size size) {
    final gridIntervalNs = _selectGridInterval();
    final startAligned = (viewport.visibleStartNs / gridIntervalNs).floor() *
        gridIntervalNs.toDouble();

    double t = startAligned;
    while (t <= viewport.visibleEndNs) {
      final x = viewport.nsToPixel(t);
      if (x >= 0 && x <= size.width) {
        final tp = TextPainter(
          text: TextSpan(
            text: _formatTime(t),
            style: const TextStyle(
              color: Color(0xFF8B949E),
              fontSize: 9,
            ),
          ),
          textDirection: TextDirection.ltr,
        )..layout();
        tp.paint(canvas, Offset(x + 2, 2));
      }
      t += gridIntervalNs;
    }
  }

  /// Pick a round grid interval (in ns) so that ~8-12 divisions are visible.
  double _selectGridInterval() {
    final visibleNs = viewport.nsPerPixel * viewport.canvasWidthPx;
    const targetDivisions = 10.0;
    final rawInterval = visibleNs / targetDivisions;

    // Round to 1, 2, 5, 10, 20, 50, … × power-of-ten.
    final magnitude = _floorPow10(rawInterval);
    for (final factor in [1.0, 2.0, 5.0, 10.0]) {
      if (magnitude * factor >= rawInterval) return magnitude * factor;
    }
    return magnitude * 10.0;
  }

  static double _floorPow10(double v) {
    if (v <= 0) return 1;
    double p = 1;
    while (p * 10 <= v) p *= 10;
    while (p > v) p /= 10;
    return p;
  }

  String _formatTime(double ns) {
    if (ns.abs() >= 1e9) return '${(ns / 1e9).toStringAsFixed(2)} s';
    if (ns.abs() >= 1e6) return '${(ns / 1e6).toStringAsFixed(2)} ms';
    if (ns.abs() >= 1e3) return '${(ns / 1e3).toStringAsFixed(2)} µs';
    return '${ns.toStringAsFixed(0)} ns';
  }

  @override
  bool shouldRepaint(GridPainter old) => old.viewport != viewport;
}
