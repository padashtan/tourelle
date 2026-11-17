#include "FaceTracker.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.FaceAnalysis.h>
#include <winrt/Windows.Storage.Streams.h>

#include <cstring>

namespace tourelle::app {

using winrt::Windows::Graphics::Imaging::BitmapAlphaMode;
using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
using winrt::Windows::Media::FaceAnalysis::FaceDetector;

struct FaceTracker::Impl {
    FaceDetector detector{nullptr};
};

FaceTracker::FaceTracker() : impl_(std::make_unique<Impl>()) {}
FaceTracker::~FaceTracker() = default;

std::unique_ptr<FaceTracker> FaceTracker::Create() {
    try {
        if (!FaceDetector::IsSupported()) return nullptr;
        std::unique_ptr<FaceTracker> tracker(new FaceTracker());
        tracker->impl_->detector = FaceDetector::CreateAsync().get();
        return tracker;
    } catch (...) {
        return nullptr;
    }
}

std::vector<FaceBox> FaceTracker::Detect(const uint8_t* bgra, uint32_t width, uint32_t height) {
    std::vector<FaceBox> faces;
    if (!bgra || width == 0 || height == 0) return faces;
    try {
        const uint32_t size = width * height * 4;
        winrt::Windows::Storage::Streams::Buffer buffer(size);
        std::memcpy(buffer.data(), bgra, size);
        buffer.Length(size);
        SoftwareBitmap color(BitmapPixelFormat::Bgra8, static_cast<int32_t>(width), static_cast<int32_t>(height),
                             BitmapAlphaMode::Ignore);
        color.CopyFromBuffer(buffer);
        const SoftwareBitmap gray = SoftwareBitmap::Convert(color, BitmapPixelFormat::Gray8);
        for (const auto& face : impl_->detector.DetectFacesAsync(gray).get()) {
            const auto box = face.FaceBox();
            faces.push_back({static_cast<float>(box.X), static_cast<float>(box.Y), static_cast<float>(box.Width),
                             static_cast<float>(box.Height)});
        }
    } catch (...) {
        faces.clear();  // detection is best effort
    }
    return faces;
}

}  // namespace tourelle::app
