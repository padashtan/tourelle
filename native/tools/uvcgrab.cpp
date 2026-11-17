// uvcgrab: hardware test bench for the WinUSB-bound camera (no virtual camera).
// The camera must be free: quit Tourelle and wait a few seconds first.
//
//   uvcgrab [frame.bmp]   stream, move the head during the stream, save the last frame
//   uvcgrab fov           measure the horizontal and vertical field of view
//   uvcgrab focus         dump the focus control and measure sharpness across its range

#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "../uvc/UvcDevice.h"

namespace {

constexpr uint8_t kMotorUnit = 9;
constexpr uint8_t kFocusSelector = 3;

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
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
}

// Streams YUY2 640x480 and keeps the latest frame.
class Grabber {
public:
    explicit Grabber(tourelle::UvcDevice& camera) : camera_(camera) {
        for (const auto& f : camera.Formats()) {
            if (!f.mjpeg && f.width == 640 && f.height == 480) format_ = f;
        }
        if (format_.width == 0) throw std::runtime_error("YUY2 640x480 not available");
        camera.SetConstantFrameRate(true);
        camera.Start(format_, format_.intervals.front(), [this](const uint8_t* data, size_t size) {
            std::lock_guard guard(lock_);
            last_.assign(data, data + size);
        });
    }
    ~Grabber() { camera_.Stop(); }
    std::vector<uint8_t> Frame() {
        std::lock_guard guard(lock_);
        return last_;
    }
    int Width() const { return static_cast<int>(format_.width); }
    int Height() const { return static_cast<int>(format_.height); }

private:
    tourelle::UvcDevice& camera_;
    tourelle::FrameFormat format_;
    std::mutex lock_;
    std::vector<uint8_t> last_;
};

// Mean luminance per column (horizontal = true) or per row.
std::vector<double> Profile(const std::vector<uint8_t>& yuy2, int width, int height, bool horizontal) {
    std::vector<double> profile(horizontal ? width : height, 0.0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) profile[horizontal ? x : y] += yuy2[(static_cast<size_t>(y) * width + x) * 2];
    }
    for (auto& v : profile) v /= horizontal ? height : width;
    return profile;
}

// Shift (in pixels) that best aligns b onto a: b[i] ~ a[i + shift].
int BestShift(const std::vector<double>& a, const std::vector<double>& b, int maxShift) {
    int best = 0;
    double bestError = 1e300;
    const int n = static_cast<int>(a.size());
    for (int s = -maxShift; s <= maxShift; ++s) {
        double error = 0;
        int count = 0;
        for (int i = 0; i < n; ++i) {
            const int j = i + s;
            if (j < 0 || j >= n) continue;
            error += std::fabs(a[j] - b[i]);
            ++count;
        }
        if (count > n / 2 && error / count < bestError) {
            bestError = error / count;
            best = s;
        }
    }
    return best;
}

// Variance of the Laplacian over the central third: higher is sharper.
double Sharpness(const std::vector<uint8_t>& yuy2, int width, int height) {
    auto luma = [&](int x, int y) { return static_cast<double>(yuy2[(static_cast<size_t>(y) * width + x) * 2]); };
    double sum = 0, sumSquares = 0;
    int count = 0;
    for (int y = height / 3; y < 2 * height / 3; ++y) {
        for (int x = width / 3; x < 2 * width / 3; ++x) {
            const double l = 4 * luma(x, y) - luma(x - 1, y) - luma(x + 1, y) - luma(x, y - 1) - luma(x, y + 1);
            sum += l;
            sumSquares += l * l;
            ++count;
        }
    }
    const double mean = sum / count;
    return sumSquares / count - mean * mean;
}

std::string Hex(const std::vector<uint8_t>& data) {
    std::string text;
    char byte[4];
    for (uint8_t b : data) {
        sprintf_s(byte, "%02X", b);
        text += byte;
    }
    return text;
}

