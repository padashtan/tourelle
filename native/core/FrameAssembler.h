#pragma once
// Rebuilds video frames from UVC payloads (isochronous packets). Pure code.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace tourelle::core {

class FrameAssembler {
public:
    using Sink = std::function<void(const uint8_t* frame, size_t size)>;

    // expectedSize: exact frame size for uncompressed formats, 0 for MJPEG.
    // maxSize: frames growing beyond it are dropped (a faulty device cannot exhaust memory).
    void Reset(size_t expectedSize, size_t maxSize, Sink sink);

    // One payload: bHeaderLength, bmHeaderInfo (bit 0 FID, bit 1 EOF, bit 2 PTS, bit 3 SCR,
    // bit 6 ERR), optional PTS/SCR, then data. Inconsistent headers mark the frame bad.
    void OnPayload(const uint8_t* data, size_t size);

    static constexpr uint8_t kMaxHeaderLength = 12;

    // Data was lost (failed transfer): the frame in progress is incomplete.
    void MarkError() { error_ = true; }

    uint64_t Delivered() const { return delivered_; }
    uint64_t Dropped() const { return dropped_; }

private:
    void Emit();

    Sink sink_;
    std::vector<uint8_t> frame_;
    size_t expectedSize_ = 0;
    size_t maxSize_ = 0;
    bool haveFid_ = false;
    bool lastFid_ = false;
    bool error_ = false;
    uint64_t delivered_ = 0;
    uint64_t dropped_ = 0;
};

}  // namespace tourelle::core
