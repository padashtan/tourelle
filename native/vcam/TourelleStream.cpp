#include "TourelleStream.h"

#include <ks.h>
#include <ksmedia.h>
#include <mferror.h>

using Microsoft::WRL::ComPtr;

namespace tourelle {

HRESULT TourelleStream::RuntimeClassInitialize(IMFMediaSource* source, DWORD streamId) {
    source_ = source;
    HRESULT hr = MFCreateEventQueue(&queue_);
    if (FAILED(hr)) return hr;

    ComPtr<IMFMediaType> nv12, yuy2;
    if (FAILED(hr = CreateMediaType(MFVideoFormat_NV12, &nv12))) return hr;
    if (FAILED(hr = CreateMediaType(MFVideoFormat_YUY2, &yuy2))) return hr;
    IMFMediaType* types[] = {nv12.Get(), yuy2.Get()};
    if (FAILED(hr = MFCreateStreamDescriptor(streamId, ARRAYSIZE(types), types, &descriptor_))) return hr;

    ComPtr<IMFMediaTypeHandler> handler;
    if (FAILED(hr = descriptor_->GetMediaTypeHandler(&handler))) return hr;
    if (FAILED(hr = handler->SetCurrentMediaType(nv12.Get()))) return hr;

    if (FAILED(hr = descriptor_.As(&attributes_))) return hr;
    attributes_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
    attributes_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, streamId);
    attributes_->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
    attributes_->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);
    return S_OK;
}

HRESULT TourelleStream::CreateMediaType(REFGUID subtype, IMFMediaType** result) {
    ComPtr<IMFMediaType> type;
    HRESULT hr = MFCreateMediaType(&type);
    if (FAILED(hr)) return hr;
    const UINT32 bitsPerPixel = subtype == MFVideoFormat_NV12 ? 12 : 16;
    const LONG stride = subtype == MFVideoFormat_NV12 ? kWidth : kWidth * 2;
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, subtype);
    MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, kWidth, kHeight);
    MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, kFps, 1);
    MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    type->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(stride));
    type->SetUINT32(MF_MT_AVG_BITRATE, kWidth * kHeight * bitsPerPixel * kFps);
    *result = type.Detach();
    return S_OK;
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
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    if (state_ != MF_STREAM_STATE_RUNNING) return MF_E_INVALIDREQUEST;
    tokens_.emplace_back(token);  // answered by the next camera frame
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

HRESULT TourelleStream::Start() {
    std::lock_guard guard(lock_);
    if (shutdown_) return MF_E_SHUTDOWN;
    if (state_ == MF_STREAM_STATE_RUNNING) return S_OK;

    ComPtr<IMFMediaTypeHandler> handler;
    ComPtr<IMFMediaType> type;
    HRESULT hr = descriptor_->GetMediaTypeHandler(&handler);
    if (SUCCEEDED(hr)) hr = handler->GetCurrentMediaType(&type);
    if (SUCCEEDED(hr)) hr = type->GetGUID(MF_MT_SUBTYPE, &subtype_);
    if (FAILED(hr)) return hr;

    // Use the pipeline's allocator when it gave one, otherwise our own.
    if (!allocator_ && FAILED(hr = MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&allocator_)))) return hr;
    if (FAILED(hr = allocator_->InitializeSampleAllocator(10, type.Get()))) return hr;

    state_ = MF_STREAM_STATE_RUNNING;
    tokens_.clear();
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
}

void TourelleStream::DeliverFrame(const BYTE* yuy2, size_t size) {
    if (size < static_cast<size_t>(kWidth) * kHeight * 2) return;
    std::lock_guard guard(lock_);
    if (shutdown_ || state_ != MF_STREAM_STATE_RUNNING || tokens_.empty()) return;  // nobody asked: drop

    ComPtr<IMFSample> sample;
    if (FAILED(allocator_->AllocateSample(&sample))) return;  // all buffers in flight: drop the frame
    if (FAILED(FillSample(yuy2, sample.Get()))) return;

    ComPtr<IUnknown> token = std::move(tokens_.front());
    tokens_.pop_front();
    sample->SetSampleTime(MFGetSystemTime());
    sample->SetSampleDuration(10'000'000 / kFps);
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

    const size_t srcStride = kWidth * 2;
    if (subtype_ == MFVideoFormat_YUY2) {
        for (UINT32 y = 0; y < kHeight; ++y) memcpy(scanline0 + static_cast<ptrdiff_t>(y) * pitch, yuy2 + y * srcStride, srcStride);
    } else {
        // NV12: full-resolution Y plane, then interleaved U/V at half resolution.
        BYTE* uvPlane = scanline0 + static_cast<ptrdiff_t>(pitch) * kHeight;
        for (UINT32 y = 0; y < kHeight; ++y) {
            const BYTE* src = yuy2 + y * srcStride;
            BYTE* dstY = scanline0 + static_cast<ptrdiff_t>(y) * pitch;
            for (UINT32 x = 0; x < kWidth; ++x) dstY[x] = src[x * 2];
            if ((y & 1) == 0) {
                BYTE* dstUV = uvPlane + static_cast<ptrdiff_t>(y / 2) * pitch;
                for (UINT32 x = 0; x < kWidth; x += 2) {
                    dstUV[x] = src[x * 2 + 1];      // U
                    dstUV[x + 1] = src[x * 2 + 3];  // V
                }
            }
        }
    }
    buffer2d->Unlock2D();
    return S_OK;
}

}  // namespace tourelle
