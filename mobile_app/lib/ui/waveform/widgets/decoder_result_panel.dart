import 'package:flutter/material.dart';
import 'package:picomso/domain/models/decoder_result.dart';
import 'package:picomso/ui/shared/theme/app_theme.dart';

/// Dismissible bottom panel showing decoded symbols in a scrollable list.
class DecoderResultPanel extends StatelessWidget {
  const DecoderResultPanel({
    super.key,
    required this.results,
    required this.onDismiss,
  });

  final List<DecoderResult> results;
  final VoidCallback onDismiss;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 180,
      decoration: const BoxDecoration(
        color: Color(0xFF161B22),
        border: Border(top: BorderSide(color: Color(0xFF30363D))),
      ),
      child: Column(
        children: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 6),
            child: Row(
              children: [
                const Text(
                  'Decoded',
                  style: TextStyle(
                    fontWeight: FontWeight.w600,
                    fontSize: 13,
                    color: Color(0xFF8B949E),
                  ),
                ),
                const SizedBox(width: 8),
                Chip(
                  label: Text(
                    '${results.length}',
                    style: const TextStyle(fontSize: 11),
                  ),
                  padding: EdgeInsets.zero,
                  materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                ),
                const Spacer(),
                IconButton(
                  icon: const Icon(Icons.keyboard_arrow_down, size: 20),
                  onPressed: onDismiss,
                  padding: EdgeInsets.zero,
                  constraints: const BoxConstraints(),
                ),
              ],
            ),
          ),
          Expanded(
            child: ListView.builder(
              scrollDirection: Axis.horizontal,
              padding: const EdgeInsets.symmetric(horizontal: 8),
              itemCount: results.length,
              itemBuilder: (_, i) => _DecoderChip(result: results[i]),
            ),
          ),
        ],
      ),
    );
  }
}

class _DecoderChip extends StatelessWidget {
  const _DecoderChip({required this.result});

  final DecoderResult result;

  @override
  Widget build(BuildContext context) {
    final color = _colorForSeverity(result.severity, result.channelIndex);
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 8),
      child: Chip(
        label: Text(
          result.label,
          style: TextStyle(color: color, fontSize: 11, fontFamily: 'monospace'),
        ),
        backgroundColor: color.withAlpha(26),
        side: BorderSide(color: color.withAlpha(77)),
        padding: const EdgeInsets.symmetric(horizontal: 4),
      ),
    );
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
}
