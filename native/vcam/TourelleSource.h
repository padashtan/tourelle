#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <ks.h>
#include <ksproxy.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "KsControls.h"
#include "Motor.h"
#include "MotorService.h"
#include "TourelleStream.h"
#include "UvcDevice.h"

namespace tourelle {

// Media source behind the "Tourelle Camera" virtual camera. Runs inside the
// Windows Camera Frame Server, owns the WinUSB camera while streaming, and
// accepts commands through IKsControl:
//  - PROPSETID_TourelleMotor (relative/absolute pan-tilt, recenter, focus);
//  - PROPSETID_VIDCAP_CAMERACONTROL pan, tilt, focus (standard, used by Windows and apps);
//  - PROPSETID_VIDCAP_VIDEOPROCAMP image settings, passed to the camera's processing unit.
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
    HRESULT StartCamera(const TourelleStream::VideoMode& mode);
    void StopCamera();

    HRESULT MotorProperty(ULONG id, ULONG flags, void* data, ULONG length, ULONG* returned);
    HRESULT CameraControlProperty(ULONG id, ULONG flags, void* data, ULONG length, ULONG* returned);
    HRESULT ProcAmpProperty(ULONG id, ULONG flags, void* data, ULONG length, ULONG* returned);

    // The Frame Server may keep the source started with no client left. The
    // camera is closed after a few idle seconds (its LED goes off) and reopened
    // by the next sample request.
    static void CALLBACK OnIdleTimer(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER);
    void StopIfIdle();
    void Wake();
    void CloseIdleTimer();

    std::recursive_mutex lock_;
    bool shutdown_ = false;
    bool started_ = false;    // MENewStream already sent once
    bool streaming_ = false;  // between Start and Stop
    bool mfStarted_ = false;
    ULONGLONG lastStartAttempt_ = 0;
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue_;
    Microsoft::WRL::ComPtr<IMFAttributes> attributes_;
    Microsoft::WRL::ComPtr<IMFPresentationDescriptor> descriptor_;
    Microsoft::WRL::ComPtr<TourelleStream> stream_;
    std::unique_ptr<UvcDevice> camera_;  // open only while streaming
    PTP_TIMER idleTimer_ = nullptr;
    std::map<ULONG, core::LongRange> procAmpRanges_;  // cached GET_MIN/MAX/RES/DEF, used under the CameraHub lock
};

}  // namespace tourelle
