import 'package:flutter/material.dart';
import 'package:picomso/domain/models/viewport_state.dart';
import 'package:picomso/ui/shared/theme/app_theme.dart';

/// Draws cursor A and cursor B lines over the waveform.
///
/// Has its own [RepaintBoundary] so cursor drags do not invalidate signal
/// painters.
class CursorPainter extends CustomPainter {
  CursorPainter({required this.viewport}) : super(repaint: null);

  final ViewportState viewport;

  final Paint _aPaint = Paint()
    ..color = AppTheme.cursorAColor
    ..strokeWidth = 1.5;

  final Paint _bPaint = Paint()
    ..color = AppTheme.cursorBColor
    ..strokeWidth = 1.5;

  @override
  void paint(Canvas canvas, Size size) {
    if (viewport.cursorANs != null) {
      final x = viewport.nsToPixel(viewport.cursorANs!);
      if (x >= 0 && x <= size.width) {
        _drawDashedLine(canvas, size, x, _aPaint);
        _drawCursorLabel(canvas, 'A', x, 12, AppTheme.cursorAColor);
      }
    }

    if (viewport.cursorBNs != null) {
      final x = viewport.nsToPixel(viewport.cursorBNs!);
      if (x >= 0 && x <= size.width) {
        _drawDashedLine(canvas, size, x, _bPaint);
        _drawCursorLabel(canvas, 'B', x, 24, AppTheme.cursorBColor);
      }
    }
  }

  void _drawDashedLine(Canvas canvas, Size size, double x, Paint paint) {
    const dashLen = 8.0;
    const gapLen = 4.0;
    double y = 0;
    while (y < size.height) {
      canvas.drawLine(
        Offset(x, y),
        Offset(x, (y + dashLen).clamp(0, size.height)),
        paint,
      );
      y += dashLen + gapLen;
    }
  }

  void _drawCursorLabel(
      Canvas canvas, String label, double x, double y, Color color) {
    final tp = TextPainter(
      text: TextSpan(
        text: label,
        style: TextStyle(
          color: color,
          fontSize: 10,
          fontWeight: FontWeight.bold,
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(canvas, Offset(x + 2, y));
  }

  @override
  bool shouldRepaint(CursorPainter old) => old.viewport != viewport;
}
