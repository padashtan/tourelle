#pragma once
// Face detection with the detector built into Windows (Windows.Media.FaceAnalysis).

#include <cstdint>
#include <memory>
#include <vector>

namespace tourelle::app {

struct FaceBox {
    float x = 0, y = 0, width = 0, height = 0;  // pixels, top-left origin
};

class FaceTracker {
public:
    // nullptr when Windows has no face detector. Call from an MTA thread.
    static std::unique_ptr<FaceTracker> Create();
    ~FaceTracker();

    // Detects faces in a top-down BGRA frame. Blocking: call from a worker thread.
    std::vector<FaceBox> Detect(const uint8_t* bgra, uint32_t width, uint32_t height);

private:
    FaceTracker();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tourelle::app
