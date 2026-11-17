// tourelle: registers the "Tourelle Camera" virtual camera and drives the head
// through it while any application is using the camera.
//
//   tourelle install           register and enable the virtual camera (current user)
//   tourelle remove            unregister it
//   tourelle move <pan> <tilt> relative move in 1/64 degree (pan +-4480, tilt +-1920)
//   tourelle reset             recenter the head

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
// After the Media Foundation headers: ksproxy.h needs the COM declarations they pull in.
#include <ks.h>
#include <ksproxy.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>

#include "TourelleProperties.h"

using Microsoft::WRL::ComPtr;

namespace {

HRESULT CreateVirtualCamera(IMFVirtualCamera** camera) {
    // Same parameters always designate the same camera.
    return MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_System,
                                 MFVirtualCameraAccess_CurrentUser, kTourelleCameraName, kTourelleCameraClsid,
                                 nullptr, 0, camera);
}

HRESULT FindSource(IMFMediaSource** source) {
    ComPtr<IMFAttributes> filter;
    HRESULT hr = MFCreateAttributes(&filter, 1);
    if (FAILED(hr)) return hr;
    filter->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (FAILED(hr = MFEnumDeviceSources(filter.Get(), &devices, &count))) return hr;
    hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    for (UINT32 i = 0; i < count; ++i) {
        wchar_t* name = nullptr;
        UINT32 length = 0;
        if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) &&
            SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length))) {
            if (wcsstr(name, kTourelleCameraName)) hr = devices[i]->ActivateObject(IID_PPV_ARGS(source));
            CoTaskMemFree(name);
        }
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    return hr;
}

HRESULT SendMotor(ULONG id, void* data, ULONG length) {
    ComPtr<IMFMediaSource> source;
    HRESULT hr = FindSource(&source);
    if (FAILED(hr)) {
        fwprintf(stderr, L"virtual camera '%s' not found (run 'tourelle install')\n", kTourelleCameraName);
        return hr;
    }
    ComPtr<IKsControl> control;
    if (SUCCEEDED(hr = source.As(&control))) {
        KSPROPERTY property{};
        property.Set = PROPSETID_TourelleMotor;
        property.Id = id;
        property.Flags = KSPROPERTY_TYPE_SET;
        ULONG returned = 0;
        hr = control->KsProperty(&property, sizeof(property), data, length, &returned);
    }
    source->Shutdown();
    return hr;
}

int Usage() {
    fwprintf(stderr, L"usage: tourelle install | remove | move <pan> <tilt> | reset\n");
    return 2;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return Usage();
    const std::wstring command = argv[1];

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    HRESULT hr = E_INVALIDARG;

    if (command == L"install" || command == L"remove") {
        ComPtr<IMFVirtualCamera> camera;
        hr = CreateVirtualCamera(&camera);
        if (SUCCEEDED(hr)) hr = command == L"install" ? camera->Start(nullptr) : camera->Remove();
        if (SUCCEEDED(hr) && command == L"install") {
            wchar_t* link = nullptr;
            UINT32 length = 0;
            if (SUCCEEDED(camera->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link,
                                                     &length))) {
                wprintf(L"virtual camera enabled: %s\n", link);
                CoTaskMemFree(link);
            }
        }
        if (camera) camera->Shutdown();
    } else if (command == L"move" && argc == 4) {
        int16_t values[2] = {static_cast<int16_t>(_wtoi(argv[2])), static_cast<int16_t>(_wtoi(argv[3]))};
        hr = SendMotor(TourelleMotor_PanTiltRelative, values, sizeof(values));
    } else if (command == L"reset") {
        BYTE mask = 3;
        hr = SendMotor(TourelleMotor_PanTiltReset, &mask, sizeof(mask));
    } else {
        return Usage();
    }

    if (FAILED(hr)) fwprintf(stderr, L"failed: 0x%08X\n", static_cast<unsigned>(hr));
    else wprintf(L"OK\n");
    MFShutdown();
    CoUninitialize();
    return FAILED(hr) ? 1 : 0;
}
