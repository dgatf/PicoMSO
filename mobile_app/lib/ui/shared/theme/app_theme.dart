import 'package:flutter/material.dart';

/// PicoMSO application theme.
class AppTheme {
  AppTheme._();

  // -------------------------------------------------------------------------
  // Channel colors
  // -------------------------------------------------------------------------

  static const List<Color> channelColors = [
    Color(0xFF4CAF50), // D0  green
    Color(0xFF2196F3), // D1  blue
    Color(0xFFFF9800), // D2  orange
    Color(0xFFE91E63), // D3  pink
    Color(0xFF9C27B0), // D4  purple
    Color(0xFF00BCD4), // D5  cyan
    Color(0xFFFFEB3B), // D6  yellow
    Color(0xFFFF5722), // D7  deep orange
    Color(0xFF8BC34A), // D8  light green
    Color(0xFF03A9F4), // D9  light blue
    Color(0xFFFF6F00), // D10 amber
    Color(0xFFAD1457), // D11 dark pink
    Color(0xFF6A1B9A), // D12 deep purple
    Color(0xFF00838F), // D13 dark cyan
    Color(0xFFF9A825), // D14 gold
    Color(0xFFBF360C), // D15 burnt orange
  ];

  static const List<Color> analogColors = [
    Color(0xFFF44336), // A0 red
    Color(0xFF00E5FF), // A1 electric blue
    Color(0xFF76FF03), // A2 lime
  ];

  static Color channelColor(int index) =>
      channelColors[index.clamp(0, channelColors.length - 1)];

  static Color analogColor(int index) =>
      analogColors[index.clamp(0, analogColors.length - 1)];

  // -------------------------------------------------------------------------
  // Waveform canvas colors
  // -------------------------------------------------------------------------

  static const Color canvasBackground = Color(0xFF0D1117);
  static const Color gridLine = Color(0xFF21262D);
  static const Color gridLineStrong = Color(0xFF30363D);
  static const Color cursorColor = Color(0xFFFFFFFF);
  static const Color cursorAColor = Color(0xFF58A6FF);
  static const Color cursorBColor = Color(0xFFF78166);
  static const Color triggerMarker = Color(0xFFD2A8FF);
  static const Color analogBaseline = Color(0xFF30363D);

  // -------------------------------------------------------------------------
  // Material theme
  // -------------------------------------------------------------------------

  static ThemeData get dark {
    return ThemeData(
      useMaterial3: true,
      colorScheme: const ColorScheme.dark(
        primary: Color(0xFF58A6FF),
        secondary: Color(0xFF3FB950),
        surface: Color(0xFF161B22),
        onSurface: Color(0xFFE6EDF3),
        error: Color(0xFFF85149),
      ),
      scaffoldBackgroundColor: const Color(0xFF0D1117),
      appBarTheme: const AppBarTheme(
        backgroundColor: Color(0xFF161B22),
        foregroundColor: Color(0xFFE6EDF3),
        elevation: 0,
      ),
      bottomSheetTheme: const BottomSheetThemeData(
        backgroundColor: Color(0xFF161B22),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.vertical(top: Radius.circular(16)),
        ),
      ),
      cardTheme: const CardThemeData(
        color: Color(0xFF161B22),
        elevation: 0,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.all(Radius.circular(8)),
          side: BorderSide(color: Color(0xFF30363D)),
        ),
      ),
      sliderTheme: const SliderThemeData(
        activeTrackColor: Color(0xFF58A6FF),
        thumbColor: Color(0xFF58A6FF),
        overlayColor: Color(0x2958A6FF),
      ),
      switchTheme: SwitchThemeData(
        thumbColor: WidgetStateProperty.resolveWith(
          (s) => s.contains(WidgetState.selected)
              ? const Color(0xFF58A6FF)
              : const Color(0xFF6E7681),
        ),
        trackColor: WidgetStateProperty.resolveWith(
          (s) => s.contains(WidgetState.selected)
              ? const Color(0x5858A6FF)
              : const Color(0xFF21262D),
        ),
      ),
      dividerColor: const Color(0xFF30363D),
      textTheme: const TextTheme(
        bodyMedium: TextStyle(color: Color(0xFFE6EDF3)),
        bodySmall: TextStyle(color: Color(0xFF8B949E)),
        labelMedium: TextStyle(color: Color(0xFF8B949E), fontSize: 11),
      ),
    );
  }

  static ThemeData get light => ThemeData(
        useMaterial3: true,
        colorScheme: const ColorScheme.light(
          primary: Color(0xFF0969DA),
          secondary: Color(0xFF1A7F37),
          surface: Color(0xFFFFFFFF),
          error: Color(0xFFCF222E),
        ),
      );
}
