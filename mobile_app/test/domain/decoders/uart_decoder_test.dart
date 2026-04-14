import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:picomso/domain/decoders/uart_decoder.dart';
import 'package:picomso/domain/models/capture_request.dart';
import 'package:picomso/domain/models/capture_mode.dart';
import 'package:picomso/domain/models/capture_session.dart';
import 'package:picomso/domain/models/digital_track.dart';

void main() {
  group('UartDecoder', () {
    const decoder = UartDecoder();

    // -----------------------------------------------------------------------
    // Helper: build a synthetic capture session with one digital track.
    // -----------------------------------------------------------------------

    CaptureSession _makeSession({
      required int sampleRateHz,
      required Uint8List packedBits,
      required int totalSamples,
    }) {
      final track = DigitalTrack(
        channelIndex: 0,
        label: 'D0',
        packedBits: packedBits,
        totalSamples: totalSamples,
      );
      final request = CaptureRequest(
        mode: CaptureMode.logicOnly,
        totalSamples: totalSamples,
        sampleRateHz: sampleRateHz,
        preTriggerSamples: 0,
        triggers: const [],
        analogChannelsMask: 0,
      );
      return CaptureSession(
        request: request,
        capturedAt: DateTime.now(),
        actualPreTriggerSamples: 0,
        digitalTracks: [track],
        analogTracks: const [],
      );
    }

    /// Build a packed-bit UART frame for [value] at [baudRate] in a
    /// [sampleRateHz] session.  Encodes: idle(high) -> start(low) -> 8 bits
    /// (LSB first) -> stop(high).
    Uint8List _encodeUartFrame({
      required int value,
      required int baudRate,
      required int sampleRateHz,
      required int totalSamples,
    }) {
      final samplesPerBit = sampleRateHz ~/ baudRate;
      final bits = Uint8List((totalSamples + 7) ~/ 8)..fillRange(0, (totalSamples + 7) ~/ 8, 0xFF); // idle high

      void writeBit(int startSample, bool high) {
        for (int s = startSample; s < startSample + samplesPerBit && s < totalSamples; s++) {
          if (high) {
            bits[s >> 3] |= (1 << (s & 7));
          } else {
            bits[s >> 3] &= ~(1 << (s & 7));
          }
        }
      }

      int pos = samplesPerBit; // start after idle period
      writeBit(pos, false); pos += samplesPerBit; // start bit
      for (int i = 0; i < 8; i++) {
        writeBit(pos, (value >> i) & 1 == 1);
        pos += samplesPerBit;
      }
      writeBit(pos, true); // stop bit

      return bits;
    }

    test('decodes 0x55 at 9600 baud / 1 MHz sample rate', () {
      const baudRate = 9600;
      const sampleRate = 1000000;
      const totalSamples = 2000;
      final bits = _encodeUartFrame(
        value: 0x55,
        baudRate: baudRate,
        sampleRateHz: sampleRate,
        totalSamples: totalSamples,
      );
      final session = _makeSession(
        sampleRateHz: sampleRate,
        packedBits: bits,
        totalSamples: totalSamples,
      );
      final config = UartDecoderConfig(
        channelIndex: 0,
        baudRate: baudRate,
        format: ByteFormat.hex,
      );
      final results = decoder.decode(session, config);
      expect(results, isNotEmpty);
      expect(results.first.label, '0x55');
    });

    test('decodes 0x00 (all zeros)', () {
      const baudRate = 9600;
      const sampleRate = 1000000;
      const totalSamples = 2000;
      final bits = _encodeUartFrame(
        value: 0x00,
        baudRate: baudRate,
        sampleRateHz: sampleRate,
        totalSamples: totalSamples,
      );
      final session = _makeSession(
        sampleRateHz: sampleRate,
        packedBits: bits,
        totalSamples: totalSamples,
      );
      final config = UartDecoderConfig(
        channelIndex: 0,
        baudRate: baudRate,
        format: ByteFormat.hex,
      );
      final results = decoder.decode(session, config);
      expect(results, isNotEmpty);
      expect(results.first.label, '0x00');
    });

    test('decodes 0xFF (all ones)', () {
      const baudRate = 9600;
      const sampleRate = 1000000;
      const totalSamples = 2000;
      final bits = _encodeUartFrame(
        value: 0xFF,
        baudRate: baudRate,
        sampleRateHz: sampleRate,
        totalSamples: totalSamples,
      );
      final session = _makeSession(
        sampleRateHz: sampleRate,
        packedBits: bits,
        totalSamples: totalSamples,
      );
      final config = UartDecoderConfig(
        channelIndex: 0,
        baudRate: baudRate,
        format: ByteFormat.hex,
      );
      final results = decoder.decode(session, config);
      expect(results, isNotEmpty);
      expect(results.first.label, '0xFF');
    });

    test('format:binary produces correct binary string', () {
      const baudRate = 9600;
      const sampleRate = 1000000;
      const totalSamples = 2000;
      final bits = _encodeUartFrame(
        value: 0xA5,
        baudRate: baudRate,
        sampleRateHz: sampleRate,
        totalSamples: totalSamples,
      );
      final session = _makeSession(
        sampleRateHz: sampleRate,
        packedBits: bits,
        totalSamples: totalSamples,
      );
      final config = UartDecoderConfig(
        channelIndex: 0,
        baudRate: baudRate,
        format: ByteFormat.binary,
      );
      final results = decoder.decode(session, config);
      expect(results, isNotEmpty);
      expect(results.first.label, '0b10100101');
    });

    test('returns empty list when channel not in session', () {
      final session = _makeSession(
        sampleRateHz: 1000000,
        packedBits: Uint8List(10),
        totalSamples: 80,
      );
      final config = UartDecoderConfig(
        channelIndex: 5, // not in session
        baudRate: 9600,
      );
      final results = decoder.decode(session, config);
      expect(results, isEmpty);
    });

    test('returns empty list when baud rate exceeds sample rate', () {
      final session = _makeSession(
        sampleRateHz: 100,
        packedBits: Uint8List(10),
        totalSamples: 80,
      );
      final config = UartDecoderConfig(
        channelIndex: 0,
        baudRate: 9600, // bitPeriodSamples < 1
      );
      final results = decoder.decode(session, config);
      expect(results, isEmpty);
    });
  });
}
