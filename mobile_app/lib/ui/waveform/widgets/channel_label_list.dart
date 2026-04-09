import 'package:flutter/material.dart';
import 'package:picomso/domain/models/capture_session.dart';
import 'package:picomso/ui/shared/theme/app_theme.dart';

/// Collapsible sidebar showing channel labels and colors.
class ChannelLabelList extends StatefulWidget {
  const ChannelLabelList({
    super.key,
    required this.session,
    required this.trackHeight,
    required this.analogTrackHeight,
    required this.onChannelLongPress,
  });

  final CaptureSession session;
  final double trackHeight;
  final double analogTrackHeight;
  final void Function(int channelIndex) onChannelLongPress;

  @override
  State<ChannelLabelList> createState() => _ChannelLabelListState();
}

class _ChannelLabelListState extends State<ChannelLabelList> {
  bool _expanded = true;

  @override
  Widget build(BuildContext context) {
    return AnimatedContainer(
      duration: const Duration(milliseconds: 200),
      width: _expanded ? 56 : 16,
      color: const Color(0xFF161B22),
      child: _expanded ? _buildExpanded() : _buildCollapsed(),
    );
  }

  Widget _buildCollapsed() => GestureDetector(
        onTap: () => setState(() => _expanded = true),
        child: const Center(
          child: Icon(Icons.chevron_right, color: Color(0xFF8B949E), size: 14),
        ),
      );

  Widget _buildExpanded() => Column(
        children: [
          // Collapse handle
          GestureDetector(
            onTap: () => setState(() => _expanded = false),
            child: const SizedBox(
              height: 24,
              child: Center(
                child: Icon(Icons.chevron_left,
                    color: Color(0xFF8B949E), size: 14),
              ),
            ),
          ),
          // Analog channel labels
          for (final t in widget.session.analogTracks)
            _ChannelLabel(
              label: t.label,
              color: AppTheme.analogColor(t.adcIndex),
              height: widget.analogTrackHeight,
              onLongPress: null, // analog tracks don't have decoder config
            ),
          // Divider
          if (widget.session.hasAnalog)
            const Divider(height: 1, color: Color(0xFF30363D)),
          // Digital channel labels
          for (final t in widget.session.digitalTracks)
            _ChannelLabel(
              label: t.label,
              color: AppTheme.channelColor(t.channelIndex),
              height: widget.trackHeight,
              onLongPress: () => widget.onChannelLongPress(t.channelIndex),
            ),
        ],
      );
}

class _ChannelLabel extends StatelessWidget {
  const _ChannelLabel({
    required this.label,
    required this.color,
    required this.height,
    required this.onLongPress,
  });

  final String label;
  final Color color;
  final double height;
  final VoidCallback? onLongPress;

  @override
  Widget build(BuildContext context) => GestureDetector(
        onLongPress: onLongPress,
        child: SizedBox(
          height: height,
          child: Row(
            children: [
              Container(width: 3, height: height * 0.6, color: color),
              const SizedBox(width: 4),
              Expanded(
                child: Text(
                  label,
                  style: TextStyle(
                    color: color,
                    fontSize: 9,
                    fontWeight: FontWeight.w600,
                  ),
                  maxLines: 1,
                  overflow: TextOverflow.clip,
                ),
              ),
            ],
          ),
        ),
      );
}
