import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/controllers/capture_controller.dart';

/// Full-screen shown while waiting for the trigger and downloading data.
///
/// Transitions automatically to the waveform viewer when capture completes.
class CaptureRunScreen extends ConsumerWidget {
  const CaptureRunScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final state = ref.watch(captureControllerProvider);

    // Navigate to waveform viewer on completion.
    ref.listen<CaptureState>(captureControllerProvider, (_, next) {
      if (next is CaptureComplete) {
        WidgetsBinding.instance.addPostFrameCallback((_) {
          Navigator.of(context).pushReplacementNamed('/waveform');
        });
      }
    });

    return Scaffold(
      backgroundColor: const Color(0xFF0D1117),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        elevation: 0,
        leading: IconButton(
          icon: const Icon(Icons.close),
          onPressed: () {
            ref.read(captureControllerProvider.notifier).reset();
            Navigator.of(context).pop();
          },
        ),
      ),
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            _buildStatusWidget(context, state),
            const SizedBox(height: 32),
            _buildStatusText(state),
            const SizedBox(height: 16),
            _buildSubText(state),
          ],
        ),
      ),
    );
  }

  Widget _buildStatusWidget(BuildContext context, CaptureState state) {
    if (state is CaptureDownloading) {
      return SizedBox(
        width: 120,
        height: 120,
        child: CircularProgressIndicator(
          value: state.progress,
          strokeWidth: 6,
          color: const Color(0xFF58A6FF),
          backgroundColor: const Color(0xFF21262D),
        ),
      );
    }

    if (state is CaptureError) {
      return const Icon(Icons.error_outline, size: 80, color: Color(0xFFF85149));
    }

    // Armed / Arming — pulsing animation.
    return _PulsingArmedWidget();
  }

  Widget _buildStatusText(CaptureState state) {
    String text;
    Color color;
    if (state is CaptureArming) {
      text = 'Arming…';
      color = const Color(0xFF8B949E);
    } else if (state is CaptureArmed) {
      text = 'Armed — waiting for trigger';
      color = const Color(0xFFD2A8FF);
    } else if (state is CaptureDownloading) {
      text = 'Downloading';
      color = const Color(0xFF58A6FF);
    } else if (state is CaptureError) {
      text = 'Capture failed';
      color = const Color(0xFFF85149);
    } else {
      text = '';
      color = Colors.transparent;
    }
    return Text(
      text,
      style: TextStyle(
        fontSize: 20,
        fontWeight: FontWeight.w600,
        color: color,
      ),
    );
  }

  Widget _buildSubText(CaptureState state) {
    if (state is CaptureDownloading) {
      return Text(
        '${(state.progress * 100).round()}%',
        style: const TextStyle(
          fontSize: 14,
          color: Color(0xFF8B949E),
        ),
      );
    }
    if (state is CaptureError) {
      return Padding(
        padding: const EdgeInsets.symmetric(horizontal: 32),
        child: Text(
          state.message,
          style: const TextStyle(
            fontSize: 13,
            color: Color(0xFF8B949E),
          ),
          textAlign: TextAlign.center,
        ),
      );
    }
    return const SizedBox.shrink();
  }
}

class _PulsingArmedWidget extends StatefulWidget {
  @override
  State<_PulsingArmedWidget> createState() => _PulsingArmedWidgetState();
}

class _PulsingArmedWidgetState extends State<_PulsingArmedWidget>
    with SingleTickerProviderStateMixin {
  late final AnimationController _controller;
  late final Animation<double> _scale;

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1200),
    )..repeat(reverse: true);
    _scale = Tween<double>(begin: 0.85, end: 1.0).animate(
      CurvedAnimation(parent: _controller, curve: Curves.easeInOut),
    );
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => ScaleTransition(
        scale: _scale,
        child: Container(
          width: 120,
          height: 120,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            color: const Color(0xFFD2A8FF).withAlpha(26),
            border: Border.all(
              color: const Color(0xFFD2A8FF),
              width: 2,
            ),
          ),
          child: const Icon(
            Icons.fiber_manual_record,
            color: Color(0xFFD2A8FF),
            size: 48,
          ),
        ),
      );
}
