#include "TourelleSource.h"

#include <ksmedia.h>
#include <mferror.h>

#include <system_error>

#include "TourelleProperties.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::MakeAndInitialize;

namespace tourelle {
namespace {

constexpr DWORD kStreamId = 0;
constexpr ULONGLONG kIdleStopMs = 3000;  // close the camera after this long without a sample request
constexpr uint32_t kFrameInterval = 10'000'000 / TourelleStream::kFps;  // 100 ns units

HRESULT HResultFromException() {
    try {
        throw;
    } catch (const std::system_error& e) {
        return HRESULT_FROM_WIN32(static_cast<DWORD>(e.code().value()));
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

}  // namespace

HRESULT TourelleSource::RuntimeClassInitialize() {
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) return hr;
    mfStarted_ = true;

    if (FAILED(hr = MFCreateEventQueue(&queue_))) return hr;
    if (FAILED(hr = CreateSourceAttributes())) return hr;
    if (FAILED(hr = MakeAndInitialize<TourelleStream>(&stream_, static_cast<IMFMediaSource*>(this), kStreamId))) {
        return hr;
    }

    ComPtr<IMFStreamDescriptor> streamDescriptor;
    if (FAILED(hr = stream_->GetStreamDescriptor(&streamDescriptor))) return hr;
    IMFStreamDescriptor* descriptors[] = {streamDescriptor.Get()};
    if (FAILED(hr = MFCreatePresentationDescriptor(1, descriptors, &descriptor_))) return hr;
    if (FAILED(hr = descriptor_->SelectStream(0))) return hr;

    stream_->SetWakeHandler([this] { Wake(); });  // the stream never outlives our Shutdown
    idleTimer_ = CreateThreadpoolTimer(&TourelleSource::OnIdleTimer, this, nullptr);
    if (!idleTimer_) return HRESULT_FROM_WIN32(GetLastError());
    FILETIME due{};
    ULARGE_INTEGER relative;
    relative.QuadPart = static_cast<ULONGLONG>(-10'000'000LL);  // first check in 1 s
    due.dwLowDateTime = relative.LowPart;
    due.dwHighDateTime = relative.HighPart;
    SetThreadpoolTimer(idleTimer_, &due, 1000, 200);
    return S_OK;
}

TourelleSource::~TourelleSource() {
    if (!shutdown_) Shutdown();
    CloseIdleTimer();
    if (mfStarted_) MFShutdown();
}

void CALLBACK TourelleSource::OnIdleTimer(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER) {
    static_cast<TourelleSource*>(context)->StopIfIdle();
}

void TourelleSource::StopIfIdle() {
    std::lock_guard guard(lock_);
    if (!shutdown_ && camera_ && stream_->IdleMilliseconds() > kIdleStopMs) StopCamera();
}

void TourelleSource::Wake() {
    std::lock_guard guard(lock_);
    if (!shutdown_ && streaming_ && !camera_) StartCamera();
}

void TourelleSource::CloseIdleTimer() {
    if (!idleTimer_) return;
    SetThreadpoolTimer(idleTimer_, nullptr, 0, 0);
    WaitForThreadpoolTimerCallbacks(idleTimer_, TRUE);  // never under lock_: the callback takes it
    CloseThreadpoolTimer(idleTimer_);
    idleTimer_ = nullptr;
}

HRESULT TourelleSource::CreateSourceAttributes() {
    HRESULT hr = MFCreateAttributes(&attributes_, 4);
    if (FAILED(hr)) return hr;

    // The legacy profile is mandatory for Frame Server sources.
    ComPtr<IMFSensorProfileCollection> profiles;
    ComPtr<IMFSensorProfile> legacy;
    if (FAILED(hr = MFCreateSensorProfileCollection(&profiles))) return hr;
    if (FAILED(hr = MFCreateSensorProfile(KSCAMERAPROFILE_Legacy, 0, nullptr, &legacy))) return hr;
    if (FAILED(hr = legacy->AddProfileFilter(kStreamId, L"((RES==;FRT<=30,1;SUT==))"))) return hr;
    if (FAILED(hr = profiles->AddProfile(legacy.Get()))) return hr;
    return attributes_->SetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION, profiles.Get());
}

// IMFMediaEventGenerator ------------------------------------------------------

IFACEMETHODIMP TourelleSource::GetEvent(DWORD flags, IMFMediaEvent** event) {
    ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard guard(lock_);
        if (shutdown_) return MF_E_SHUTDOWN;
        queue = queue_;
    }
    return queue->GetEvent(flags, event);  // may block: do not hold the lock
}

