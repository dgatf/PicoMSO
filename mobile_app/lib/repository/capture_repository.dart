import 'dart:typed_data';
import 'dart:isolate';

import 'package:picomso/domain/models/analog_track.dart';
import 'package:picomso/domain/models/capture_request.dart';
import 'package:picomso/domain/models/capture_session.dart';
import 'package:picomso/domain/models/capture_mode.dart';
import 'package:picomso/domain/models/digital_track.dart';
import 'package:picomso/protocol/protocol_codec.dart';
import 'package:picomso/protocol/protocol_constants.dart';
import 'package:picomso/transport/transport_interface.dart';

/// Orchestrates one complete capture cycle:
///   SET_MODE → REQUEST_CAPTURE → READ_DATA_BLOCK loop → CaptureSession
///
/// Multi-stream note (mixed-signal mode): logic and scope data blocks arrive
/// interleaved with independent TERMINAL flags.  This repository accumulates
/// both streams and only assembles the [CaptureSession] once both streams
/// have received their TERMINAL block.
class CaptureRepository {
  CaptureRepository(this._transport, this._codec);

  final Transport _transport;
  final ProtocolCodec _codec;

  /// Execute a full capture and return a [CaptureSession].
  ///
  /// [onProgress] is called with a fraction in [0, 1] as blocks arrive.
  Future<CaptureSession> runCapture(
    CaptureRequest request, {
    void Function(double progress)? onProgress,
  }) async {
    // 1. SET_MODE
    final streamsMask = _streamsForMode(request.mode);
    await _transport.sendControl(_codec.encodeSetMode(streamsMask));
    final setModeResp = await _transport.readDataBlock();
    _codec.decodeAck(setModeResp); // throws on error

    // 2. REQUEST_CAPTURE
    await _transport.sendControl(_codec.encodeRequestCapture(request));
    final captureResp = await _transport.readDataBlock();
    _codec.decodeAck(captureResp); // throws on error

    // 3. READ_DATA_BLOCK loop
    final logicBlocks = <Uint8List>[];
    final scopeBlocks = <Uint8List>[];

    bool logicDone = request.mode == CaptureMode.mixedSignal ? false : false;
    bool scopeDone = request.mode == CaptureMode.mixedSignal ? false : true;

    // For logic-only mode we only wait for the logic stream.
    if (request.mode == CaptureMode.logicOnly) {
      logicDone = false;
      scopeDone = true;
    } else {
      logicDone = false;
      scopeDone = false;
    }

    int blockCount = 0;
    final expectedBlocks = (request.totalSamples / kDataBlockSize).ceil() + 4;

    while (!logicDone || !scopeDone) {
      await _transport.sendControl(_codec.encodeReadDataBlock());
      final raw = await _transport.readDataBlock();
      final block = _codec.decodeDataBlock(raw);

      if (block.streamId == kStreamIdLogic) {
        logicBlocks.add(block.payload);
        if (block.isTerminal) logicDone = true;
      } else if (block.streamId == kStreamIdScope) {
        scopeBlocks.add(block.payload);
        if (block.isTerminal) scopeDone = true;
      }

      blockCount++;
      onProgress?.call(
        (blockCount / expectedBlocks).clamp(0.0, 0.95),
      );
    }

    onProgress?.call(1.0);

    // 4. Assemble CaptureSession off the UI thread.
    final session = await Isolate.run(
      () => _assembleSession(request, logicBlocks, scopeBlocks),
    );
    return session;
  }

  // ---------------------------------------------------------------------------
  // Private helpers (safe to call from isolate)
  // ---------------------------------------------------------------------------

  static CaptureSession _assembleSession(
    CaptureRequest request,
    List<Uint8List> logicBlocks,
    List<Uint8List> scopeBlocks,
  ) {
    final digital = _buildDigitalTracks(request, logicBlocks);
    final analog = _buildAnalogTracks(request, scopeBlocks);
    return CaptureSession(
      request: request,
      capturedAt: DateTime.now(),
      actualPreTriggerSamples: request.preTriggerSamples, // firmware reports via data
      digitalTracks: digital,
      analogTracks: analog,
    );
  }

