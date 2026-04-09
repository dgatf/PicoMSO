import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/controllers/capture_controller.dart';
import 'package:picomso/domain/models/capture_session.dart';
import 'package:picomso/domain/models/viewport_state.dart';

/// Controller for the waveform viewport (pan, zoom, cursors).
class ViewportController extends Notifier<ViewportState> {
  @override
  ViewportState build() {
    // When a new session arrives, reset to zoom-to-fit.
    ref.listen<CaptureSession?>(
      captureSessionProvider,
      (_, session) {
        if (session != null) {
          state = ViewportState.zoomToFit(
            session.totalDurationNs,
            state.canvasWidthPx,
          );
        }
      },
    );
    return const ViewportState(
      visibleStartNs: 0,
      nsPerPixel: 1,
    );
  }

  /// Update canvas width (called by layout widget on size change).
  void setCanvasWidth(double widthPx) {
    if (state.canvasWidthPx == widthPx) return;
    state = state.copyWith(canvasWidthPx: widthPx);
  }

  /// Zoom around [focalPixel] by [factor].
  void zoom(double factor, double focalPixel) {
    final session = ref.read(captureSessionProvider);
    final totalNs = session?.totalDurationNs ?? state.nsPerPixel * state.canvasWidthPx;
    state = state.zoom(factor, focalPixel, totalNs);
  }

  /// Pan by [deltaPx] pixels.
  void pan(double deltaPx) {
    final session = ref.read(captureSessionProvider);
    final totalNs = session?.totalDurationNs ?? state.nsPerPixel * state.canvasWidthPx;
    state = state.pan(deltaPx, totalNs);
  }

  /// Zoom to fit the entire capture.
  void zoomToFit() {
    final session = ref.read(captureSessionProvider);
    if (session == null) return;
    state = ViewportState.zoomToFit(session.totalDurationNs, state.canvasWidthPx);
  }

  /// Place cursor A at [ns] nanoseconds.
  void setCursorA(double? ns) {
    state = state.copyWith(cursorANs: () => ns);
  }

  /// Place cursor B at [ns] nanoseconds.
  void setCursorB(double? ns) {
    state = state.copyWith(cursorBNs: () => ns);
  }

  /// Clear both cursors.
  void clearCursors() {
    state = state.copyWith(
      cursorANs: () => null,
      cursorBNs: () => null,
    );
  }
}

final viewportControllerProvider =
    NotifierProvider<ViewportController, ViewportState>(ViewportController.new);
