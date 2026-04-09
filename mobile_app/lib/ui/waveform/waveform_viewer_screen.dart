import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/controllers/capture_controller.dart';
import 'package:picomso/controllers/decoder_controller.dart';
import 'package:picomso/controllers/viewport_controller.dart';
import 'package:picomso/domain/models/capture_mode.dart';
import 'package:picomso/domain/models/capture_session.dart';
import 'package:picomso/domain/models/min_max_pyramid.dart';
import 'package:picomso/ui/shared/theme/app_theme.dart';
import 'package:picomso/ui/waveform/painters/analog_track_painter.dart';
import 'package:picomso/ui/waveform/painters/cursor_painter.dart';
import 'package:picomso/ui/waveform/painters/decoder_overlay_painter.dart';
import 'package:picomso/ui/waveform/painters/digital_track_painter.dart';
import 'package:picomso/ui/waveform/painters/grid_painter.dart';
import 'package:picomso/ui/waveform/widgets/channel_label_list.dart';
import 'package:picomso/ui/waveform/widgets/cursor_readout_panel.dart';
import 'package:picomso/ui/waveform/widgets/decoder_result_panel.dart';
import 'package:picomso/ui/decoder_config/decoder_config_sheet.dart';
import 'package:picomso/ui/capture_setup/capture_setup_sheet.dart';

// ---------------------------------------------------------------------------
// Pyramid provider (one per analog track, computed async)
// ---------------------------------------------------------------------------

final _analogPyramidsProvider =
    FutureProvider<List<MinMaxPyramid?>>((ref) async {
  final session = ref.watch(captureSessionProvider);
  if (session == null) return const [];
  return Future.wait(
    session.analogTracks.map((t) => MinMaxPyramid.buildAsync(t.samples)),
  );
});

/// The core waveform viewer screen.
class WaveformViewerScreen extends ConsumerStatefulWidget {
  const WaveformViewerScreen({super.key});

  @override
  ConsumerState<WaveformViewerScreen> createState() =>
      _WaveformViewerScreenState();
}

class _WaveformViewerScreenState extends ConsumerState<WaveformViewerScreen> {
  static const double _digitalTrackHeight = 32.0;
  static const double _analogTrackHeight = 72.0;

  bool _showDecoderPanel = false;
  bool _cursorsActive = false;

  // Gesture state
  double? _lastPanX;
  double? _lastScaleDistance;

