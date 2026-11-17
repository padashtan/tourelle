#include "TourelleStream.h"

#include <ks.h>
#include <ksmedia.h>
#include <mferror.h>

#include "Imaging.h"

using Microsoft::WRL::ComPtr;

namespace tourelle {

HRESULT TourelleStream::RuntimeClassInitialize(IMFMediaSource* source, DWORD streamId) {
    source_ = source;
    HRESULT hr = MFCreateEventQueue(&queue_);
    if (FAILED(hr)) return hr;

    // NV12 first (what most apps want), then YUY2, for every mode.
    std::vector<ComPtr<IMFMediaType>> types;
    for (const GUID& subtype : {MFVideoFormat_NV12, MFVideoFormat_YUY2}) {
        for (const VideoMode& mode : kModes) {
            ComPtr<IMFMediaType> type;
            if (FAILED(hr = CreateMediaType(subtype, mode, &type))) return hr;
            types.push_back(type);
        }
    }
    std::vector<IMFMediaType*> raw;
    for (const auto& type : types) raw.push_back(type.Get());
    if (FAILED(hr = MFCreateStreamDescriptor(streamId, static_cast<DWORD>(raw.size()), raw.data(), &descriptor_))) {
        return hr;
    }

    ComPtr<IMFMediaTypeHandler> handler;
    if (FAILED(hr = descriptor_->GetMediaTypeHandler(&handler))) return hr;
    if (FAILED(hr = handler->SetCurrentMediaType(raw.front()))) return hr;

    if (FAILED(hr = descriptor_.As(&attributes_))) return hr;
    attributes_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
    attributes_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, streamId);
    attributes_->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
    attributes_->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);
    return S_OK;
}

HRESULT TourelleStream::CreateMediaType(REFGUID subtype, const VideoMode& mode, IMFMediaType** result) {
    ComPtr<IMFMediaType> type;
    HRESULT hr = MFCreateMediaType(&type);
    if (FAILED(hr)) return hr;
    const UINT32 bitsPerPixel = subtype == MFVideoFormat_NV12 ? 12 : 16;
    const LONG stride = static_cast<LONG>(subtype == MFVideoFormat_NV12 ? mode.width : mode.width * 2);
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, subtype);
    MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, mode.width, mode.height);
    MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, mode.fps, 1);
    MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    type->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(stride));
    type->SetUINT32(MF_MT_AVG_BITRATE, mode.width * mode.height * bitsPerPixel * mode.fps);
    *result = type.Detach();
    return S_OK;
}

TourelleStream::VideoMode TourelleStream::SelectedMode() {
    ComPtr<IMFMediaTypeHandler> handler;
    ComPtr<IMFMediaType> type;
    UINT32 width = 0, height = 0;
    if (SUCCEEDED(descriptor_->GetMediaTypeHandler(&handler)) && SUCCEEDED(handler->GetCurrentMediaType(&type)) &&
        SUCCEEDED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height))) {
        for (const VideoMode& mode : kModes) {
            if (mode.width == width && mode.height == height) return mode;
        }
    }
    return kModes[0];
}

// IMFMediaEventGenerator ------------------------------------------------------

IFACEMETHODIMP TourelleStream::GetEvent(DWORD flags, IMFMediaEvent** event) {
    ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard guard(lock_);
        if (shutdown_) return MF_E_SHUTDOWN;
        queue = queue_;
    }
    return queue->GetEvent(flags, event);  // may block: do not hold the lock
}

IFACEMETHODIMP TourelleStream::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
    std::lock_guard guard(lock_);
    return shutdown_ ? MF_E_SHUTDOWN : queue_->BeginGetEvent(callback, state);
}

IFACEMETHODIMP TourelleStream::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
    std::lock_guard guard(lock_);
    return shutdown_ ? MF_E_SHUTDOWN : queue_->EndGetEvent(result, event);
}

IFACEMETHODIMP TourelleStream::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status,
                                       const PROPVARIANT* eventValue) {
    std::lock_guard guard(lock_);
    return shutdown_ ? MF_E_SHUTDOWN : queue_->QueueEventParamVar(type, extendedType, status, eventValue);
}

// IMFMediaStream --------------------------------------------------------------

IFACEMETHODIMP TourelleStream::GetMediaSource(IMFMediaSource** source) {
    if (!source) return E_POINTER;
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    return source_.CopyTo(source);
}

IFACEMETHODIMP TourelleStream::GetStreamDescriptor(IMFStreamDescriptor** descriptor) {
    if (!descriptor) return E_POINTER;
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    return descriptor_.CopyTo(descriptor);
}

IFACEMETHODIMP TourelleStream::RequestSample(IUnknown* token) {
    std::function<void()> wake;
    {
        std::lock_guard guard(lock_);
        if (shutdown_) return MF_E_SHUTDOWN;
        if (state_ != MF_STREAM_STATE_RUNNING) return MF_E_INVALIDREQUEST;
        tokens_.emplace_back(token);  // answered by the next camera frame
        lastRequest_ = GetTickCount64();
        wake = wake_;
    }
    if (wake) wake();  // reopens the camera if the source stopped it while idle
    return S_OK;
}

// IMFMediaStream2 -------------------------------------------------------------

IFACEMETHODIMP TourelleStream::SetStreamState(MF_STREAM_STATE state) {
    switch (state) {
        case MF_STREAM_STATE_RUNNING:
            return Start();
        case MF_STREAM_STATE_STOPPED:
            return Stop();
        default:
            return MF_E_INVALID_STATE_TRANSITION;  // no pause on a live camera
    }
}

