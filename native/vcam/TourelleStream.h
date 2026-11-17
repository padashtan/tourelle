#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <deque>
#include <mutex>

namespace tourelle {

class TourelleSource;

// The single video stream of the virtual camera. Turns camera frames (YUY2)
// into Media Foundation samples, one per RequestSample call.
class TourelleStream
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Microsoft::WRL::ChainInterfaces<IMFMediaStream2, IMFMediaStream,
                                                                          IMFMediaEventGenerator>> {
public:
    static constexpr UINT32 kWidth = 640;
    static constexpr UINT32 kHeight = 480;
    static constexpr UINT32 kFps = 30;

    HRESULT RuntimeClassInitialize(IMFMediaSource* source, DWORD streamId);

    // IMFMediaEventGenerator
    IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
    IFACEMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status,
                              const PROPVARIANT* eventValue) override;

    // IMFMediaStream
    IFACEMETHODIMP GetMediaSource(IMFMediaSource** source) override;
    IFACEMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** descriptor) override;
    IFACEMETHODIMP RequestSample(IUnknown* token) override;

    // IMFMediaStream2
    IFACEMETHODIMP SetStreamState(MF_STREAM_STATE state) override;
    IFACEMETHODIMP GetStreamState(MF_STREAM_STATE* state) override;

    // Called by the source.
    HRESULT SetAllocator(IUnknown* allocator);
    HRESULT Start();
    HRESULT Stop();
    void Shutdown();
    void DeliverFrame(const BYTE* yuy2, size_t size);  // camera thread
    IMFAttributes* Attributes() const { return attributes_.Get(); }

private:
    HRESULT CreateMediaType(REFGUID subtype, IMFMediaType** type);
    HRESULT FillSample(const BYTE* yuy2, IMFSample* sample);

    std::mutex lock_;
    bool shutdown_ = false;
    MF_STREAM_STATE state_ = MF_STREAM_STATE_STOPPED;
    Microsoft::WRL::ComPtr<IMFMediaSource> source_;  // released at Shutdown to break the cycle
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue_;
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> descriptor_;
    Microsoft::WRL::ComPtr<IMFAttributes> attributes_;  // the stream descriptor's attributes
    Microsoft::WRL::ComPtr<IMFVideoSampleAllocator> allocator_;
    GUID subtype_ = MFVideoFormat_NV12;
    std::deque<Microsoft::WRL::ComPtr<IUnknown>> tokens_;  // pending requests, may hold null tokens
};

}  // namespace tourelle
