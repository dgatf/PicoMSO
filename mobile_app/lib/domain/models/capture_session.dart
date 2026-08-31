import 'package:picomso/domain/models/capture_request.dart';
import 'package:picomso/domain/models/digital_track.dart';
import 'package:picomso/domain/models/analog_track.dart';

/// Immutable result of a completed capture.
class CaptureSession {
  const CaptureSession({
    required this.request,
    required this.capturedAt,
    required this.actualPreTriggerSamples,
    required this.digitalTracks,
    required this.analogTracks,
  });

  final CaptureRequest request;
  final DateTime capturedAt;

  /// Actual pre-trigger sample count returned by firmware.
  /// May be less than [CaptureRequest.preTriggerSamples].
  final int actualPreTriggerSamples;

  final List<DigitalTrack> digitalTracks;
  final List<AnalogTrack> analogTracks;

  /// Total capture duration in nanoseconds, derived from request.
  double get totalDurationNs => request.totalDurationNs;

  /// Sample period in nanoseconds.
  double get samplePeriodNs => request.samplePeriodNs;

  /// True if this session has any analog data.
  bool get hasAnalog => analogTracks.isNotEmpty;

  /// Number of digital channels present.
  int get digitalChannelCount => digitalTracks.length;

  /// Number of analog channels present.
  int get analogChannelCount => analogTracks.length;
}
