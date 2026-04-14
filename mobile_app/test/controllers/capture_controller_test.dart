import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/controllers/capture_controller.dart';
import 'package:picomso/domain/models/capture_mode.dart';
import 'package:picomso/domain/models/capture_request.dart';

void main() {
  group('CaptureController state machine', () {
    late ProviderContainer container;
    late CaptureController controller;

    setUp(() {
      container = ProviderContainer();
      controller = container.read(captureControllerProvider.notifier);
    });

    tearDown(() => container.dispose());

    test('initial state is CaptureIdle', () {
      expect(container.read(captureControllerProvider), isA<CaptureIdle>());
    });

    test('reset() from any state returns to CaptureIdle', () {
      controller.reset();
      expect(container.read(captureControllerProvider), isA<CaptureIdle>());
    });

    test('captureSessionProvider returns null when idle', () {
      expect(container.read(captureSessionProvider), isNull);
    });

    test('state is CaptureArming immediately after arm()', () async {
      final request = CaptureRequest(
        mode: CaptureMode.logicOnly,
        totalSamples: 1024,
        sampleRateHz: 1000000,
        preTriggerSamples: 100,
        triggers: const [],
        analogChannelsMask: 0,
      );
      // arm() is async and will fail because the USB transport is a stub.
      // We just verify the state transitions to CaptureArming first.
      final future = controller.arm(request);
      // Immediately after calling arm(), state should be CaptureArming.
      expect(container.read(captureControllerProvider), isA<CaptureArming>());
      // Await the future; it will throw because USB transport is unimplemented.
      await future;
      // After error, state should be CaptureError.
      expect(container.read(captureControllerProvider), isA<CaptureError>());
    });

    test('CaptureError contains request reference', () async {
      final request = CaptureRequest(
        mode: CaptureMode.logicOnly,
        totalSamples: 1024,
        sampleRateHz: 1000000,
        preTriggerSamples: 0,
        triggers: const [],
        analogChannelsMask: 0,
      );
      await controller.arm(request);
      final state = container.read(captureControllerProvider);
      expect(state, isA<CaptureError>());
      expect((state as CaptureError).request, request);
    });
  });
}
