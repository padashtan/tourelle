#include <vector>

#include "DemoPattern.h"
#include "Test.h"

using namespace tourelle::core;

namespace {
uint32_t Checksum(const std::vector<uint8_t>& image) {
    uint32_t sum = 0;
    for (size_t i = 0; i < image.size(); ++i) sum = sum * 31 + image[i];
    return sum;
}
}  // namespace

TEST(demo_frame_is_opaque_and_moves_with_the_head) {
    const uint32_t w = 64, h = 48;
    std::vector<uint8_t> centered(w * h * 4), panned(w * h * 4);
    DrawDemoFrame(centered.data(), w, h, {0, 0}, 0.0);
    DrawDemoFrame(panned.data(), w, h, {640, 0}, 0.0);
    bool opaque = true;
    for (size_t i = 3; i < centered.size(); i += 4) opaque = opaque && centered[i] == 255;
    EXPECT(opaque);
    EXPECT(Checksum(centered) != Checksum(panned));
    DrawDemoFrame(nullptr, w, h, {}, 0.0);  // ignored, no crash
}
