# PicoMSO

RP2040 mixed-signal firmware with shared logic-capture, scope-capture, protocol,
transport, and integration layers under `firmware/`.

## Repository layout

```text
PicoMSO/
├── README.md
├── docs/
│   ├── architecture.md
│   ├── building.md
│   ├── protocol.md
│   └── repository-transition.md
├── external/
│   └── pico-sdk/
├── firmware/
│   ├── app/
│   ├── common/
│   ├── integration/
│   ├── mixed_signal/
│   ├── protocol/
│   └── transport/
├── src/
└── utils/
```

## Firmware structure

- `firmware/app/` - current Pico SDK application entry point
- `firmware/common/` - shared debug, types, and capture-controller state
- `firmware/mixed_signal/` - logic and scope capture backends
- `firmware/protocol/` - packet format and command dispatch
- `firmware/transport/` - transport abstraction plus USB backend
- `firmware/integration/` - receive/dispatch/send glue

## Build

Initialize submodules, then build the firmware application from the repository
root:

```bash
git submodule update --init --recursive
cmake -S firmware/app -B build/picomso
cmake --build build/picomso
```

See:

- [`docs/building.md`](docs/building.md)
- [`docs/architecture.md`](docs/architecture.md)
- [`docs/protocol.md`](docs/protocol.md)
