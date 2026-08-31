import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/ui/home/home_screen.dart';
import 'package:picomso/ui/capture_run/capture_run_screen.dart';
import 'package:picomso/ui/waveform/waveform_viewer_screen.dart';
import 'package:picomso/ui/settings/settings_screen.dart';
import 'package:picomso/ui/shared/theme/app_theme.dart';

/// Root application widget.
///
/// Uses Flutter's built-in [Navigator] with named routes for simplicity.
/// Replace with go_router for deeper linking requirements.
class PicoMsoApp extends ConsumerWidget {
  const PicoMsoApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return MaterialApp(
      title: 'PicoMSO',
      debugShowCheckedModeBanner: false,
      theme: AppTheme.light,
      darkTheme: AppTheme.dark,
      themeMode: ThemeMode.dark,
      initialRoute: '/',
      routes: {
        '/': (_) => const HomeScreen(),
        '/capture-run': (_) => const CaptureRunScreen(),
        '/waveform': (_) => const WaveformViewerScreen(),
        '/settings': (_) => const SettingsScreen(),
      },
    );
  }
}
