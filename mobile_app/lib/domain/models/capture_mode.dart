/// Operating mode for a capture request.
enum CaptureMode {
  /// Logic-only capture: 16 digital channels, no analog.
  logicOnly,

  /// Mixed-signal capture: up to 16 digital + up to 3 analog (A0/A1/A2).
  mixedSignal,
}
