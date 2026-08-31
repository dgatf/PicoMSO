/// One trigger slot (maps to picomso_trigger_config_t).
class TriggerConfig {
  const TriggerConfig({
    required this.isEnabled,
    required this.pin,
    required this.match,
  });

  final bool isEnabled;

  /// GPIO pin number (0-15 for logic channels).
  final int pin;

  final TriggerMatch match;

  TriggerConfig copyWith({
    bool? isEnabled,
    int? pin,
    TriggerMatch? match,
  }) =>
      TriggerConfig(
        isEnabled: isEnabled ?? this.isEnabled,
        pin: pin ?? this.pin,
        match: match ?? this.match,
      );

  @override
  bool operator ==(Object other) =>
      other is TriggerConfig &&
      other.isEnabled == isEnabled &&
      other.pin == pin &&
      other.match == match;

  @override
  int get hashCode => Object.hash(isEnabled, pin, match);
}

/// Trigger match condition (picomso_trigger_match_t).
enum TriggerMatch {
  levelLow(0x00),
  levelHigh(0x01),
  edgeLow(0x02),
  edgeHigh(0x03);

  const TriggerMatch(this.wireValue);

  /// Wire value sent in the REQUEST_CAPTURE payload.
  final int wireValue;

  static TriggerMatch fromWire(int value) {
    return TriggerMatch.values.firstWhere(
      (e) => e.wireValue == value,
      orElse: () => throw ArgumentError('Unknown TriggerMatch wire value: $value'),
    );
  }

  String get displayName {
    switch (this) {
      case TriggerMatch.levelLow:
        return 'Low level';
      case TriggerMatch.levelHigh:
        return 'High level';
      case TriggerMatch.edgeLow:
        return 'Falling edge';
      case TriggerMatch.edgeHigh:
        return 'Rising edge';
    }
  }
}
