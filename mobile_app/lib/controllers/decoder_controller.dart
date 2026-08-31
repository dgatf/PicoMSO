import 'dart:isolate';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/controllers/capture_controller.dart';
import 'package:picomso/domain/decoders/decoder_interface.dart';
import 'package:picomso/domain/decoders/uart_decoder.dart';
import 'package:picomso/domain/decoders/spi_decoder.dart';
import 'package:picomso/domain/decoders/i2c_decoder.dart';
import 'package:picomso/domain/models/capture_session.dart';
import 'package:picomso/domain/models/decoder_result.dart';

// ---------------------------------------------------------------------------
// Decoder config state
// ---------------------------------------------------------------------------

/// Active decoder configurations keyed by channel index.
class DecoderConfigState {
  const DecoderConfigState({this.configs = const {}});

  final Map<int, DecoderConfig> configs;

  DecoderConfigState withConfig(int channelIndex, DecoderConfig config) =>
      DecoderConfigState(
        configs: {...configs, channelIndex: config},
      );

  DecoderConfigState withoutConfig(int channelIndex) {
    final m = Map<int, DecoderConfig>.from(configs);
    m.remove(channelIndex);
    return DecoderConfigState(configs: m);
  }
}

class DecoderConfigController extends Notifier<DecoderConfigState> {
  @override
  DecoderConfigState build() => const DecoderConfigState();

  void setConfig(int channelIndex, DecoderConfig config) {
    state = state.withConfig(channelIndex, config);
  }

  void removeConfig(int channelIndex) {
    state = state.withoutConfig(channelIndex);
  }
}

final decoderConfigProvider =
    NotifierProvider<DecoderConfigController, DecoderConfigState>(
        DecoderConfigController.new);

// ---------------------------------------------------------------------------
// Decoder results (computed async in isolate)
// ---------------------------------------------------------------------------

/// Runs all active decoders against the current [CaptureSession].
///
/// Re-runs automatically when either the session or the decoder config changes.
final decoderResultsProvider =
    FutureProvider<List<DecoderResult>>((ref) async {
  final session = ref.watch(captureSessionProvider);
  final configs = ref.watch(decoderConfigProvider).configs;

  if (session == null || configs.isEmpty) return const [];

  return Isolate.run(() => _runDecoders(session, configs));
});

List<DecoderResult> _runDecoders(
  CaptureSession session,
  Map<int, DecoderConfig> configs,
) {
  final results = <DecoderResult>[];
  for (final entry in configs.entries) {
    final config = entry.value;
    try {
      final decoded = _dispatchDecoder(session, config);
      results.addAll(decoded);
    } catch (_) {
      // Decoder errors are non-fatal; skip and continue.
    }
  }
  results.sort((a, b) => a.startSample.compareTo(b.startSample));
  return results;
}

List<DecoderResult> _dispatchDecoder(
  CaptureSession session,
  DecoderConfig config,
) {
  if (config is UartDecoderConfig) {
    return const UartDecoder().decode(session, config);
  }
  if (config is SpiDecoderConfig) {
    return const SpiDecoder().decode(session, config);
  }
  if (config is I2cDecoderConfig) {
    return const I2cDecoder().decode(session, config);
  }
  return const [];
}
