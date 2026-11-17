#include "TourelleActivate.h"

#include <mfvirtualcamera.h>

#include "TourelleSource.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;

namespace tourelle {

HRESULT TourelleActivate::RuntimeClassInitialize() {
    HRESULT hr = MFCreateAttributes(&a_, 1);
    if (FAILED(hr)) return hr;
    // We produce frames ourselves: no physical camera to associate.
    return a_->SetUINT32(MF_VIRTUALCAMERA_PROVIDE_ASSOCIATED_CAMERA_SOURCES, 1);
}

IFACEMETHODIMP TourelleActivate::ActivateObject(REFIID riid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    std::lock_guard guard(lock_);
    if (!source_) {
        ComPtr<TourelleSource> source;
        HRESULT hr = MakeAndInitialize<TourelleSource>(&source);
        if (FAILED(hr)) return hr;
        source.As(&source_);
    }
    return source_->QueryInterface(riid, object);
}

IFACEMETHODIMP TourelleActivate::ShutdownObject() {
    std::lock_guard guard(lock_);
    if (source_) {
        source_->Shutdown();
        source_.Reset();
    }
    return S_OK;
}

IFACEMETHODIMP TourelleActivate::DetachObject() {
    std::lock_guard guard(lock_);
    source_.Reset();
    return S_OK;
}

}  // namespace tourelle
