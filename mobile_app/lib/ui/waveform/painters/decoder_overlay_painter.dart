import 'package:flutter/material.dart';
import 'package:picomso/domain/models/decoder_result.dart';
import 'package:picomso/domain/models/viewport_state.dart';
import 'package:picomso/ui/shared/theme/app_theme.dart';

/// Paints decoder annotation segments over the digital tracks.
///
/// Each [DecoderResult] is drawn as a colored rectangle with a text label
/// positioned over the corresponding channel row.
class DecoderOverlayPainter extends CustomPainter {
  DecoderOverlayPainter({
    required this.results,
    required this.viewport,
    required this.trackHeight,
    required this.trackOffsetY,
    required this.samplePeriodNs,
    required this.channelCount,
  }) : super(repaint: null);

  final List<DecoderResult> results;
  final ViewportState viewport;
  final double trackHeight;
  final double trackOffsetY;
  final double samplePeriodNs;
  final int channelCount;

  // Pre-allocated paint objects.
  final Paint _bgPaint = Paint()..style = PaintingStyle.fill;
  final Paint _borderPaint = Paint()
    ..style = PaintingStyle.stroke
    ..strokeWidth = 1.0;

  @override
  void paint(Canvas canvas, Size size) {
    for (final result in results) {
      final startX = viewport.nsToPixel(result.startSample * samplePeriodNs);
      final endX = viewport.nsToPixel(result.endSample * samplePeriodNs);

      if (endX < 0 || startX > size.width) continue;

      final rowY = trackOffsetY +
          result.channelIndex.clamp(0, channelCount - 1) * trackHeight;
      final annotY = rowY + trackHeight * 0.2;
      final annotH = trackHeight * 0.6;

      final rect = RRect.fromRectAndRadius(
        Rect.fromLTWH(
          startX.clamp(0, size.width),
          annotY,
          (endX - startX).clamp(2, size.width),
          annotH,
        ),
        const Radius.circular(3),
      );

      final color = _colorForSeverity(result.severity, result.channelIndex);
      _bgPaint.color = color.withAlpha(51);
      _borderPaint.color = color;

      canvas.drawRRect(rect, _bgPaint);
      canvas.drawRRect(rect, _borderPaint);

      final width = endX - startX;
      if (width > 20) {
        final tp = TextPainter(
          text: TextSpan(
            text: result.label,
            style: TextStyle(color: color, fontSize: 9),
          ),
          textDirection: TextDirection.ltr,
          maxLines: 1,
          ellipsis: '…',
        )..layout(maxWidth: width.clamp(0, size.width).toDouble());
        tp.paint(canvas, Offset(startX + 2, annotY + annotH / 2 - tp.height / 2));
      }
    }
  }

  Color _colorForSeverity(DecoderSeverity severity, int channelIndex) {
    switch (severity) {
      case DecoderSeverity.normal:
        return AppTheme.channelColor(channelIndex);
      case DecoderSeverity.warning:
        return Colors.amber;
      case DecoderSeverity.error:
        return Colors.red;
    }
  }

  @override
  bool shouldRepaint(DecoderOverlayPainter old) =>
      old.results != results ||
      old.viewport != viewport ||
      old.trackHeight != trackHeight ||
      old.trackOffsetY != trackOffsetY;
}
