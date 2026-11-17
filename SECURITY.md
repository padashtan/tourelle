# Security policy

Tourelle installs a component that runs inside a Windows service (the Camera Frame Server) and
changes the driver of one USB device. Security reports are taken seriously.

## Reporting a vulnerability

Please **do not open a public issue**. Use GitHub's
[private vulnerability reporting](https://github.com/padashtan/tourelle/security/advisories/new)
instead. Include the version (☰ menu, *About*), the steps to reproduce, and the impact you
see.

You can expect an acknowledgement within a week.

## Scope

In scope:

- `TourelleCamera.dll`, the media source loaded by the Frame Server. Any process that can open the
  camera can send requests to it.
- The elevated setup (`Tourelle.exe --install` / `--uninstall`) and the installer.
- The parsing of data that comes from the USB device: descriptors and video payloads.

Out of scope: issues that need administrator rights on the machine first, and the behavior of the
camera's firmware.

## Supported versions

Only the latest release receives fixes.
