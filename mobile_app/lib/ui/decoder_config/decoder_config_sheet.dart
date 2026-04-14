import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:picomso/controllers/decoder_controller.dart';
import 'package:picomso/domain/decoders/decoder_interface.dart';
import 'package:picomso/domain/decoders/uart_decoder.dart';
import 'package:picomso/domain/decoders/spi_decoder.dart';
import 'package:picomso/domain/decoders/i2c_decoder.dart';

enum _DecoderType { uart, spi, i2c }

/// Bottom sheet for configuring a protocol decoder on a specific channel.
class DecoderConfigSheet extends ConsumerStatefulWidget {
  const DecoderConfigSheet({super.key, required this.channelIndex});

  final int channelIndex;

  @override
  ConsumerState<DecoderConfigSheet> createState() => _DecoderConfigSheetState();
}

class _DecoderConfigSheetState extends ConsumerState<DecoderConfigSheet> {
  _DecoderType _type = _DecoderType.uart;

  // UART
  int _baudRate = 115200;
  ByteFormat _uartFormat = ByteFormat.hex;
  bool _idleHigh = true;

  // SPI
  int _clkChannel = 1;
  int? _misoChannel;
  int _cpol = 0;
  int _cpha = 0;
  SpiByteFormat _spiFormat = SpiByteFormat.hex;

  // I2C
  int _sclChannel = 1;
  I2cByteFormat _i2cFormat = I2cByteFormat.hex;

  static const _baudRates = [
    1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600,
  ];

