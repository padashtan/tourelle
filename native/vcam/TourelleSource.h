#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <ks.h>
#include <ksproxy.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <memory>
#include <mutex>

#include "TourelleStream.h"
#include "UvcDevice.h"

namespace tourelle {

// Media source behind the "Tourelle Camera" virtual camera. Runs inside the
// Windows Camera Frame Server, owns the WinUSB camera while streaming, and
// accepts pan/tilt commands through IKsControl (PROPSETID_TourelleMotor).
class TourelleSource
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Microsoft::WRL::ChainInterfaces<IMFMediaSourceEx, IMFMediaSource, IMFMediaEventGenerator>, IMFGetService,
          IKsControl, IMFSampleAllocatorControl> {
public:
    HRESULT RuntimeClassInitialize();
    ~TourelleSource() override;

    // IMFMediaEventGenerator
    IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
    IFACEMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status,
                              const PROPVARIANT* eventValue) override;

    // IMFMediaSource
    IFACEMETHODIMP GetCharacteristics(DWORD* characteristics) override;
    IFACEMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) override;
    IFACEMETHODIMP Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat,
                         const PROPVARIANT* startPosition) override;
    IFACEMETHODIMP Stop() override;
    IFACEMETHODIMP Pause() override;
    IFACEMETHODIMP Shutdown() override;

    // IMFMediaSourceEx
    IFACEMETHODIMP GetSourceAttributes(IMFAttributes** attributes) override;
    IFACEMETHODIMP GetStreamAttributes(DWORD streamId, IMFAttributes** attributes) override;
    IFACEMETHODIMP SetD3DManager(IUnknown* manager) override;

    // IMFGetService
    IFACEMETHODIMP GetService(REFGUID service, REFIID riid, LPVOID* object) override;

    // IKsControl
    IFACEMETHODIMP KsProperty(PKSPROPERTY property, ULONG propertyLength, LPVOID data, ULONG dataLength,
                              ULONG* bytesReturned) override;
    IFACEMETHODIMP KsMethod(PKSMETHOD method, ULONG methodLength, LPVOID data, ULONG dataLength,
                            ULONG* bytesReturned) override;
    IFACEMETHODIMP KsEvent(PKSEVENT event, ULONG eventLength, LPVOID data, ULONG dataLength,
                           ULONG* bytesReturned) override;

    // IMFSampleAllocatorControl
    IFACEMETHODIMP SetDefaultAllocator(DWORD outputStreamId, IUnknown* allocator) override;
    IFACEMETHODIMP GetAllocatorUsage(DWORD outputStreamId, DWORD* inputStreamId,
                                     MFSampleAllocatorUsage* usage) override;

private:
    HRESULT CreateSourceAttributes();
    HRESULT StartCamera();
    void StopCamera();
    HRESULT MotorCommand(ULONG id, const BYTE* data, ULONG length);

    std::recursive_mutex lock_;
    bool shutdown_ = false;
    bool started_ = false;   // MENewStream already sent once
    bool mfStarted_ = false;
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue_;
    Microsoft::WRL::ComPtr<IMFAttributes> attributes_;
    Microsoft::WRL::ComPtr<IMFPresentationDescriptor> descriptor_;
    Microsoft::WRL::ComPtr<TourelleStream> stream_;
    std::unique_ptr<UvcDevice> camera_;  // open only while streaming
};

}  // namespace tourelle
