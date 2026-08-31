/// Immutable viewport state: what portion of the waveform is currently visible.
///
/// The time axis is in nanoseconds from the start of the capture (t=0).
/// [visibleStartNs] is the left edge; [nsPerPixel] is the zoom level.
class ViewportState {
  const ViewportState({
    required this.visibleStartNs,
    required this.nsPerPixel,
    this.cursorANs,
    this.cursorBNs,
    this.canvasWidthPx = 0,
  });

  /// Left edge of the visible window in nanoseconds.
  final double visibleStartNs;

  /// Zoom level: nanoseconds represented by one display pixel.
  final double nsPerPixel;

  /// Cursor A position in nanoseconds (null if cursor disabled).
  final double? cursorANs;

  /// Cursor B position in nanoseconds (null if cursor disabled).
  final double? cursorBNs;

  /// Current canvas width in logical pixels (set by the layout system).
  final double canvasWidthPx;

  /// Right edge of the visible window.
  double get visibleEndNs => visibleStartNs + nsPerPixel * canvasWidthPx;

  /// Cursor delta (B - A) if both cursors are active.
  double? get cursorDeltaNs {
    if (cursorANs == null || cursorBNs == null) return null;
    return cursorBNs! - cursorANs!;
  }

  /// Convert a nanosecond position to a canvas x pixel.
  double nsToPixel(double ns) => (ns - visibleStartNs) / nsPerPixel;

  /// Convert a canvas x pixel to nanoseconds.
  double pixelToNs(double px) => visibleStartNs + px * nsPerPixel;

  /// Convert a nanosecond position to a sample index for the given rate.
  int nsToSampleIndex(double ns, double samplePeriodNs) {
    return (ns / samplePeriodNs).floor();
  }

  /// Clamp and return a new viewport zoomed by [factor] around [focalPixel].
  ViewportState zoom(double factor, double focalPixel, double totalDurationNs) {
    final focalNs = pixelToNs(focalPixel);
    final newNsPerPixel = (nsPerPixel * factor).clamp(0.1, totalDurationNs);
    final newStart = (focalNs - focalPixel * newNsPerPixel)
        .clamp(0, totalDurationNs - newNsPerPixel * canvasWidthPx);
    return copyWith(
      nsPerPixel: newNsPerPixel,
      visibleStartNs: newStart.toDouble(),
    );
  }

  /// Pan by [deltaPx] pixels.
  ViewportState pan(double deltaPx, double totalDurationNs) {
    final newStart = (visibleStartNs - deltaPx * nsPerPixel)
        .clamp(0, (totalDurationNs - nsPerPixel * canvasWidthPx).clamp(0, totalDurationNs));
    return copyWith(visibleStartNs: newStart.toDouble());
  }

  /// Return a zoom-to-fit viewport for the given total duration.
  static ViewportState zoomToFit(double totalDurationNs, double canvasWidthPx) {
    final nspp = canvasWidthPx > 0 ? totalDurationNs / canvasWidthPx : 1.0;
    return ViewportState(
      visibleStartNs: 0,
      nsPerPixel: nspp,
      canvasWidthPx: canvasWidthPx,
    );
  }

  ViewportState copyWith({
    double? visibleStartNs,
    double? nsPerPixel,
    double? Function()? cursorANs,
    double? Function()? cursorBNs,
    double? canvasWidthPx,
  }) =>
      ViewportState(
        visibleStartNs: visibleStartNs ?? this.visibleStartNs,
        nsPerPixel: nsPerPixel ?? this.nsPerPixel,
        cursorANs: cursorANs != null ? cursorANs() : this.cursorANs,
        cursorBNs: cursorBNs != null ? cursorBNs() : this.cursorBNs,
        canvasWidthPx: canvasWidthPx ?? this.canvasWidthPx,
      );

  @override
  bool operator ==(Object other) =>
      other is ViewportState &&
      other.visibleStartNs == visibleStartNs &&
      other.nsPerPixel == nsPerPixel &&
      other.cursorANs == cursorANs &&
      other.cursorBNs == cursorBNs &&
      other.canvasWidthPx == canvasWidthPx;

  @override
  int get hashCode => Object.hash(
        visibleStartNs,
        nsPerPixel,
        cursorANs,
        cursorBNs,
        canvasWidthPx,
      );
}
