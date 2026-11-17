// tourelle-cli: scripting companion of Tourelle.exe (which does the setup).
//
//   tourelle-cli enable             register the "Tourelle Camera" virtual camera (current user)
//   tourelle-cli remove             unregister it
//   tourelle-cli move <pan> <tilt>  relative move in degrees (pan +-70, tilt +-30)
//   tourelle-cli reset              recenter the head

#include <windows.h>
#include <mfapi.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "TourelleClient.h"

using Microsoft::WRL::ComPtr;

namespace {

int Usage() {
    fwprintf(stderr, L"usage: tourelle-cli enable | remove | move <pan-degrees> <tilt-degrees> | reset\n");
    return 2;
}

int16_t DegreesToUnits(const wchar_t* text) {
    const double units = _wtof(text) * 64.0;  // the motor counts in 1/64 degree
    return static_cast<int16_t>(units < -32768 ? -32768 : units > 32767 ? 32767 : units);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return Usage();
    const std::wstring command = argv[1];
    const bool motor = command == L"move" || command == L"reset";
    if (!(command == L"enable" || command == L"remove" || (command == L"move" && argc == 4) || command == L"reset")) {
        return Usage();
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    HRESULT hr;
    if (command == L"enable") {
        hr = tourelle::client::EnableVirtualCamera();
    } else if (command == L"remove") {
        hr = tourelle::client::RemoveVirtualCamera();
    } else {
        ComPtr<IMFMediaSource> source;
        hr = tourelle::client::FindSource(&source);
        if (SUCCEEDED(hr)) {
            hr = command == L"reset"
                     ? tourelle::client::PanTiltReset(source.Get())
                     : tourelle::client::PanTiltRelative(source.Get(), DegreesToUnits(argv[2]), DegreesToUnits(argv[3]));
            source->Shutdown();
        } else if (motor) {
            fwprintf(stderr, L"\"Tourelle Camera\" not found: install it with Tourelle.exe first.\n");
        }
    }

    if (FAILED(hr)) fwprintf(stderr, L"failed: 0x%08X\n", static_cast<unsigned>(hr));
    else wprintf(L"OK\n");
    MFShutdown();
    CoUninitialize();
    return FAILED(hr) ? 1 : 0;
}
