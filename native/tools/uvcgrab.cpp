// Streams video from the WinUSB-bound camera, moves the head while streaming,
// and saves the last frame as a BMP. Proves "live" pan/tilt.

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <vector>

#include "../uvc/UvcDevice.h"

namespace {

uint8_t Clamp(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

// YUY2 to bottom-up BGR24 BMP.
void SaveBmp(const wchar_t* path, const std::vector<uint8_t>& yuy2, int width, int height) {
    const int stride = (width * 3 + 3) & ~3;
    std::vector<uint8_t> pixels(static_cast<size_t>(stride) * height);
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = &yuy2[static_cast<size_t>(y) * width * 2];
        uint8_t* dst = &pixels[static_cast<size_t>(height - 1 - y) * stride];
        for (int x = 0; x < width; x += 2, src += 4) {
            const int u = src[1] - 128, v = src[3] - 128;
            for (int k = 0; k < 2; ++k) {
                const int c = (src[k * 2] - 16) * 298;
                *dst++ = Clamp((c + 516 * u + 128) >> 8);
                *dst++ = Clamp((c - 100 * u - 208 * v + 128) >> 8);
                *dst++ = Clamp((c + 409 * v + 128) >> 8);
            }
        }
    }
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = height;
    info.biPlanes = 1;
    info.biBitCount = 24;
    info.biSizeImage = static_cast<DWORD>(pixels.size());
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + info.biSizeImage;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    out.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        auto camera = tourelle::UvcDevice::Open();
        const tourelle::FrameFormat* chosen = nullptr;
        for (const auto& f : camera->Formats()) {
            wprintf(L"format %u frame %u %s %ux%u, fastest %.1f fps\n", f.formatIndex, f.frameIndex,
                    f.mjpeg ? L"MJPEG" : L"YUY2", f.width, f.height,
                    f.intervals.empty() ? 0.0 : 1e7 / f.intervals.front());
            if (!f.mjpeg && f.width == 640 && f.height == 480) chosen = &f;
        }
        if (!chosen) throw std::runtime_error("YUY2 640x480 not available");

        camera->SetConstantFrameRate(true);
        std::mutex lock;
        std::vector<uint8_t> last;
        camera->Start(*chosen, chosen->intervals.front(), [&](const uint8_t* data, size_t size) {
            std::lock_guard guard(lock);
            last.assign(data, data + size);
        });

        auto report = [&](const wchar_t* step) {
            wprintf(L"%-28s frames %llu, dropped %llu\n", step, camera->FramesDelivered(), camera->FramesDropped());
        };
        Sleep(2000);
        report(L"after 2 s:");
        camera->PanTiltRelative(1280, 0);  // 20 degrees, while streaming
        Sleep(2500);
        report(L"after pan +20 deg:");
        camera->PanTiltReset();
        Sleep(5000);
        report(L"after reset:");
        camera->Stop();

        const wchar_t* path = argc > 1 ? argv[1] : L"frame.bmp";
        std::lock_guard guard(lock);
        if (last.empty()) throw std::runtime_error("No complete frame received");
        SaveBmp(path, last, static_cast<int>(chosen->width), static_cast<int>(chosen->height));
        wprintf(L"saved %s\n", path);
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