  @override
  void initState() {
    super.initState();
    // Pre-populate from existing config if any.
    final existing = ref.read(decoderConfigProvider).configs[widget.channelIndex];
    if (existing is UartDecoderConfig) {
      _type = _DecoderType.uart;
      _baudRate = existing.baudRate;
      _uartFormat = existing.format;
      _idleHigh = existing.idleHigh;
    } else if (existing is SpiDecoderConfig) {
      _type = _DecoderType.spi;
      _clkChannel = existing.clkChannelIndex;
      _cpol = existing.cpol;
      _cpha = existing.cpha;
      _spiFormat = existing.format;
    } else if (existing is I2cDecoderConfig) {
      _type = _DecoderType.i2c;
      _sclChannel = existing.sclChannelIndex;
      _i2cFormat = existing.format;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: EdgeInsets.only(
        bottom: MediaQuery.of(context).viewInsets.bottom,
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          _handle(),
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 4, 16, 16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Decoder: D${widget.channelIndex}',
                  style: const TextStyle(
                      fontWeight: FontWeight.w600, fontSize: 16),
                ),
                const SizedBox(height: 12),
                _TypeSelector(),
                const SizedBox(height: 12),
                _typeConfig(),
                const SizedBox(height: 20),
                Row(
                  children: [
                    Expanded(
                      child: OutlinedButton(
                        onPressed: _clear,
                        child: const Text('Clear'),
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: FilledButton(
                        onPressed: _apply,
                        child: const Text('Apply'),
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _handle() => Padding(
        padding: const EdgeInsets.symmetric(vertical: 8),
        child: Center(
          child: Container(
            width: 36,
            height: 4,
            decoration: BoxDecoration(
              color: const Color(0xFF30363D),
              borderRadius: BorderRadius.circular(2),
            ),
          ),
        ),
      );

  Widget _TypeSelector() => SegmentedButton<_DecoderType>(
        segments: const [
          ButtonSegment(value: _DecoderType.uart, label: Text('UART')),
          ButtonSegment(value: _DecoderType.spi, label: Text('SPI')),
          ButtonSegment(value: _DecoderType.i2c, label: Text('I2C')),
        ],
        selected: {_type},
        onSelectionChanged: (s) =>
            setState(() => _type = s.first),
      );

  Widget _typeConfig() {
    switch (_type) {
      case _DecoderType.uart:
        return _UartConfig();
      case _DecoderType.spi:
        return _SpiConfig();
      case _DecoderType.i2c:
        return _I2cConfig();
    }
  }

  Widget _UartConfig() => Column(
        children: [
          _Row(
            label: 'Baud rate',
            child: DropdownButton<int>(
              value: _baudRates.contains(_baudRate) ? _baudRate : 115200,
              isExpanded: true,
              items: _baudRates
                  .map((v) =>
                      DropdownMenuItem(value: v, child: Text('$v bps')))
                  .toList(),
              onChanged: (v) {
                if (v != null) setState(() => _baudRate = v);
              },
            ),
          ),
          _Row(
            label: 'Format',
            child: DropdownButton<ByteFormat>(
              value: _uartFormat,
              isExpanded: true,
              items: ByteFormat.values
                  .map((v) => DropdownMenuItem(
                      value: v, child: Text(v.name.toUpperCase())))
                  .toList(),
              onChanged: (v) {
                if (v != null) setState(() => _uartFormat = v);
              },
            ),
          ),
          SwitchListTile(
            title: const Text('Idle High (standard)'),
            value: _idleHigh,
            onChanged: (v) => setState(() => _idleHigh = v),
            dense: true,
          ),
        ],
      );

  Widget _SpiConfig() => Column(
        children: [
          _Row(
            label: 'CLK channel',
            child: DropdownButton<int>(
              value: _clkChannel,
              isExpanded: true,
              items: List.generate(
                  16,
                  (i) => DropdownMenuItem(value: i, child: Text('D$i'))),
              onChanged: (v) {
                if (v != null) setState(() => _clkChannel = v);
              },
            ),
          ),
          _Row(
            label: 'CPOL',
            child: DropdownButton<int>(
              value: _cpol,
              isExpanded: true,
              items: const [
                DropdownMenuItem(value: 0, child: Text('0 (idle low)')),
                DropdownMenuItem(value: 1, child: Text('1 (idle high)')),
              ],
              onChanged: (v) {
                if (v != null) setState(() => _cpol = v);
              },
            ),
          ),
          _Row(
            label: 'CPHA',
            child: DropdownButton<int>(
              value: _cpha,
              isExpanded: true,
              items: const [
                DropdownMenuItem(value: 0, child: Text('0 (leading)')),
                DropdownMenuItem(value: 1, child: Text('1 (trailing)')),
              ],
              onChanged: (v) {
                if (v != null) setState(() => _cpha = v);
              },
            ),
          ),
          _Row(
            label: 'Format',
            child: DropdownButton<SpiByteFormat>(
              value: _spiFormat,
              isExpanded: true,
              items: SpiByteFormat.values
                  .map((v) => DropdownMenuItem(
                      value: v, child: Text(v.name.toUpperCase())))
                  .toList(),
              onChanged: (v) {
                if (v != null) setState(() => _spiFormat = v);
              },
            ),
          ),
        ],
      );

  Widget _I2cConfig() => Column(
        children: [
          _Row(
            label: 'SCL channel',
            child: DropdownButton<int>(
              value: _sclChannel,
              isExpanded: true,
              items: List.generate(
                  16,
                  (i) => DropdownMenuItem(value: i, child: Text('D$i'))),
              onChanged: (v) {
                if (v != null) setState(() => _sclChannel = v);
              },
            ),
          ),
          _Row(
            label: 'Format',
            child: DropdownButton<I2cByteFormat>(
              value: _i2cFormat,
              isExpanded: true,
              items: I2cByteFormat.values
                  .map((v) => DropdownMenuItem(
                      value: v, child: Text(v.name.toUpperCase())))
                  .toList(),
              onChanged: (v) {
                if (v != null) setState(() => _i2cFormat = v);
              },
            ),
          ),
        ],
      );

  Widget _Row({required String label, required Widget child}) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 4),
        child: Row(
          children: [
            SizedBox(
              width: 100,
              child: Text(label,
                  style: const TextStyle(fontSize: 13, color: Color(0xFF8B949E))),
            ),
            Expanded(child: child),
          ],
        ),
      );

  void _apply() {
    final DecoderConfig config;
    switch (_type) {
      case _DecoderType.uart:
        config = UartDecoderConfig(
          channelIndex: widget.channelIndex,
          baudRate: _baudRate,
          format: _uartFormat,
          idleHigh: _idleHigh,
        );
      case _DecoderType.spi:
        config = SpiDecoderConfig(
          channelIndex: widget.channelIndex,
          clkChannelIndex: _clkChannel,
          cpol: _cpol,
          cpha: _cpha,
          format: _spiFormat,
        );
      case _DecoderType.i2c:
        config = I2cDecoderConfig(
          channelIndex: widget.channelIndex,
          sclChannelIndex: _sclChannel,
          format: _i2cFormat,
        );
    }
    ref.read(decoderConfigProvider.notifier).setConfig(widget.channelIndex, config);
    Navigator.of(context).pop();
  }

  void _clear() {
    ref.read(decoderConfigProvider.notifier).removeConfig(widget.channelIndex);
    Navigator.of(context).pop();
  }
}
