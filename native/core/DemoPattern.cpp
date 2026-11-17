#include "DemoPattern.h"

#include <cmath>

namespace tourelle::core {
namespace {

struct Color {
    uint8_t r, g, b;
};

Color Mix(Color a, Color b, double t) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return {static_cast<uint8_t>(a.r + (b.r - a.r) * t), static_cast<uint8_t>(a.g + (b.g - a.g) * t),
            static_cast<uint8_t>(a.b + (b.b - a.b) * t)};
}

}  // namespace

void DrawDemoFrame(uint8_t* bgra, uint32_t width, uint32_t height, PanTilt position, double seconds) {
    if (!bgra || width == 0 || height == 0) return;
    const double scale = static_cast<double>(width) / kCalibrationWidth;
    // Scene coordinates of the image's top-left corner (the scene moves opposite to the head).
    const double originX = -position.pan / kPanUnitsPerPixel * scale;
    const double originY = position.tilt / kTiltUnitsPerPixel * scale;
    const double horizon = height * 0.62;
    const double pulse = 0.5 + 0.5 * std::sin(seconds * 2.0);

    for (uint32_t y = 0; y < height; ++y) {
        const double sy = y + originY;
        for (uint32_t x = 0; x < width; ++x) {
            const double sx = x + originX;
            Color c;
            if (sy < horizon) {
                c = Mix({28, 40, 54}, {58, 78, 98}, sy / horizon);  // wall
                // Framed pictures every 260 px.
                const double fx = std::fmod(std::fabs(sx), 260.0), fy = sy - horizon * 0.25;
                if (fx > 40 && fx < 150 && fy > 0 && fy < horizon * 0.35) c = Mix({245, 165, 36}, {60, 207, 145}, fx / 150);
            } else {
                c = Mix({70, 52, 40}, {40, 30, 24}, (sy - horizon) / (height - horizon + 1));  // floor
                if (std::fmod(std::fabs(sx) + (sy - horizon) * 1.5, 80.0) < 3) c = {90, 70, 55};
            }
            // A round "face" target at the scene origin, gently pulsing.
            const double dx = sx - width * 0.5, dy = sy - height * 0.45;
            const double r = std::sqrt(dx * dx + dy * dy);
            if (r < 46) c = Mix({232, 237, 242}, {200, 210, 220}, r / 46);
            if (r >= 46 && r < 50 + 4 * pulse) c = {245, 165, 36};
            uint8_t* p = bgra + (static_cast<size_t>(y) * width + x) * 4;
            p[0] = c.b;
            p[1] = c.g;
            p[2] = c.r;
            p[3] = 255;
        }
    }
}

}  // namespace tourelle::core
