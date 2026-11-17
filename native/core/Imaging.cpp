#include "Imaging.h"

namespace tourelle::core {

void Yuy2ToNv12(const uint8_t* src, uint32_t width, uint32_t height, uint8_t* dst, ptrdiff_t dstPitch) {
    const size_t srcStride = static_cast<size_t>(width) * 2;
    uint8_t* uvPlane = dst + dstPitch * static_cast<ptrdiff_t>(height);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = src + y * srcStride;
        uint8_t* lumaRow = dst + static_cast<ptrdiff_t>(y) * dstPitch;
        for (uint32_t x = 0; x < width; ++x) lumaRow[x] = row[x * 2];
        if ((y & 1) == 0) {
            // Chroma: average the two rows of each 2x2 block.
            const uint8_t* next = y + 1 < height ? row + srcStride : row;
            uint8_t* uvRow = uvPlane + static_cast<ptrdiff_t>(y / 2) * dstPitch;
            for (uint32_t x = 0; x + 1 < width; x += 2) {
                uvRow[x] = static_cast<uint8_t>((row[x * 2 + 1] + next[x * 2 + 1] + 1) / 2);      // U
                uvRow[x + 1] = static_cast<uint8_t>((row[x * 2 + 3] + next[x * 2 + 3] + 1) / 2);  // V
            }
        }
    }
}

double Sharpness(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t bytesPerPixel, uint32_t lumaOffset) {
    if (!pixels || width < 6 || height < 6 || bytesPerPixel == 0 || lumaOffset >= bytesPerPixel) return 0.0;
    const auto luma = [&](uint32_t x, uint32_t y) {
        return static_cast<double>(pixels[(static_cast<size_t>(y) * width + x) * bytesPerPixel + lumaOffset]);
    };
    double sum = 0, sumSquares = 0;
    size_t count = 0;
    for (uint32_t y = height / 3; y < 2 * height / 3; ++y) {
        for (uint32_t x = width / 3; x < 2 * width / 3; ++x) {
            const double l = 4 * luma(x, y) - luma(x - 1, y) - luma(x + 1, y) - luma(x, y - 1) - luma(x, y + 1);
            sum += l;
            sumSquares += l * l;
            ++count;
        }
    }
    if (count == 0) return 0.0;
    const double mean = sum / static_cast<double>(count);
    return sumSquares / static_cast<double>(count) - mean * mean;
}

}  // namespace tourelle::core
