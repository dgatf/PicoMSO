# Manual libsigrok setup

This document describes the manual host-side setup for PicoMSO using the
temporary libsigrok fork and command-line tools.

Most users should use the prebuilt **PicoMSO Desktop** binaries instead:

https://github.com/dgatf/pulseview/releases

---

## Overview

PicoMSO support is currently provided through a temporary libsigrok fork until
upstream integration is merged.

Temporary fork:

https://github.com/dgatf/libsigrok/tree/picomso-driver

This manual setup is intended for:

- Development and debugging
- Custom host-side integration
- Command-line usage with `sigrok-cli`
- Testing outside the prebuilt desktop application

---

## Precompiled libsigrok binaries

Precompiled host-side binaries are available here:

https://github.com/dgatf/libsigrok/releases

These packages are intended for advanced users who want to install the PicoMSO
driver manually.

---

## Linux

Extract the archive and copy the files into `/usr/local`:

```bash
tar -xf libsigrok-ubuntu-22.04.tar.gz
cd libsigrok-ubuntu-22.04
sudo cp -a usr/local/* /usr/local/
sudo ldconfig
```

Verify the installation:

```bash
pkg-config --modversion libsigrok
```

You can also check that PicoMSO is visible to sigrok:

```bash
sigrok-cli --scan
```

---

## Windows (MSYS2 UCRT64)

Extract the package and copy the files into the MSYS2 UCRT64 environment:

```bash
unzip libsigrok-windows-ucrt64.zip
cd libsigrok-windows-ucrt64
cp -a ucrt64/* /ucrt64/
```

Then verify from an MSYS2 UCRT64 shell:

```bash
sigrok-cli --scan
```

---

## macOS

Extract the archive and copy the files into `/usr/local`:

```bash
tar -xf libsigrok-macos-14.tar.gz
cd libsigrok-macos-14
sudo cp -a usr/local/* /usr/local/
```

Verify the installation:

```bash
sigrok-cli --scan
```

---

## Building libsigrok manually

If you prefer to build the fork manually, clone the PicoMSO branch:

```bash
git clone --branch picomso-driver https://github.com/dgatf/libsigrok.git
cd libsigrok
```

Then build it using the normal libsigrok build procedure for your platform.

This path is recommended only for development or when modifying the driver.

---

## Using PicoMSO with sigrok-cli

Show the PicoMSO device information:

```bash
sigrok-cli -d picomso --show
```

Run a simple logic capture:

```bash
sigrok-cli -d picomso --channels D0 --samples 1000 --config samplerate=5k
```

Run a mixed-signal capture:

```bash
sigrok-cli -d picomso --channels D0,A0 --samples 1000 --config samplerate=5k
```

Scan connected sigrok devices:

```bash
sigrok-cli --scan
```

---

## Notes

- PicoMSO Desktop is the recommended installation path for normal users.
- Manual libsigrok installation is mainly intended for development and testing.
- Until upstream support is merged, PicoMSO requires the temporary
  `picomso-driver` libsigrok fork.
- Requests beyond PicoMSO hardware limits are rejected by the driver.

---

## Related documentation

- [../README.md](../README.md)
- [building.md](building.md)
- [architecture.md](architecture.md)
- [protocol.md](protocol.md)