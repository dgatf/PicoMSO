/// Information returned by the GET_INFO command (picomso_info_response_t).
class DeviceInfo {
  const DeviceInfo({
    required this.protocolVersionMajor,
    required this.protocolVersionMinor,
    required this.firmwareId,
  });

  final int protocolVersionMajor;
  final int protocolVersionMinor;

  /// Firmware identifier string (e.g. "PicoMSO-0.1").
  final String firmwareId;

  String get versionString => '$protocolVersionMajor.$protocolVersionMinor';

  @override
  String toString() =>
      'DeviceInfo(fw=$firmwareId protocol=$versionString)';
}

/// Capabilities returned by GET_CAPABILITIES (picomso_capabilities_t).
class DeviceCapabilities {
  const DeviceCapabilities({
    required this.capabilityFlags,
    required this.maxLogicChannels,
    required this.maxAnalogChannels,
    required this.maxSampleRateLogicHz,
    required this.maxSampleRateScopeHz,
    required this.maxSamplesLogic,
    required this.maxSamplesScope,
  });

  final int capabilityFlags;
  final int maxLogicChannels;
  final int maxAnalogChannels;
  final int maxSampleRateLogicHz;
  final int maxSampleRateScopeHz;
  final int maxSamplesLogic;
  final int maxSamplesScope;
}

/// Live status returned by GET_STATUS (picomso_status_response_t).
class DeviceStatus {
  const DeviceStatus({
    required this.streamsMask,
    required this.isCapturing,
  });

  final int streamsMask;
  final bool isCapturing;
}
