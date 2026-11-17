// tourelle-cli: scripting companion of Tourelle.exe (which does the setup).
//
//   tourelle-cli enable                register the "Tourelle Camera" virtual camera (current user)
//   tourelle-cli remove                unregister it
//   tourelle-cli move <pan> <tilt>     relative move in degrees (pan +-70, tilt +-30)
//   tourelle-cli goto <pan> <tilt>     absolute position in degrees (recenters first if needed)
//   tourelle-cli reset                 recenter the head
//   tourelle-cli where                 print the position tracked by the camera source
//   tourelle-cli focus [0-255]         set (or print) the focus motor, 0 = far

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#pragma comment(lib, "mfreadwrite.lib")

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "TourelleClient.h"
// After the Media Foundation headers: ksproxy.h needs the COM declarations they pull in.
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>

using Microsoft::WRL::ComPtr;

namespace {

// Queries the standard controls the source exposes to Windows and apps.
HRESULT Probe(IUnknown* source) {
    ComPtr<IKsControl> control;
    HRESULT hr = source->QueryInterface(IID_PPV_ARGS(&control));
    if (FAILED(hr)) return hr;
    struct Item {
        const GUID* set;
        ULONG id;
        const wchar_t* name;
    } items[] = {
        {&PROPSETID_VIDCAP_CAMERACONTROL, KSPROPERTY_CAMERACONTROL_PAN, L"pan (degrees)"},
        {&PROPSETID_VIDCAP_CAMERACONTROL, KSPROPERTY_CAMERACONTROL_TILT, L"tilt (degrees)"},
        {&PROPSETID_VIDCAP_CAMERACONTROL, KSPROPERTY_CAMERACONTROL_FOCUS, L"focus"},
        {&PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_BRIGHTNESS, L"brightness"},
        {&PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_CONTRAST, L"contrast"},
        {&PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_SATURATION, L"saturation"},
        {&PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_SHARPNESS, L"sharpness"},
        {&PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_WHITEBALANCE, L"white balance"},
        {&PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_GAIN, L"gain"},
        {&PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_POWERLINE_FREQUENCY, L"power line"},
        {&PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_HUE, L"hue (absent on this camera)"},
    };
    for (const auto& item : items) {
        KSPROPERTY_CAMERACONTROL_S request{};
        request.Property.Set = *item.set;
        request.Property.Id = item.id;
        request.Property.Flags = KSPROPERTY_TYPE_BASICSUPPORT;
        struct {
            KSPROPERTY_DESCRIPTION description;
            KSPROPERTY_MEMBERSHEADER header;
            KSPROPERTY_STEPPING_LONG range;
            KSPROPERTY_MEMBERSHEADER defaultHeader;
            LONG defaultValue;
        } support{};
        ULONG returned = 0;
        HRESULT basic = control->KsProperty(&request.Property, sizeof(request), &support, sizeof(support), &returned);
        request.Property.Flags = KSPROPERTY_TYPE_GET;
        KSPROPERTY_CAMERACONTROL_S value{};
        HRESULT get = control->KsProperty(&request.Property, sizeof(request), &value, sizeof(value), &returned);
        if (FAILED(basic)) {
            wprintf(L"%-30s not supported (0x%08X)\n", item.name, static_cast<unsigned>(basic));
        } else {
            wprintf(L"%-30s range %ld..%ld step %lu default %ld, current %ld%s\n", item.name,
                    support.range.Bounds.SignedMinimum, support.range.Bounds.SignedMaximum,
                    support.range.SteppingDelta, support.defaultValue, SUCCEEDED(get) ? value.Value : -9999,
                    value.Flags & 1 ? L" (auto)" : L"");
        }
    }
    return S_OK;
}

int Usage() {
    fwprintf(stderr,
             L"usage: tourelle-cli enable | remove | reset | where\n"
             L"       tourelle-cli move <pan-degrees> <tilt-degrees>\n"
             L"       tourelle-cli goto <pan-degrees> <tilt-degrees>\n"
             L"       tourelle-cli focus [0-255]\n");
    return 2;
}

// Streams the given mode for a few seconds and reports the frame rate.
HRESULT Grab(IMFMediaSource* source, UINT32 width, UINT32 height) {
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromMediaSource(source, nullptr, &reader);
    const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    ComPtr<IMFMediaType> chosen;
    for (DWORD i = 0; SUCCEEDED(hr); ++i) {
        ComPtr<IMFMediaType> type;
        if (FAILED(reader->GetNativeMediaType(stream, i, &type))) break;
        UINT32 w = 0, h = 0;
        GUID subtype{};
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
        type->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (w == width && h == height && subtype == MFVideoFormat_NV12) {
            chosen = type;
            break;
        }
    }
    if (SUCCEEDED(hr) && !chosen) hr = MF_E_INVALIDMEDIATYPE;
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(stream, nullptr, chosen.Get());
    int frames = 0;
    const ULONGLONG start = GetTickCount64();
    while (SUCCEEDED(hr) && GetTickCount64() - start < 4000) {
        DWORD index = 0, flags = 0;
        LONGLONG time = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(stream, 0, &index, &flags, &time, &sample);
        if (sample) ++frames;
    }
    if (SUCCEEDED(hr)) wprintf(L"%ux%u: %d frames in 4 s (%.1f fps)\n", width, height, frames, frames / 4.0);
    return hr;
}

int16_t DegreesToUnits(const wchar_t* text) {
    const double units = _wtof(text) * 64.0;  // the motor counts in 1/64 degree
    return static_cast<int16_t>(units < -32768 ? -32768 : units > 32767 ? 32767 : units);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    // Media Foundation is delay-loaded: it and its dependencies load from System32 only.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (argc < 2) return Usage();
    const std::wstring command = argv[1];
    const bool twoArgs = argc == 4;
    if (!(command == L"enable" || command == L"remove" || command == L"reset" || command == L"where" ||
          command == L"probe" ||
          ((command == L"move" || command == L"goto" || command == L"grab") && twoArgs) ||
          (command == L"focus" && argc <= 3))) {
        return Usage();
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    HRESULT hr;
    if (command == L"enable") {
        hr = tourelle::client::EnableVirtualCamera();
    } else if (command == L"remove") {
        hr = tourelle::client::RemoveVirtualCamera();
    } else {
        ComPtr<IMFMediaSource> source;
        hr = tourelle::client::FindSource(&source);
        if (FAILED(hr)) {
            fwprintf(stderr, L"\"Tourelle Camera\" not found: install it with Tourelle.exe first.\n");
        } else {
            if (command == L"probe") {
                hr = Probe(source.Get());
            } else if (command == L"grab") {
                // ReadSample blocks if no frame ever comes: give up after a while.
                const UINT32 width = static_cast<UINT32>(_wtoi(argv[2])), height = static_cast<UINT32>(_wtoi(argv[3]));
                HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!done) return 1;
                std::thread([&, done] {
                    if (FAILED(hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
                        SetEvent(done);
                        return;
                    }
                    hr = Grab(source.Get(), width, height);
                    CoUninitialize();
                    SetEvent(done);
                }).detach();
                if (WaitForSingleObject(done, 10000) != WAIT_OBJECT_0) {
                    fwprintf(stderr, L"%ux%u: no frame within 10 s\n", width, height);
                    ExitProcess(1);
                }
            } else if (command == L"reset") {
                hr = tourelle::client::PanTiltReset(source.Get());
            } else if (command == L"move") {
                hr = tourelle::client::PanTiltRelative(source.Get(), DegreesToUnits(argv[2]), DegreesToUnits(argv[3]));
            } else if (command == L"goto") {
                hr = tourelle::client::PanTiltAbsolute(source.Get(), DegreesToUnits(argv[2]), DegreesToUnits(argv[3]));
            } else if (command == L"where") {
                int16_t pan = 0, tilt = 0;
                bool known = false;
                hr = tourelle::client::GetPosition(source.Get(), &pan, &tilt, &known);
                if (SUCCEEDED(hr)) {
                    wprintf(known ? L"pan %.1f, tilt %.1f degrees\n" : L"unknown (recenter once)\n", pan / 64.0,
                            tilt / 64.0);
                }
            } else if (argc == 3) {
                hr = tourelle::client::SetFocus(source.Get(), static_cast<uint8_t>(_wtoi(argv[2]) & 0xFF));
            } else {
                uint8_t focus = 0;
                hr = tourelle::client::GetFocus(source.Get(), &focus);
                if (SUCCEEDED(hr)) wprintf(L"focus %u\n", focus);
            }
            source->Shutdown();
        }
    }

    if (FAILED(hr)) fwprintf(stderr, L"failed: 0x%08X\n", static_cast<unsigned>(hr));
    else if (command != L"where" && command != L"probe" && command != L"grab" && !(command == L"focus" && argc == 2)) {
        wprintf(L"OK\n");
    }
    MFShutdown();
    CoUninitialize();
    return FAILED(hr) ? 1 : 0;
}
