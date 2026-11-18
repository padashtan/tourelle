# Changelog

All notable changes to Tourelle. Versions follow [Semantic Versioning](https://semver.org/).

## 1.0.0 - 2026-11-18

### Added
- Preview resolution in the menu: 640 × 480 (30 fps), 800 × 600 (25 fps) or 960 × 720 (10 fps), remembered.
- Share the camera with Teams, Camera and other apps: *Video in Tourelle* (menu, tray menu or the V key) releases the video and keeps the controls. When another app already uses the camera, Tourelle leaves it alone and keeps only the controls.
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

### Security
These fixes follow an independent security review and an adversarial code review done before the
first signed release.
- The camera source DLL is embedded in Tourelle.exe. The elevated setup writes it from there, never
  from a file next to the executable, which closes DLL planting and swap attacks.
- Tourelle.exe and tourelle-cli.exe load their DLLs from System32 only (`/DEPENDENTLOADFLAG`,
  `SetDefaultDllDirectories`).
- Only SYSTEM, administrators and the Frame Server's account (LocalService) can open the
  WinUSB-bound camera. Apps go through the virtual camera and the Windows camera privacy settings.
  The camera is also checked by its USB id before use.
- Video payload headers are validated against the UVC rules (PTS/SCR sizes, 12-byte maximum). An
  error no longer spills onto the next frame. Packet bounds are checked without integer wrap-around.
- The media source keeps itself alive across the wake callback (use-after-free race). Short or
  inconsistent replies from the device are handled.
- The motor queue cannot be flooded: recenters and absolute moves supersede pending moves.
- Uninstall only deletes files inside `Program Files\Tourelle` and only removes WinUSB bindings that
  Tourelle made. A failed driver switch always gives the camera back to Windows.
- Control Flow Guard and CET compatibility on every binary. The CI actions are pinned to commit SHAs
  and Inno Setup is verified by SHA-256. Signing runs only for tags on `main`, behind a protected
  environment.

## 0.2.0

- Tourelle.exe: dark interface, live preview, notification area icon, start with Windows, inverted
  axes option, English, French, Spanish and Polish.
- The camera turns off (LED off) a few seconds after the last app stops using it.
- Fixed a hang when closing the app.

## 0.1.0

- First version: user-mode UVC driver over WinUSB, Windows 11 virtual camera, pan/tilt from the
  command line.
