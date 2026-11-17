#include "FrameAssembler.h"

namespace tourelle::core {

void FrameAssembler::Reset(size_t expectedSize, size_t maxSize, Sink sink) {
    sink_ = std::move(sink);
    expectedSize_ = expectedSize;
    maxSize_ = maxSize;
    frame_.clear();
    frame_.reserve(expectedSize ? expectedSize : maxSize / 4);
    haveFid_ = lastFid_ = error_ = false;
    delivered_ = dropped_ = 0;
}

void FrameAssembler::OnPayload(const uint8_t* data, size_t size) {
    if (!data || size < 2) return;
    // UVC payload header: bHeaderLength, bmHeaderInfo (bit 0 FID, 1 EOF, 2 PTS, 3 SCR,
    // 6 ERR), then 4 bytes of PTS and 6 of SCR when their bits are set: 12 bytes at most.
    const uint8_t headerLength = data[0];
    const uint8_t info = data[1];
    const size_t required = 2 + ((info & 0x04) ? 4 : 0) + ((info & 0x08) ? 6 : 0);
    if (headerLength < required || headerLength > kMaxHeaderLength || headerLength > size) {
        error_ = true;  // corrupt packet: the frame it belongs to is not trustworthy
        return;
    }
    const bool fid = (info & 0x01) != 0;

    // A new frame began without EOF: close the previous one, even if only an error
    // was recorded for it (otherwise that error would spill onto the new frame).
    if (haveFid_ && fid != lastFid_ && (!frame_.empty() || error_)) Emit();
    haveFid_ = true;
    lastFid_ = fid;
    if (info & 0x40) error_ = true;

    const size_t bytes = size - headerLength;
    if (frame_.size() + bytes > maxSize_) {
        error_ = true;  // keep counting the frame as one drop, but stop growing
    } else {
        frame_.insert(frame_.end(), data + headerLength, data + size);
    }
    if (info & 0x02) Emit();
}

void FrameAssembler::Emit() {
    const bool complete = !error_ && !frame_.empty() && (expectedSize_ == 0 || frame_.size() == expectedSize_);
    if (complete) {
        if (sink_) sink_(frame_.data(), frame_.size());
        ++delivered_;
    } else if (!frame_.empty() || error_) {
        ++dropped_;
    }
    frame_.clear();
    error_ = false;
}

}  // namespace tourelle::core