IFACEMETHODIMP TourelleStream::GetStreamState(MF_STREAM_STATE* state) {
    if (!state) return E_POINTER;
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    *state = state_;
    return S_OK;
}

// Source side -----------------------------------------------------------------

HRESULT TourelleStream::SetAllocator(IUnknown* allocator) {
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    if (state_ == MF_STREAM_STATE_RUNNING) return MF_E_INVALIDREQUEST;
    allocator_.Reset();
    return allocator ? allocator->QueryInterface(IID_PPV_ARGS(&allocator_)) : S_OK;
}

TourelleStream::VideoMode TourelleStream::ModeOf(IMFMediaType* type) {
    UINT32 width = 0, height = 0;
    if (type && SUCCEEDED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height))) {
        for (const VideoMode& mode : kModes) {
            if (mode.width == width && mode.height == height) return mode;
        }
    }
    return kModes[0];
}

TourelleStream::VideoMode TourelleStream::Mode() {
    std::lock_guard guard(lock_);
    return mode_;
}

HRESULT TourelleStream::Start(IMFMediaType* selected) {
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    if (state_ == MF_STREAM_STATE_RUNNING) return S_OK;

    // The type comes from the presentation descriptor the pipeline passed to the
    // source's Start; IMFMediaStream2::SetStreamState has none and uses ours.
    ComPtr<IMFMediaType> type = selected;
    HRESULT hr = S_OK;
    if (!type) {
        ComPtr<IMFMediaTypeHandler> handler;
        hr = descriptor_->GetMediaTypeHandler(&handler);
        if (SUCCEEDED(hr)) hr = handler->GetCurrentMediaType(&type);
    }
    if (SUCCEEDED(hr)) hr = type->GetGUID(MF_MT_SUBTYPE, &subtype_);
    if (FAILED(hr)) return hr;
    if (subtype_ != MFVideoFormat_NV12 && subtype_ != MFVideoFormat_YUY2) return MF_E_INVALIDMEDIATYPE;
    mode_ = ModeOf(type.Get());

    // Use the pipeline's allocator when it gave one, otherwise our own.
    if (!allocator_ && FAILED(hr = MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&allocator_)))) return hr;
    if (FAILED(hr = allocator_->InitializeSampleAllocator(10, type.Get()))) return hr;

    state_ = MF_STREAM_STATE_RUNNING;
    tokens_.clear();
    lastRequest_ = GetTickCount64();
    return queue_->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
}

HRESULT TourelleStream::Stop() {
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    if (state_ == MF_STREAM_STATE_STOPPED) return S_OK;
    state_ = MF_STREAM_STATE_STOPPED;
    tokens_.clear();
    if (allocator_) allocator_->UninitializeSampleAllocator();
    return queue_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
}

void TourelleStream::Shutdown() {
    std::lock_guard guard(lock_);
    if (shutdown_) return;
    shutdown_ = true;
    state_ = MF_STREAM_STATE_STOPPED;
    tokens_.clear();
    if (queue_) queue_->Shutdown();
    if (allocator_) allocator_->UninitializeSampleAllocator();
    allocator_.Reset();
    source_.Reset();
    wake_ = nullptr;
}

void TourelleStream::DeliverFrame(const BYTE* yuy2, size_t size) {
    std::lock_guard guard(lock_);
    if (size < static_cast<size_t>(mode_.width) * mode_.height * 2) return;  // frame of another mode
    if (shutdown_ || state_ != MF_STREAM_STATE_RUNNING || tokens_.empty()) return;  // nobody asked: drop

    ComPtr<IMFSample> sample;
    if (FAILED(allocator_->AllocateSample(&sample))) return;  // all buffers in flight: drop the frame
    if (FAILED(FillSample(yuy2, sample.Get()))) return;

    ComPtr<IUnknown> token = std::move(tokens_.front());
    tokens_.pop_front();
    sample->SetSampleTime(MFGetSystemTime());
    sample->SetSampleDuration(10'000'000 / mode_.fps);
    if (token) sample->SetUnknown(MFSampleExtension_Token, token.Get());
    queue_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
}

HRESULT TourelleStream::FillSample(const BYTE* yuy2, IMFSample* sample) {
    ComPtr<IMFMediaBuffer> buffer;
    ComPtr<IMF2DBuffer2> buffer2d;
    HRESULT hr = sample->GetBufferByIndex(0, &buffer);
    if (SUCCEEDED(hr)) hr = buffer.As(&buffer2d);
    if (FAILED(hr)) return hr;

    BYTE* scanline0 = nullptr;
    BYTE* start = nullptr;
    LONG pitch = 0;
    DWORD length = 0;
    if (FAILED(hr = buffer2d->Lock2DSize(MF2DBuffer_LockFlags_Write, &scanline0, &pitch, &start, &length))) return hr;

    const UINT32 width = mode_.width, height = mode_.height;
    const size_t srcStride = static_cast<size_t>(width) * 2;
    if (subtype_ == MFVideoFormat_YUY2) {
        if (pitch < static_cast<LONG>(srcStride) || length < static_cast<DWORD>(pitch) * height) {
            buffer2d->Unlock2D();
            return E_UNEXPECTED;
        }
        for (UINT32 y = 0; y < height; ++y) memcpy(scanline0 + static_cast<ptrdiff_t>(y) * pitch, yuy2 + y * srcStride, srcStride);
    } else if (pitch >= static_cast<LONG>(width) && length >= static_cast<DWORD>(pitch) * height * 3 / 2) {
        core::Yuy2ToNv12(yuy2, width, height, scanline0, pitch);
    }
    buffer2d->Unlock2D();
    return S_OK;
}

}  // namespace tourelle
