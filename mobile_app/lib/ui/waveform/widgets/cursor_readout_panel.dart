import 'package:flutter/material.dart';
import 'package:picomso/domain/models/viewport_state.dart';

/// Floating panel showing cursor A/B time values and delta.
class CursorReadoutPanel extends StatelessWidget {
  const CursorReadoutPanel({super.key, required this.viewport});

  final ViewportState viewport;

  @override
  Widget build(BuildContext context) {
    final a = viewport.cursorANs;
    final b = viewport.cursorBNs;
    if (a == null && b == null) return const SizedBox.shrink();

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: const Color(0xFF161B22).withAlpha(230),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: const Color(0xFF30363D)),
      ),
      child: DefaultTextStyle(
        style: const TextStyle(fontSize: 11, fontFamily: 'monospace'),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            if (a != null)
              _CursorRow(label: 'A', ns: a, color: const Color(0xFF58A6FF)),
            if (b != null)
              _CursorRow(label: 'B', ns: b, color: const Color(0xFFF78166)),
            if (a != null && b != null) ...[
              const Divider(height: 8, color: Color(0xFF30363D)),
              _CursorRow(
                label: 'Δ',
                ns: (b - a).abs(),
                color: const Color(0xFFE6EDF3),
                showFreq: true,
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _CursorRow extends StatelessWidget {
  const _CursorRow({
    required this.label,
    required this.ns,
    required this.color,
    this.showFreq = false,
  });

  final String label;
  final double ns;
  final Color color;
  final bool showFreq;

  @override
  Widget build(BuildContext context) {
    String timeStr;
    if (ns.abs() >= 1e9) {
      timeStr = '${(ns / 1e9).toStringAsFixed(4)} s';
    } else if (ns.abs() >= 1e6) {
      timeStr = '${(ns / 1e6).toStringAsFixed(4)} ms';
    } else if (ns.abs() >= 1e3) {
      timeStr = '${(ns / 1e3).toStringAsFixed(4)} µs';
    } else {
      timeStr = '${ns.toStringAsFixed(1)} ns';
    }

    String freqStr = '';
    if (showFreq && ns > 0) {
      final freq = 1e9 / ns;
      if (freq >= 1e6) {
        freqStr = '  ${(freq / 1e6).toStringAsFixed(3)} MHz';
      } else if (freq >= 1e3) {
        freqStr = '  ${(freq / 1e3).toStringAsFixed(3)} kHz';
      } else {
        freqStr = '  ${freq.toStringAsFixed(3)} Hz';
      }
    }

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            '$label: ',
            style: TextStyle(color: color, fontWeight: FontWeight.w700),
          ),
          Text(
            timeStr,
            style: TextStyle(color: color),
          ),
          if (freqStr.isNotEmpty)
            Text(
              freqStr,
              style: const TextStyle(color: Color(0xFF8B949E)),
            ),
        ],
      ),
    );
  }
}
