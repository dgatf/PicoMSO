import 'package:picomso/domain/models/capture_mode.dart';
import 'package:picomso/domain/models/capture_request.dart';
import 'package:picomso/domain/models/trigger_config.dart';
import 'package:picomso/protocol/protocol_constants.dart';

/// Builds a validated [CaptureRequest] from user-supplied parameters.
///
/// Validates constraints imposed by the firmware protocol:
/// - Minimum 1 sample requested
/// - Total samples = per-channel × popcount(analogChannelsMask) for scope
/// - analog_channels bits 0-2 only; bit 0 forced when mask is 0 in mixed mode
class CaptureRequestBuilder {
  CaptureMode mode = CaptureMode.logicOnly;
  int perChannelSamples = 4096;
  int sampleRateHz = 1000000; // 1 MHz default
  double preTriggerFraction = 0.1; // 10 % pre-trigger
  List<TriggerConfig> triggers = [];
  int analogChannelsMask = 0x01; // A0 only

  /// Number of enabled analog channels.
  int get _analogChannelCount {
    int mask = analogChannelsMask & 0x07;
    int count = 0;
    while (mask != 0) {
      count += mask & 1;
      mask >>= 1;
    }
    return count;
  }

  /// Build and return a [CaptureRequest], throwing [ArgumentError] if invalid.
  CaptureRequest build() {
    if (perChannelSamples < 1) {
      throw ArgumentError.value(perChannelSamples, 'perChannelSamples', 'Must be >= 1');
    }
    if (sampleRateHz < 1) {
      throw ArgumentError.value(sampleRateHz, 'sampleRateHz', 'Must be >= 1');
    }

    final effectiveMask = mode == CaptureMode.mixedSignal
        ? (analogChannelsMask & 0x07) == 0
            ? 0x01
            : analogChannelsMask & 0x07
        : 0x00;

    final totalSamples = mode == CaptureMode.mixedSignal
        ? perChannelSamples * _analogChannelCount
        : perChannelSamples;

    final preTrigger = (perChannelSamples * preTriggerFraction).round().clamp(0, perChannelSamples - 1);

    final effectiveTriggers = triggers.take(kMaxTriggerCount).toList();

    return CaptureRequest(
      mode: mode,
      totalSamples: totalSamples,
      sampleRateHz: sampleRateHz,
      preTriggerSamples: preTrigger,
      triggers: effectiveTriggers,
      analogChannelsMask: effectiveMask,
    );
  }
}
