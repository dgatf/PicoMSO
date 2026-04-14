import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/controllers/device_controller.dart';
import 'package:picomso/domain/models/capture_request.dart';
import 'package:picomso/domain/models/capture_session.dart';
import 'package:picomso/protocol/protocol_codec.dart';
import 'package:picomso/repository/capture_repository.dart';

// ---------------------------------------------------------------------------
// Capture lifecycle state machine
// ---------------------------------------------------------------------------

/// All states of the capture lifecycle.
sealed class CaptureState {
  const CaptureState();
}

/// No active capture; ready to arm.
final class CaptureIdle extends CaptureState {
  const CaptureIdle();
}

/// SET_MODE + REQUEST_CAPTURE sent; waiting for firmware ACK.
final class CaptureArming extends CaptureState {
  const CaptureArming(this.request);
  final CaptureRequest request;
}

/// ACK received; firmware is waiting for trigger condition.
final class CaptureArmed extends CaptureState {
  const CaptureArmed(this.request);
  final CaptureRequest request;
}

/// Trigger has fired; downloading data blocks from the device.
final class CaptureDownloading extends CaptureState {
  const CaptureDownloading(this.request, this.progress);
  final CaptureRequest request;

  /// Download progress in [0.0, 1.0].
  final double progress;
}

/// Capture complete; session is available.
final class CaptureComplete extends CaptureState {
  const CaptureComplete(this.session);
  final CaptureSession session;
}

/// An error occurred during any phase.
final class CaptureError extends CaptureState {
  const CaptureError(this.message, {this.request});
  final String message;
  final CaptureRequest? request;
}

// ---------------------------------------------------------------------------
// Provider
// ---------------------------------------------------------------------------

final captureRepositoryProvider = Provider<CaptureRepository>((ref) {
  return CaptureRepository(
    ref.read(usbTransportProvider),
    ProtocolCodec(),
  );
});

/// Controller that drives the capture state machine.
class CaptureController extends Notifier<CaptureState> {
  @override
  CaptureState build() => const CaptureIdle();

  /// Begin a capture with [request].
  Future<void> arm(CaptureRequest request) async {
    state = CaptureArming(request);

    try {
      final repo = ref.read(captureRepositoryProvider);
      state = CaptureArmed(request);

      final session = await repo.runCapture(
        request,
        onProgress: (p) {
          // Transition to downloading on first progress callback.
          if (state is CaptureArmed) {
            state = CaptureDownloading(request, p);
          } else if (state is CaptureDownloading) {
            state = CaptureDownloading(request, p);
          }
        },
      );

      state = CaptureComplete(session);
    } on Exception catch (e) {
      state = CaptureError(e.toString(), request: request);
    }
  }

  /// Reset to idle state (e.g. after an error or when user dismisses).
  void reset() => state = const CaptureIdle();
}

final captureControllerProvider =
    NotifierProvider<CaptureController, CaptureState>(CaptureController.new);

/// Derived provider that exposes the current [CaptureSession] if available.
final captureSessionProvider = Provider<CaptureSession?>((ref) {
  final state = ref.watch(captureControllerProvider);
  if (state is CaptureComplete) return state.session;
  return null;
});