IFACEMETHODIMP TourelleSource::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
    std::lock_guard guard(lock_);
    return shutdown_ ? MF_E_SHUTDOWN : queue_->BeginGetEvent(callback, state);
}

IFACEMETHODIMP TourelleSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
    std::lock_guard guard(lock_);
    return shutdown_ ? MF_E_SHUTDOWN : queue_->EndGetEvent(result, event);
}

IFACEMETHODIMP TourelleSource::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status,
                                       const PROPVARIANT* eventValue) {
    std::lock_guard guard(lock_);
    return shutdown_ ? MF_E_SHUTDOWN : queue_->QueueEventParamVar(type, extendedType, status, eventValue);
}

// IMFMediaSource --------------------------------------------------------------

IFACEMETHODIMP TourelleSource::GetCharacteristics(DWORD* characteristics) {
    if (!characteristics) return E_POINTER;
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    *characteristics = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

IFACEMETHODIMP TourelleSource::CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) {
    if (!descriptor) return E_POINTER;
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    return descriptor_->Clone(descriptor);
}

IFACEMETHODIMP TourelleSource::Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat,
                                  const PROPVARIANT* startPosition) {
    if (!descriptor || !startPosition) return E_INVALIDARG;
    if (timeFormat && *timeFormat != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;

    HRESULT hr = StartCamera();
    if (FAILED(hr)) return hr;
    streaming_ = true;

    ComPtr<IUnknown> stream;
    stream_.As(&stream);
    hr = queue_->QueueEventParamUnk(started_ ? MEUpdatedStream : MENewStream, GUID_NULL, S_OK, stream.Get());
    if (FAILED(hr)) return hr;
    started_ = true;
    if (FAILED(hr = stream_->Start())) return hr;

    PROPVARIANT time{};
    time.vt = VT_I8;
    time.hVal.QuadPart = MFGetSystemTime();
    return queue_->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &time);
}

IFACEMETHODIMP TourelleSource::Stop() {
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    streaming_ = false;
    StopCamera();
    HRESULT hr = stream_->Stop();
    if (FAILED(hr)) return hr;
    return queue_->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
}

IFACEMETHODIMP TourelleSource::Pause() { return MF_E_INVALID_STATE_TRANSITION; }

IFACEMETHODIMP TourelleSource::Shutdown() {
    {
        std::lock_guard guard(lock_);
        if (shutdown_) return MF_E_SHUTDOWN;
    }
    CloseIdleTimer();  // outside lock_: waits for a running callback, which takes lock_
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    streaming_ = false;
    StopCamera();  // before the stream goes away: the camera thread feeds it
    if (stream_) stream_->Shutdown();
    if (queue_) queue_->Shutdown();
    shutdown_ = true;
    return S_OK;
}

// IMFMediaSourceEx ------------------------------------------------------------

IFACEMETHODIMP TourelleSource::GetSourceAttributes(IMFAttributes** attributes) {
    if (!attributes) return E_POINTER;
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    return attributes_.CopyTo(attributes);  // the store itself, not a copy
}

IFACEMETHODIMP TourelleSource::GetStreamAttributes(DWORD streamId, IMFAttributes** attributes) {
    if (!attributes) return E_POINTER;
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    if (streamId != kStreamId) return MF_E_INVALIDSTREAMNUMBER;
    *attributes = stream_->Attributes();
    (*attributes)->AddRef();
    return S_OK;
}

IFACEMETHODIMP TourelleSource::SetD3DManager(IUnknown*) { return S_OK; }  // system memory samples only

