import 'package:picomso/domain/models/capture_mode.dart';
import 'package:picomso/domain/models/trigger_config.dart';

/// Immutable capture request; maps directly to picomso_request_capture_request_t.
class CaptureRequest {
  const CaptureRequest({
    required this.mode,
    required this.totalSamples,
    required this.sampleRateHz,
    required this.preTriggerSamples,
    required this.triggers,
    required this.analogChannelsMask,
  });

  final CaptureMode mode;

  /// Total interleaved sample count sent to firmware.
  /// For logic-only: equals per-channel sample count.
  /// For mixed-signal: per-channel * popcount(analogChannelsMask).
  final int totalSamples;

  /// Sample rate in Hz (single global field; firmware rescales ADC if needed).
  final int sampleRateHz;

  /// Number of pre-trigger samples requested (may differ from actual).
  final int preTriggerSamples;

  /// Up to 4 trigger configurations.
  final List<TriggerConfig> triggers;

  /// Bitmask of enabled ADC inputs (bits 0-2 = A0/A1/A2).
  /// 0x00 for logic-only mode (firmware treats as A0-only when scope enabled).
  final int analogChannelsMask;

  /// Sample period in nanoseconds.
  double get samplePeriodNs => 1e9 / sampleRateHz;

  /// Total capture duration in nanoseconds.
  double get totalDurationNs => totalSamples * samplePeriodNs;

  CaptureRequest copyWith({
    CaptureMode? mode,
    int? totalSamples,
    int? sampleRateHz,
    int? preTriggerSamples,
    List<TriggerConfig>? triggers,
    int? analogChannelsMask,
  }) =>
      CaptureRequest(
        mode: mode ?? this.mode,
        totalSamples: totalSamples ?? this.totalSamples,
        sampleRateHz: sampleRateHz ?? this.sampleRateHz,
        preTriggerSamples: preTriggerSamples ?? this.preTriggerSamples,
        triggers: triggers ?? this.triggers,
        analogChannelsMask: analogChannelsMask ?? this.analogChannelsMask,
      );

  @override
  bool operator ==(Object other) =>
      other is CaptureRequest &&
      other.mode == mode &&
      other.totalSamples == totalSamples &&
      other.sampleRateHz == sampleRateHz &&
      other.preTriggerSamples == preTriggerSamples &&
      other.analogChannelsMask == analogChannelsMask;

  @override
  int get hashCode => Object.hash(
        mode,
        totalSamples,
        sampleRateHz,
        preTriggerSamples,
        analogChannelsMask,
      );
}
