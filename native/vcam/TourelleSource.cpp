#include "TourelleSource.h"

#include <ksmedia.h>
#include <mferror.h>

#include <algorithm>
#include <cstring>
#include <system_error>

#include "TourelleProperties.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;

namespace tourelle {
namespace {

constexpr DWORD kStreamId = 0;
constexpr ULONGLONG kIdleStopMs = 3000;         // close the camera after this long without a sample request
constexpr ULONGLONG kRetryOpenMs = 1000;        // at most one reopen attempt per second (unplugged camera)

// UVC processing unit controls behind PROPSETID_VIDCAP_VIDEOPROCAMP.
struct ProcAmpControl {
    ULONG ksId;
    uint8_t selector;      // PU_*_CONTROL
    uint8_t bit;           // bit in the processing unit's bmControls
    uint8_t size;          // bytes
    bool isSigned;
    uint8_t autoSelector;  // 0 when there is no automatic mode
    uint8_t autoBit;
};

constexpr ProcAmpControl kProcAmp[] = {
    {KSPROPERTY_VIDEOPROCAMP_BACKLIGHT_COMPENSATION, 0x01, 8, 2, false, 0, 0},
    {KSPROPERTY_VIDEOPROCAMP_BRIGHTNESS, 0x02, 0, 2, true, 0, 0},
    {KSPROPERTY_VIDEOPROCAMP_CONTRAST, 0x03, 1, 2, false, 0, 0},
    {KSPROPERTY_VIDEOPROCAMP_GAIN, 0x04, 9, 2, false, 0, 0},
    {KSPROPERTY_VIDEOPROCAMP_POWERLINE_FREQUENCY, 0x05, 10, 1, false, 0, 0},
    {KSPROPERTY_VIDEOPROCAMP_HUE, 0x06, 2, 2, true, 0x10, 11},
    {KSPROPERTY_VIDEOPROCAMP_SATURATION, 0x07, 3, 2, false, 0, 0},
    {KSPROPERTY_VIDEOPROCAMP_SHARPNESS, 0x08, 4, 2, false, 0, 0},
    {KSPROPERTY_VIDEOPROCAMP_GAMMA, 0x09, 5, 2, false, 0, 0},
    {KSPROPERTY_VIDEOPROCAMP_WHITEBALANCE, 0x0A, 6, 2, false, 0x0B, 12},
};

const ProcAmpControl* FindProcAmp(ULONG id) {
    for (const auto& control : kProcAmp) {
        if (control.ksId == id) return &control;
    }
    return nullptr;
}

LONG Decode(const std::vector<uint8_t>& bytes, const ProcAmpControl& control) {
    if (bytes.size() < control.size) return 0;
    if (control.size == 1) return bytes[0];
    const uint16_t raw = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
    return control.isSigned ? static_cast<int16_t>(raw) : raw;
}

std::vector<uint8_t> Encode(LONG amount, const ProcAmpControl& control) {
    if (control.size == 1) return {static_cast<uint8_t>(amount)};
    const uint16_t raw = static_cast<uint16_t>(amount);
    return {static_cast<uint8_t>(raw & 0xFF), static_cast<uint8_t>(raw >> 8)};
}

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

HRESULT MoreData(ULONG needed, ULONG* returned) {
    *returned = needed;
    return HRESULT_FROM_WIN32(ERROR_MORE_DATA);
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

// Idle handling -----------------------------------------------------------------

void CALLBACK TourelleSource::OnIdleTimer(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER) {
    static_cast<TourelleSource*>(context)->StopIfIdle();
}

void TourelleSource::StopIfIdle() {
    std::lock_guard guard(lock_);
    if (shutdown_ || !camera_) return;
    if (camera_->Lost() || stream_->IdleMilliseconds() > kIdleStopMs) StopCamera();
}

void TourelleSource::Wake() {
    std::lock_guard guard(lock_);
    if (shutdown_ || !streaming_ || camera_) return;
    const ULONGLONG now = GetTickCount64();
    if (now - lastStartAttempt_ < kRetryOpenMs) return;
    StartCamera(stream_->Mode());
}

void TourelleSource::CloseIdleTimer() {
    if (!idleTimer_) return;
    SetThreadpoolTimer(idleTimer_, nullptr, 0, 0);
    WaitForThreadpoolTimerCallbacks(idleTimer_, TRUE);  // never under lock_: the callback takes it
    CloseThreadpoolTimer(idleTimer_);
    idleTimer_ = nullptr;
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

    // The media type the pipeline chose lives in the descriptor it passes here.
    ComPtr<IMFStreamDescriptor> selectedStream;
    ComPtr<IMFMediaTypeHandler> handler;
    ComPtr<IMFMediaType> selectedType;
    BOOL selected = FALSE;
    HRESULT hr = descriptor->GetStreamDescriptorByIndex(0, &selected, &selectedStream);
    if (SUCCEEDED(hr)) hr = selectedStream->GetMediaTypeHandler(&handler);
    if (SUCCEEDED(hr)) hr = handler->GetCurrentMediaType(&selectedType);
    if (FAILED(hr)) return hr;

    if (FAILED(hr = StartCamera(TourelleStream::ModeOf(selectedType.Get())))) return hr;
    streaming_ = true;

    ComPtr<IUnknown> stream;
    stream_.As(&stream);
    hr = queue_->QueueEventParamUnk(started_ ? MEUpdatedStream : MENewStream, GUID_NULL, S_OK, stream.Get());
    if (FAILED(hr)) return hr;
    started_ = true;
    if (FAILED(hr = stream_->Start(selectedType.Get()))) return hr;

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
    CloseIdleTimer();  // outside lock_: waits for a running callback, which takes it
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
// Requests come from any process that opens the camera: copy and size-check
// everything before use.

IFACEMETHODIMP TourelleSource::KsProperty(PKSPROPERTY property, ULONG propertyLength, LPVOID data, ULONG dataLength,
                                          ULONG* bytesReturned) {
    if (!property || propertyLength < sizeof(KSPROPERTY) || !bytesReturned) return E_INVALIDARG;
    *bytesReturned = 0;
    KSPROPERTY header;
    std::memcpy(&header, property, sizeof(header));
    {
        std::lock_guard guard(lock_);
        if (shutdown_) return MF_E_SHUTDOWN;
    }
    if (header.Set == PROPSETID_TourelleMotor) {
        return MotorProperty(header.Id, header.Flags, data, dataLength, bytesReturned);
    }
    if (header.Set == PROPSETID_VIDCAP_CAMERACONTROL) {
        return CameraControlProperty(header.Id, header.Flags, data, dataLength, bytesReturned);
    }
    if (header.Set == PROPSETID_VIDCAP_VIDEOPROCAMP) {
        return ProcAmpProperty(header.Id, header.Flags, data, dataLength, bytesReturned);
    }
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

HRESULT TourelleSource::MotorProperty(ULONG id, ULONG flags, void* data, ULONG length, ULONG* returned) {
    const core::KsOperation operation = core::OperationOf(flags);
    switch (id) {
        case TourelleMotor_PanTiltRelative:
        case TourelleMotor_PanTiltAbsolute: {
            if (operation == core::KsOperation::Get && id == TourelleMotor_PanTiltAbsolute) {
                if (!data || length < sizeof(TourellePosition)) return MoreData(sizeof(TourellePosition), returned);
                TourellePosition position{};
                bool known = false;
                const core::PanTilt current = MotorService::Get().Position(&known);
                position.pan = core::ToMotorValue(current.pan);
                position.tilt = core::ToMotorValue(current.tilt);
                position.flags = known ? TourellePosition_Known : 0;
                std::memcpy(data, &position, sizeof(position));
                *returned = sizeof(position);
                return S_OK;
            }
            if (operation != core::KsOperation::Set) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            if (!data || length < sizeof(TourellePanTilt)) return MoreData(sizeof(TourellePanTilt), returned);
            TourellePanTilt move;
            std::memcpy(&move, data, sizeof(move));
            MotorService::Get().Enqueue({id == TourelleMotor_PanTiltRelative ? MotorService::Command::Kind::Relative : MotorService::Command::Kind::Absolute,
                     {move.pan, move.tilt}});
            return S_OK;
        }
        case TourelleMotor_PanTiltReset:
            if (operation != core::KsOperation::Set) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            if (!data || length < 1) return MoreData(1, returned);
            if (static_cast<const uint8_t*>(data)[0] & 3) MotorService::Get().Enqueue({MotorService::Command::Kind::Reset});
            return S_OK;
        case TourelleMotor_Focus:
            if (!data || length < 1) return MoreData(1, returned);
            if (operation == core::KsOperation::Get) {
                static_cast<uint8_t*>(data)[0] = static_cast<uint8_t>(MotorService::Get().Focus());
                *returned = 1;
                return S_OK;
            }
            if (operation != core::KsOperation::Set) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            MotorService::Get().Enqueue({MotorService::Command::Kind::Focus, {}, static_cast<const uint8_t*>(data)[0]});
            return S_OK;
        default:
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
}

HRESULT TourelleSource::CameraControlProperty(ULONG id, ULONG flags, void* data, ULONG length, ULONG* returned) {
    core::LongRange range;
    if (id == KSPROPERTY_CAMERACONTROL_PAN) {
        range = {-core::kPanLimit / core::kUnitsPerDegree, core::kPanLimit / core::kUnitsPerDegree, 1, 0};
    } else if (id == KSPROPERTY_CAMERACONTROL_TILT) {
        range = {-core::kTiltLimit / core::kUnitsPerDegree, core::kTiltLimit / core::kUnitsPerDegree, 1, 0};
    } else if (id == KSPROPERTY_CAMERACONTROL_FOCUS) {
        range = {core::kFocusMin, core::kFocusMax, 1, 0};
    } else {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    constexpr ULONG kManual = KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;

    switch (core::OperationOf(flags)) {
        case core::KsOperation::BasicSupport:
            return core::WriteBasicSupport(range, data, length, returned);
        case core::KsOperation::Get: {
            LONG amount = 0;
            {
                const core::PanTilt position = MotorService::Get().Position(nullptr);
                amount = id == KSPROPERTY_CAMERACONTROL_PAN    ? position.pan / core::kUnitsPerDegree
                        : id == KSPROPERTY_CAMERACONTROL_TILT ? position.tilt / core::kUnitsPerDegree
                                                              : MotorService::Get().Focus();
            }
            return core::WriteControlValue(data, length, amount, kManual, kManual, returned);
        }
        case core::KsOperation::Set: {
            LONG amount = 0;
            ULONG ignored = 0;
            if (!core::ReadControlValue(data, length, &amount, &ignored)) {
                return MoreData(sizeof(KSPROPERTY_CAMERACONTROL_S), returned);
            }
            amount = std::clamp(amount, range.minimum, range.maximum);
            if (id == KSPROPERTY_CAMERACONTROL_FOCUS) {
                MotorService::Get().Enqueue({MotorService::Command::Kind::Focus, {}, static_cast<int>(amount)});
                return S_OK;
            }
            bool known = false;
            core::PanTilt target = MotorService::Get().Position(&known);
            if (!known) target = {};
            (id == KSPROPERTY_CAMERACONTROL_PAN ? target.pan : target.tilt) = amount * core::kUnitsPerDegree;
            MotorService::Get().Enqueue({MotorService::Command::Kind::Absolute, target});
            return S_OK;
        }
        default:
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
}

HRESULT TourelleSource::ProcAmpProperty(ULONG id, ULONG flags, void* data, ULONG length, ULONG* returned) {
    const ProcAmpControl* control = FindProcAmp(id);
    if (!control) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    const core::KsOperation operation = core::OperationOf(flags);
    if (operation == core::KsOperation::Unsupported) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

    // Validate the caller's buffer before touching the camera.
    LONG requested = 0;
    ULONG requestedFlags = 0;
    if (operation == core::KsOperation::Set && !core::ReadControlValue(data, length, &requested, &requestedFlags)) {
        return MoreData(sizeof(KSPROPERTY_VIDEOPROCAMP_S), returned);
    }
    if (operation == core::KsOperation::Get && (!data || length < sizeof(KSPROPERTY_VIDEOPROCAMP_S))) {
        return MoreData(sizeof(KSPROPERTY_VIDEOPROCAMP_S), returned);
    }

    HRESULT result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    HRESULT hr = CameraHub::Get().With([&](UvcDevice& camera) {
        const auto& config = camera.Configuration();
        if (!config.processingUnit || !(config.processingControls & (1u << control->bit))) return;  // not on this camera
        const uint8_t unit = config.processingUnit;
        const bool hasAuto = control->autoSelector && (config.processingControls & (1u << control->autoBit));
        const ULONG capabilities = KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL |
                                   (hasAuto ? KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO : 0);

        auto it = procAmpRanges_.find(id);
        if (it == procAmpRanges_.end()) {
            core::LongRange range;
            range.minimum = Decode(camera.GetControl(kGetMin, unit, control->selector, control->size), *control);
            range.maximum = Decode(camera.GetControl(kGetMax, unit, control->selector, control->size), *control);
            range.step = std::max<LONG>(1, Decode(camera.GetControl(kGetRes, unit, control->selector, control->size),
                                                  *control));
            range.defaultValue = Decode(camera.GetControl(kGetDef, unit, control->selector, control->size), *control);
            if (range.minimum > range.maximum) return;  // nonsense from the device: treat as unsupported
            it = procAmpRanges_.emplace(id, range).first;
        }
        const core::LongRange& range = it->second;

        switch (operation) {
            case core::KsOperation::BasicSupport:
                result = core::WriteBasicSupport(range, data, length, returned);
                break;
            case core::KsOperation::Get: {
                const LONG amount = Decode(camera.GetControl(kGetCur, unit, control->selector, control->size), *control);
                bool automatic = false;
                if (hasAuto) {
                    const auto mode = camera.GetControl(kGetCur, unit, control->autoSelector, 1);
                    automatic = !mode.empty() && mode[0] != 0;  // the device may answer with fewer bytes
                }
                result = core::WriteControlValue(
                    data, length, amount,
                    automatic ? KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO : KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL,
                    capabilities, returned);
                break;
            }
            case core::KsOperation::Set: {
                const bool automatic = hasAuto && (requestedFlags & KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO);
                if (hasAuto) camera.SetControl(unit, control->autoSelector, {static_cast<uint8_t>(automatic ? 1 : 0)});
                if (!automatic) {
                    camera.SetControl(unit, control->selector,
                                      Encode(std::clamp(requested, range.minimum, range.maximum), *control));
                }
                result = S_OK;
                break;
            }
            case core::KsOperation::Unsupported:  // rejected before reaching the camera
                break;
        }
    });
    return FAILED(hr) ? hr : result;
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

HRESULT TourelleSource::StartCamera(const TourelleStream::VideoMode& mode) {
    if (camera_) return S_OK;
    lastStartAttempt_ = GetTickCount64();
    try {
        auto camera = UvcDevice::Open();
        const FrameFormat* format = nullptr;
        for (const auto& f : camera->Formats()) {
            if (!f.mjpeg && f.width == mode.width && f.height == mode.height) format = &f;
        }
        if (!format) return MF_E_INVALIDMEDIATYPE;
        camera->SetConstantFrameRate(true);
        ComPtr<TourelleStream> stream = stream_;
        camera->Start(*format, 10'000'000 / mode.fps,
                      [stream](const uint8_t* frame, size_t size) { stream->DeliverFrame(frame, size); });
        camera_ = std::move(camera);
        CameraHub::Get().Attach(camera_.get());
        return S_OK;
    } catch (...) {
        return HResultFromException();
    }
}

void TourelleSource::StopCamera() {
    if (camera_) CameraHub::Get().Detach(camera_.get());  // waits for a motor command using it
    camera_.reset();  // stops streaming and releases WinUSB for other processes
}

}  // namespace tourelle