  /// Assemble raw logic bytes into 16 [DigitalTrack] objects.
  ///
  /// The firmware packs 2 bytes per sample (16 channels, 1 bit each,
  /// little-endian uint16).  We unpack and repack channel-by-channel.
  static List<DigitalTrack> _buildDigitalTracks(
    CaptureRequest request,
    List<Uint8List> blocks,
  ) {
    // Concatenate all raw bytes.
    int totalBytes = blocks.fold(0, (s, b) => s + b.length);
    final raw = Uint8List(totalBytes);
    int offset = 0;
    for (final b in blocks) {
      raw.setRange(offset, offset + b.length, b);
      offset += b.length;
    }

    // Each sample is 2 bytes (uint16 LE), 1 bit per channel.
    final sampleCount = (raw.length ~/ 2).clamp(0, request.totalSamples);
    final bd = ByteData.sublistView(raw);

    // Pre-allocate packed bit arrays per channel.
    final packedLen = (sampleCount + 7) ~/ 8;
    final channelBits = List.generate(16, (_) => Uint8List(packedLen));

    for (int s = 0; s < sampleCount; s++) {
      final word = s * 2 < raw.length - 1
          ? bd.getUint16(s * 2, Endian.little)
          : 0;
      for (int ch = 0; ch < 16; ch++) {
        if ((word >> ch) & 1 == 1) {
          channelBits[ch][s >> 3] |= (1 << (s & 7));
        }
      }
    }

    return List.generate(
      16,
      (ch) => DigitalTrack(
        channelIndex: ch,
        label: 'D\$ch',
        packedBits: channelBits[ch],
        totalSamples: sampleCount,
      ),
    );
  }

  /// Assemble raw scope bytes into [AnalogTrack] objects.
  ///
  /// The firmware streams interleaved 2-byte little-endian ADC samples
  /// (12-bit, 0-4095) for the enabled channels in ascending index order.
  /// We demultiplex using the same phase counter as the libsigrok driver.
  static List<AnalogTrack> _buildAnalogTracks(
    CaptureRequest request,
    List<Uint8List> blocks,
  ) {
    final mask = request.analogChannelsMask & 0x07;
    if (mask == 0 || blocks.isEmpty) return const [];

    // Build ordered list of enabled ADC indices (ascending).
    final enabledIndices = <int>[];
    for (int i = 0; i < 3; i++) {
      if ((mask >> i) & 1 == 1) enabledIndices.add(i);
    }
    final numChannels = enabledIndices.length;

    // Concatenate all raw scope bytes.
    int totalBytes = blocks.fold(0, (s, b) => s + b.length);
    final raw = Uint8List(totalBytes);
    int off = 0;
    for (final b in blocks) {
      raw.setRange(off, off + b.length, b);
      off += b.length;
    }

    final bd = ByteData.sublistView(raw);
    final totalInterleaved = raw.length ~/ 2;
    final perChannelCount = (totalInterleaved / numChannels).floor();

    // Allocate per-channel sample buffers.
    final channelSamples = List.generate(
      numChannels,
      (_) => Float32List(perChannelCount),
    );

    // Demultiplex: phase counter cycles through enabled channels.
    int phase = 0;

    // Initialize per-channel counters.
    final counts = List.filled(numChannels, 0);

    for (int i = 0; i * 2 + 1 < raw.length; i++) {
      final rawAdc = bd.getUint16(i * 2, Endian.little) & 0x0FFF;
      final normalized = rawAdc / 4095.0;
      if (counts[phase] < perChannelCount) {
        channelSamples[phase][counts[phase]] = normalized;
        counts[phase]++;
      }
      phase = (phase + 1) % numChannels;
    }

    return List.generate(
      numChannels,
      (i) => AnalogTrack(
        adcIndex: enabledIndices[i],
        label: 'A\${enabledIndices[i]}',
        samples: channelSamples[i],
        vRef: 3.3,
      ),
    );
  }

  static int _streamsForMode(CaptureMode mode) {
    switch (mode) {
      case CaptureMode.logicOnly:
        return kStreamLogic;
      case CaptureMode.mixedSignal:
        return kStreamBoth;
    }
  }
}