  @override
  Widget build(BuildContext context) {
    final session = ref.watch(captureSessionProvider);
    if (session == null) {
      return const Scaffold(
        body: Center(child: Text('No capture data')),
      );
    }

    final viewport = ref.watch(viewportControllerProvider);
    final decoderResults = ref.watch(decoderResultsProvider).valueOrNull ?? [];
    final pyramids = ref.watch(_analogPyramidsProvider).valueOrNull ?? [];

    final analogH = session.analogTracks.length * _analogTrackHeight;
    final digitalH = session.digitalTracks.length * _digitalTrackHeight;
    final totalH = analogH + (session.hasAnalog ? 1 : 0) + digitalH;

    return Scaffold(
      backgroundColor: AppTheme.canvasBackground,
      body: SafeArea(
        child: Column(
          children: [
            _buildTopBar(context, session),
            Expanded(
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  // Channel labels sidebar
                  ChannelLabelList(
                    session: session,
                    trackHeight: _digitalTrackHeight,
                    analogTrackHeight: _analogTrackHeight,
                    onChannelLongPress: (ch) => _showDecoderConfig(context, ch),
                  ),
                  // Waveform canvas
                  Expanded(
                    child: LayoutBuilder(
                      builder: (context, constraints) {
                        WidgetsBinding.instance.addPostFrameCallback((_) {
                          ref.read(viewportControllerProvider.notifier)
                              .setCanvasWidth(constraints.maxWidth);
                        });
                        return _buildCanvas(
                          context,
                          session,
                          viewport,
                          decoderResults,
                          pyramids,
                          constraints.maxWidth,
                          totalH,
                        );
                      },
                    ),
                  ),
                ],
              ),
            ),
            // Cursor readout
            if (_cursorsActive)
              Padding(
                padding: const EdgeInsets.all(8),
                child: CursorReadoutPanel(viewport: viewport),
              ),
            // Decoder panel
            if (_showDecoderPanel && decoderResults.isNotEmpty)
              DecoderResultPanel(
                results: decoderResults,
                onDismiss: () => setState(() => _showDecoderPanel = false),
              ),
          ],
        ),
      ),
      floatingActionButton: FloatingActionButton.small(
        heroTag: 'retrigger',
        onPressed: _retrigger,
        tooltip: 'Retrigger',
        child: const Icon(Icons.replay),
      ),
    );
  }

  Widget _buildTopBar(BuildContext context, CaptureSession session) {
    return Container(
      height: 44,
      color: const Color(0xFF161B22),
      padding: const EdgeInsets.symmetric(horizontal: 4),
      child: Row(
        children: [
          IconButton(
            icon: const Icon(Icons.arrow_back, size: 20),
            onPressed: () => Navigator.of(context).pop(),
          ),
          Text(
            session.request.mode == CaptureMode.logicOnly
                ? 'Logic Capture'
                : 'Mixed Signal',
            style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 14),
          ),
          const Spacer(),
          // Cursor toggle
          IconButton(
            icon: Icon(
              Icons.space_bar,
              size: 20,
              color: _cursorsActive
                  ? const Color(0xFF58A6FF)
                  : const Color(0xFF8B949E),
            ),
            tooltip: 'Cursors',
            onPressed: () => setState(() {
              _cursorsActive = !_cursorsActive;
              if (!_cursorsActive) {
                ref.read(viewportControllerProvider.notifier).clearCursors();
              }
            }),
          ),
          // Zoom-to-fit
          IconButton(
            icon: const Icon(Icons.fit_screen, size: 20),
            tooltip: 'Zoom to fit',
            onPressed: () =>
                ref.read(viewportControllerProvider.notifier).zoomToFit(),
          ),
          // Decoder panel toggle
          IconButton(
            icon: Icon(
              Icons.text_snippet_outlined,
              size: 20,
              color: _showDecoderPanel
                  ? const Color(0xFF58A6FF)
                  : const Color(0xFF8B949E),
            ),
            tooltip: 'Decoder results',
            onPressed: () =>
                setState(() => _showDecoderPanel = !_showDecoderPanel),
          ),
        ],
      ),
    );
  }

  Widget _buildCanvas(
    BuildContext context,
    CaptureSession session,
    dynamic viewport,
    List<dynamic> decoderResults,
    List<MinMaxPyramid?> pyramids,
    double canvasWidth,
    double totalH,
  ) {
    final samplePeriodNs = session.samplePeriodNs;
    final analogH = session.analogTracks.length * _analogTrackHeight;

    return GestureDetector(
      onScaleStart: _onScaleStart,
      onScaleUpdate: (d) => _onScaleUpdate(d, ref),
      onScaleEnd: (_) => _onScaleEnd(),
      onTapDown: _cursorsActive
          ? (d) => _placeCursor(d.localPosition.dx, ref)
          : null,
      child: Stack(
        children: [
          // Grid
          RepaintBoundary(
            child: CustomPaint(
              painter: GridPainter(viewport: viewport),
              size: Size(canvasWidth, totalH),
            ),
          ),
          // Analog signals
          if (session.hasAnalog)
            RepaintBoundary(
              child: CustomPaint(
                painter: AnalogTrackPainter(
                  tracks: session.analogTracks,
                  pyramids: pyramids,
                  viewport: viewport,
                  trackHeight: _analogTrackHeight,
                  samplePeriodNs: samplePeriodNs,
                ),
                size: Size(canvasWidth, analogH),
              ),
            ),
          // Analog/digital divider
          if (session.hasAnalog)
            Positioned(
              top: analogH,
              left: 0,
              right: 0,
              child: const Divider(
                height: 1,
                color: Color(0xFF30363D),
              ),
            ),
          // Digital signals
          Positioned(
            top: session.hasAnalog ? analogH + 1 : 0,
            left: 0,
            right: 0,
            child: RepaintBoundary(
              child: CustomPaint(
                painter: DigitalTrackPainter(
                  tracks: session.digitalTracks,
                  viewport: viewport,
                  trackHeight: _digitalTrackHeight,
                  trackOffsetY: 0,
                  samplePeriodNs: samplePeriodNs,
                ),
                size: Size(canvasWidth,
                    session.digitalTracks.length * _digitalTrackHeight),
              ),
            ),
          ),
          // Decoder overlays
          if (decoderResults.isNotEmpty)
            Positioned(
              top: session.hasAnalog ? analogH + 1 : 0,
              left: 0,
              right: 0,
              child: RepaintBoundary(
                child: CustomPaint(
                  painter: DecoderOverlayPainter(
                    results: List.from(decoderResults),
                    viewport: viewport,
                    trackHeight: _digitalTrackHeight,
                    trackOffsetY: 0,
                    samplePeriodNs: samplePeriodNs,
                    channelCount: session.digitalTracks.length,
                  ),
                  size: Size(canvasWidth,
                      session.digitalTracks.length * _digitalTrackHeight),
                ),
              ),
            ),
          // Cursors (own RepaintBoundary — drag does not invalidate signals)
          RepaintBoundary(
            child: CustomPaint(
              painter: CursorPainter(viewport: viewport),
              size: Size(canvasWidth, totalH),
            ),
          ),
        ],
      ),
    );
  }

  // -------------------------------------------------------------------------
  // Gesture handling
  // -------------------------------------------------------------------------

  void _onScaleStart(ScaleStartDetails d) {
    _lastPanX = d.focalPoint.dx;
    _lastScaleDistance = null;
  }

  void _onScaleUpdate(ScaleUpdateDetails d, WidgetRef ref) {
    final ctrl = ref.read(viewportControllerProvider.notifier);

    if (d.scale != 1.0) {
      // Pinch-to-zoom.
      ctrl.zoom(1.0 / d.scale, d.focalPoint.dx);
      _lastScaleDistance = d.scale;
    } else {
      // Pan.
      if (_lastPanX != null) {
        ctrl.pan(_lastPanX! - d.focalPoint.dx);
      }
    }
    _lastPanX = d.focalPoint.dx;
  }

  void _onScaleEnd() {
    _lastPanX = null;
    _lastScaleDistance = null;
  }

  void _placeCursor(double x, WidgetRef ref) {
    final viewport = ref.read(viewportControllerProvider);
    final ns = viewport.pixelToNs(x);
    final ctrl = ref.read(viewportControllerProvider.notifier);
    if (viewport.cursorANs == null) {
      ctrl.setCursorA(ns);
    } else if (viewport.cursorBNs == null) {
      ctrl.setCursorB(ns);
    } else {
      // Both set: move A.
      ctrl.setCursorA(ns);
      ctrl.setCursorB(null);
    }
  }

  void _showDecoderConfig(BuildContext context, int channelIndex) {
    showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      builder: (_) => ProviderScope(
        parent: ProviderScope.containerOf(context),
        child: DecoderConfigSheet(channelIndex: channelIndex),
      ),
    );
  }

  void _retrigger() {
    final session = ref.read(captureSessionProvider);
    if (session == null) return;
    showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      builder: (_) => ProviderScope(
        parent: ProviderScope.containerOf(context),
        child: CaptureSetupSheet(initialMode: session.request.mode),
      ),
    );
  }
}
