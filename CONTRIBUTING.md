# Contributing to Tourelle

Thanks for your interest! Bug reports, camera reports, translations and code are all welcome.

## Reporting a bug

Open an issue with the *Bug report* template. In Tourelle.exe, open the ☰ menu and click
**Copy diagnostics**, then paste the result into the issue. It lists the versions, the drivers and
the setup state, and contains no personal data.

## Another camera?

If you have a motorized Logitech camera other than the QuickCam Sphere AF, run
`tools\dumpdesc.ps1` and attach its output to a *Camera report* issue. The output shows whether the
camera has the same hidden motor unit.

## Building

Requirements: Visual Studio 2022 or Build Tools, with the C++ workload and the Windows 11 SDK.

```bat
native\build.cmd                         :: Release build into native\build
native\build\tourelle-tests.exe          :: unit tests (no camera needed)
native\build\Tourelle.exe --demo         :: the app with a synthetic image (no camera needed)
```

Hardware checks with the camera connected:

```bat
native\build\uvcgrab.exe                 :: stream + move test, without the virtual camera
native\build\tourelle-cli.exe probe      :: standard controls through the virtual camera
native\build\tourelle-cli.exe grab 800 600
```

The installer is built with Inno Setup 6:
`iscc /DAppVersion=0.3.0 /DBinDir=..\native\build installer\Tourelle.iss`.

## Code layout

| Folder | What lives there |
|---|---|
| `native/core` | Pure logic: no Windows I/O, fully unit tested |
| `native/uvc` | User-mode UVC driver over WinUSB |
| `native/vcam` | Media Foundation source loaded by the Windows Camera Frame Server |
| `native/client` | Client side of the virtual camera (registration, motor commands) |
| `native/app` | Tourelle.exe: user interface and setup |
| `native/tests` | Unit tests (`tourelle-tests.exe`, run by CTest and the CI) |

Rules of thumb:

- Logic that can be tested without the camera belongs in `core`, with a test.
- The camera source runs inside a Windows service and receives requests from any process: validate
  every size and pointer it gets.
- No third-party code. The project uses Windows components only.
- Warnings are errors (`/W4 /WX`).

## Translations

All texts live in `native/app/Strings.h`. To add a language:

1. Add a `Texts` instance with every field.
2. Add it to `kLanguages`.
3. Map its Windows language in `DefaultLanguage()`.

`tourelle-tests` checks that no field is missing. Native speakers are welcome to review the Spanish
and Polish texts.

## Pull requests

- Keep each pull request focused on one change.
- Make sure `tourelle-tests` passes, and that the CI is green.
- Describe how you tested on hardware, if you could.