int MeasureFov(tourelle::UvcDevice& camera) {
    camera.PanTiltReset();
    Sleep(6000);
    Grabber grab(camera);
    Sleep(2500);
    const auto a = grab.Frame();
    constexpr int kPanUnits = 1280, kTiltUnits = 640;  // 20 and 10 degrees
    camera.PanTiltRelative(kPanUnits, 0);
    Sleep(5000);
    const auto b = grab.Frame();
    camera.PanTiltRelative(0, kTiltUnits);
    Sleep(5000);
    const auto c = grab.Frame();
    const int w = grab.Width(), h = grab.Height();
    if (a.empty() || b.empty() || c.empty()) throw std::runtime_error("no frames");

    const int panShift = BestShift(Profile(a, w, h, true), Profile(b, w, h, true), w / 3);
    const int tiltShift = BestShift(Profile(b, w, h, false), Profile(c, w, h, false), h / 3);
    printf("pan 20 deg  -> image shift %d px\n", panShift);
    printf("tilt 10 deg -> image shift %d px\n", tiltShift);
    if (panShift) printf("horizontal FOV ~ %.1f deg (positive pan moves the scene %s)\n", 20.0 * w / std::abs(panShift),
                         panShift > 0 ? "left" : "right");
    if (tiltShift) printf("vertical FOV   ~ %.1f deg (positive tilt moves the scene %s)\n",
                          10.0 * h / std::abs(tiltShift), tiltShift > 0 ? "up" : "down");
    camera.PanTiltReset();
    return 0;
}

int ProbeFocus(tourelle::UvcDevice& camera) {
    const char* names[] = {"CUR", "MIN", "MAX", "RES", "LEN", "INFO", "DEF"};
    const uint8_t requests[] = {0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87};
    for (int i = 0; i < 7; ++i) {
        const uint16_t length = requests[i] == 0x85 ? 2 : requests[i] == 0x86 ? 1 : 6;
        try {
            printf("focus %-4s %s\n", names[i], Hex(camera.GetControl(requests[i], kMotorUnit, kFocusSelector, length)).c_str());
        } catch (const std::exception& e) {
            printf("focus %-4s failed: %s\n", names[i], e.what());
        }
    }
    const auto original = camera.GetControl(0x81, kMotorUnit, kFocusSelector, 6);
    Grabber grab(camera);
    Sleep(2000);
    printf("current      sharpness %.1f\n", Sharpness(grab.Frame(), grab.Width(), grab.Height()));
    for (int value = 0; value <= 255; value += 32) {
        auto data = original;
        data[0] = static_cast<uint8_t>(value);
        try {
            camera.SetControl(kMotorUnit, kFocusSelector, data);
        } catch (const std::exception& e) {
            printf("set byte0=%3d failed: %s\n", value, e.what());
            continue;
        }
        Sleep(900);
        printf("byte0=%3d    sharpness %.1f   readback %s\n", value, Sharpness(grab.Frame(), grab.Width(), grab.Height()),
               Hex(camera.GetControl(0x81, kMotorUnit, kFocusSelector, 6)).c_str());
    }
    camera.SetControl(kMotorUnit, kFocusSelector, original);
    return 0;
}

int StreamTest(tourelle::UvcDevice& camera, const wchar_t* path) {
    for (const auto& f : camera.Formats()) {
        wprintf(L"format %u frame %u %s %ux%u, fastest %.1f fps\n", f.formatIndex, f.frameIndex,
                f.mjpeg ? L"MJPEG" : L"YUY2", f.width, f.height, f.intervals.empty() ? 0.0 : 1e7 / f.intervals.front());
    }
    std::vector<uint8_t> last;
    {
        Grabber grab(camera);
        auto report = [&](const wchar_t* step) {
            wprintf(L"%-28s frames %llu, dropped %llu\n", step, camera.FramesDelivered(), camera.FramesDropped());
        };
        Sleep(2000);
        report(L"after 2 s:");
        camera.PanTiltRelative(1280, 0);  // 20 degrees, while streaming
        Sleep(2500);
        report(L"after pan +20 deg:");
        camera.PanTiltReset();
        Sleep(5000);
        report(L"after reset:");
        last = grab.Frame();
        if (last.empty()) throw std::runtime_error("No complete frame received");
        SaveBmp(path, last, grab.Width(), grab.Height());
    }
    wprintf(L"saved %s\n", path);
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        auto camera = tourelle::UvcDevice::Open();
        const std::wstring mode = argc > 1 ? argv[1] : L"";
        if (mode == L"fov") return MeasureFov(*camera);
        if (mode == L"focus") return ProbeFocus(*camera);
        return StreamTest(*camera, argc > 1 ? argv[1] : L"frame.bmp");
    } catch (const std::exception& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
