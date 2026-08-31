# PicoMSO Mobile App

Flutter application for the PicoMSO mixed-signal instrument.

## Architecture

See [docs/architecture.md](../docs/architecture.md) for the full design document.

### Layers

| Layer | Path | Responsibility |
|---|---|---|
| Transport | `lib/transport/` | Raw USB byte I/O |
| Protocol | `lib/protocol/` | Wire-format encode/decode |
| Repository | `lib/repository/` | Command/response orchestration |
| Controllers | `lib/controllers/` | Riverpod state providers |
| UI | `lib/ui/` | Screens, painters, widgets |

## Getting Started

```sh
flutter pub get
flutter pub run build_runner build --delete-conflicting-outputs
flutter run
```

## USB Transport

`UsbTransport` is a stub that requires a platform-specific USB implementation
(e.g. a native Android plugin that provides EP0 vendor control transfers and
EP6 bulk IN reads). See `lib/transport/usb_transport.dart`.
