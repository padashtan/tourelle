# Changelog

All notable changes to Tourelle. Versions follow [Semantic Versioning](https://semver.org/).

## 0.3.0

### Added
- **Standard camera controls**: pan, tilt and focus (`PROPSETID_VIDCAP_CAMERACONTROL`), plus image
  settings such as brightness, contrast, saturation, sharpness, white balance, gain and power line
  frequency (`PROPSETID_VIDCAP_VIDEOPROCAMP`). Windows and video apps can drive the camera without
  Tourelle.exe.
- **Absolute positions**: the camera source tracks the head position. Four saved positions in the app
  (keys 1–4; right-click a position to save it).
- **Click to aim**: click a point of the preview to bring it to the center.
- **Focus**: near/far buttons and contrast autofocus (AF).
- **Face following** with the face detector built into Windows.
- **Global shortcuts** (Ctrl+Alt+arrows, Ctrl+Alt+Home, Ctrl+Alt+1–4, Ctrl+Alt+F) and Xbox controller
  support.
- 800×600 (25 fps) and 960×720 (10 fps) video modes, in addition to 640×480 (30 fps).
- Installer (`tourelle-setup-<version>.exe`).
- *Copy diagnostics* menu item for bug reports. `--demo` mode, which runs without the camera.
- `tourelle-cli goto`, `where`, `focus`, `probe` and `grab`.
- Unit tests for the pure logic: descriptor parsing (including the real descriptor and malformed
  inputs), frame assembly, pixel conversion, motor math, autofocus, KS property helpers and
  translations.

### Changed
- Motor commands run on a worker shared by the whole DLL, so a recenter-then-move completes even when
  the client closes right away.
- The motor unit is found by its GUID in the descriptor instead of a fixed unit id.

### Fixed
- The camera reopens by itself after it is unplugged and plugged back in.
- Demo mode no longer writes to the user's settings.

## 0.2.0

- Tourelle.exe: dark interface, live preview, notification area icon, start with Windows, inverted
  axes option, English, French, Spanish and Polish.
- The camera turns off (LED off) a few seconds after the last app stops using it.
- Fixed a hang when closing the app.

## 0.1.0

- First version: user-mode UVC driver over WinUSB, Windows 11 virtual camera, pan/tilt from the
  command line.
