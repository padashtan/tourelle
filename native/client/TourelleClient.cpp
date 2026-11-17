#include "TourelleClient.h"

#include <mfapi.h>
#include <mfvirtualcamera.h>
// After the Media Foundation headers: ksproxy.h needs the COM declarations they pull in.
#include <ks.h>
#include <ksproxy.h>
#include <wrl/client.h>

#include <cwchar>

#include "TourelleProperties.h"

using Microsoft::WRL::ComPtr;

namespace tourelle::client {
namespace {

HRESULT CreateVirtualCamera(IMFVirtualCamera** camera) {
    // The same parameters always designate the same camera.
    return MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_System,
                                 MFVirtualCameraAccess_CurrentUser, kTourelleCameraName, kTourelleCameraClsid, nullptr,
                                 0, camera);
}

// Calls visit(activate) for each video capture device until it returns true.
template <typename Visit>
HRESULT ForEachCamera(Visit visit) {
    ComPtr<IMFAttributes> filter;
    HRESULT hr = MFCreateAttributes(&filter, 1);
    if (FAILED(hr)) return hr;
    filter->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (FAILED(hr = MFEnumDeviceSources(filter.Get(), &devices, &count))) return hr;
    bool done = false;
    for (UINT32 i = 0; i < count; ++i) {
        if (!done) done = visit(devices[i]);
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    return S_OK;
}

bool IsTourelle(IMFActivate* device) {
    wchar_t* name = nullptr;
    UINT32 length = 0;
    if (FAILED(device->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length))) return false;
    const bool match = wcsstr(name, kTourelleCameraName) != nullptr;
    CoTaskMemFree(name);
    return match;
}

HRESULT SendMotor(IUnknown* source, ULONG id, void* data, ULONG length) {
    if (!source) return E_POINTER;
    ComPtr<IKsControl> control;
    HRESULT hr = source->QueryInterface(IID_PPV_ARGS(&control));
    if (FAILED(hr)) return hr;
    KSPROPERTY property{};
    property.Set = PROPSETID_TourelleMotor;
    property.Id = id;
    property.Flags = KSPROPERTY_TYPE_SET;
    ULONG returned = 0;
    return control->KsProperty(&property, sizeof(property), data, length, &returned);
}

}  // namespace

HRESULT EnableVirtualCamera() {
    ComPtr<IMFVirtualCamera> camera;
    HRESULT hr = CreateVirtualCamera(&camera);
    if (SUCCEEDED(hr)) hr = camera->Start(nullptr);
    if (camera) camera->Shutdown();
    return hr;
}

HRESULT RemoveVirtualCamera() {
    ComPtr<IMFVirtualCamera> camera;
    HRESULT hr = CreateVirtualCamera(&camera);
    if (SUCCEEDED(hr)) hr = camera->Remove();
    if (camera) camera->Shutdown();
    return hr;
}

bool IsVirtualCameraListed() {
    bool found = false;
    ForEachCamera([&](IMFActivate* device) { return found = IsTourelle(device); });
    return found;
}

HRESULT FindSource(IMFMediaSource** source) {
    HRESULT result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    HRESULT hr = ForEachCamera([&](IMFActivate* device) {
        if (!IsTourelle(device)) return false;
        result = device->ActivateObject(IID_PPV_ARGS(source));
        return true;
    });
    return FAILED(hr) ? hr : result;
}

HRESULT PanTiltRelative(IUnknown* source, int16_t pan, int16_t tilt) {
    int16_t values[2] = {pan, tilt};
    return SendMotor(source, TourelleMotor_PanTiltRelative, values, sizeof(values));
}

HRESULT PanTiltReset(IUnknown* source) {
    BYTE mask = 3;  // pan and tilt
    return SendMotor(source, TourelleMotor_PanTiltReset, &mask, sizeof(mask));
}

}  // namespace tourelle::client