// IMFGetService ---------------------------------------------------------------

IFACEMETHODIMP TourelleSource::GetService(REFGUID, REFIID, LPVOID* object) {
    if (object) *object = nullptr;
    return MF_E_UNSUPPORTED_SERVICE;
}

// IKsControl ------------------------------------------------------------------

IFACEMETHODIMP TourelleSource::KsProperty(PKSPROPERTY property, ULONG propertyLength, LPVOID data, ULONG dataLength,
                                       ULONG* bytesReturned) {
    if (!property || propertyLength < sizeof(KSPROPERTY) || !bytesReturned) return E_INVALIDARG;
    *bytesReturned = 0;
    if (property->Set != PROPSETID_TourelleMotor) return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    if (!(property->Flags & KSPROPERTY_TYPE_SET)) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

    const ULONG needed = property->Id == TourelleMotor_PanTiltRelative ? 4
                         : property->Id == TourelleMotor_PanTiltReset  ? 1
                                                                     : 0;
    if (needed == 0) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (!data || dataLength < needed) {
        *bytesReturned = needed;
        return HRESULT_FROM_WIN32(ERROR_MORE_DATA);
    }
    return MotorCommand(property->Id, static_cast<const BYTE*>(data), needed);
}

IFACEMETHODIMP TourelleSource::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*) {
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP TourelleSource::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*) {
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

// IMFSampleAllocatorControl ---------------------------------------------------

IFACEMETHODIMP TourelleSource::SetDefaultAllocator(DWORD outputStreamId, IUnknown* allocator) {
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    if (outputStreamId != kStreamId) return MF_E_INVALIDSTREAMNUMBER;
    return stream_->SetAllocator(allocator);
}

IFACEMETHODIMP TourelleSource::GetAllocatorUsage(DWORD outputStreamId, DWORD* inputStreamId,
                                              MFSampleAllocatorUsage* usage) {
    if (!inputStreamId || !usage) return E_POINTER;
    if (outputStreamId != kStreamId) return MF_E_INVALIDSTREAMNUMBER;
    *inputStreamId = outputStreamId;
    *usage = MFSampleAllocatorUsage_UsesProvidedAllocator;
    return S_OK;
}

// Camera ----------------------------------------------------------------------

HRESULT TourelleSource::StartCamera() {
    if (camera_) return S_OK;
    try {
        auto camera = UvcDevice::Open();
        const FrameFormat* format = nullptr;
        for (const auto& f : camera->Formats()) {
            if (!f.mjpeg && f.width == TourelleStream::kWidth && f.height == TourelleStream::kHeight) format = &f;
        }
        if (!format) return MF_E_INVALIDMEDIATYPE;
        camera->SetConstantFrameRate(true);
        ComPtr<TourelleStream> stream = stream_;
        camera->Start(*format, kFrameInterval,
                      [stream](const uint8_t* frame, size_t size) { stream->DeliverFrame(frame, size); });
        camera_ = std::move(camera);
        return S_OK;
    } catch (...) {
        return HResultFromException();
    }
}

void TourelleSource::StopCamera() {
    camera_.reset();  // stops streaming and releases WinUSB for other processes
}

HRESULT TourelleSource::MotorCommand(ULONG id, const BYTE* data, ULONG length) {
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    try {
        // Not streaming: open the camera just for this command.
        std::unique_ptr<UvcDevice> temporary;
        UvcDevice* camera = camera_.get();
        if (!camera) {
            temporary = UvcDevice::Open();
            camera = temporary.get();
        }
        if (id == TourelleMotor_PanTiltRelative && length == 4) {
            int16_t pan = 0, tilt = 0;
            memcpy(&pan, data, 2);
            memcpy(&tilt, data + 2, 2);
            camera->PanTiltRelative(pan, tilt);
        } else if (id == TourelleMotor_PanTiltReset && length == 1) {
            camera->PanTiltReset((data[0] & 1) != 0, (data[0] & 2) != 0);
        }
        return S_OK;
    } catch (...) {
        return HResultFromException();
    }
}

}  // namespace tourelle
