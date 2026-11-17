#pragma once
// Pixel conversions and image metrics. Pure code.

#include <cstddef>
#include <cstdint>

namespace tourelle::core {

// YUY2 (packed 4:2:2) to NV12 (planar Y + interleaved UV 4:2:0). Width and height
// must be even. dstPitch is the byte pitch of both NV12 planes; the UV plane starts
// at dst + dstPitch * height.
void Yuy2ToNv12(const uint8_t* src, uint32_t width, uint32_t height, uint8_t* dst, ptrdiff_t dstPitch);

// Focus measure: variance of the Laplacian of the luma over the central third.
// bytesPerPixel/lumaOffset select the luma byte: YUY2 = (2, 0), BGRA = (4, 1).
double Sharpness(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t bytesPerPixel, uint32_t lumaOffset);

}  // namespace tourelle::core
