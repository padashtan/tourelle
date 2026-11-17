#include <random>

#include "Test.h"
#include "UvcDescriptors.h"
#include "data/SphereAfDescriptor.h"

using namespace tourelle::core;

namespace {
UvcConfiguration Real() { return ParseConfiguration(kSphereAfConfiguration, sizeof(kSphereAfConfiguration)); }
}  // namespace

TEST(descriptor_header_and_terminals) {
    const auto config = Real();
    EXPECT_EQ(config.bcdUvc, 0x0100);
    EXPECT_EQ(config.cameraTerminal, 1);
    EXPECT_EQ(config.processingUnit, 2);
    EXPECT_EQ(config.processingControls, 0x175Bu);  // brightness, contrast, saturation, sharpness, WB, ...
    EXPECT_EQ(config.streamInterface, 1);
    EXPECT_EQ(config.streamEndpoint, 0x81);
}

TEST(descriptor_finds_hidden_motor_unit) {
    const auto config = Real();
    const auto motor = config.FindUnit(kLogitechMotorUnit);
    EXPECT(motor.has_value());
    if (motor) EXPECT_EQ(*motor, 9);
    int hidden = 0, visible = 0;
    for (const auto& unit : config.units) (unit.hidden ? hidden : visible)++;
    EXPECT_EQ(hidden, 4);   // ...2250, 2251, 2255, 2256
    EXPECT_EQ(visible, 3);  // ...221E, 221F, 2252
}

TEST(descriptor_formats) {
    const auto config = Real();
    EXPECT_EQ(config.formats.size(), 15u);  // 7 MJPEG + 8 YUY2 frame sizes
    bool yuy2Vga = false, mjpegSvga = false;
    for (const auto& f : config.formats) {
        EXPECT(!f.intervals.empty());
        if (!f.mjpeg && f.width == 640 && f.height == 480) yuy2Vga = f.intervals.front() == 333333;
        if (f.mjpeg && f.width == 800 && f.height == 600) mjpegSvga = f.intervals.front() == 333333;
    }
    EXPECT(yuy2Vga);
    EXPECT(mjpegSvga);
}

TEST(descriptor_every_prefix_is_safe) {
    // Truncation at any byte must neither crash nor read out of bounds (run under ASan or
    // page heap to catch overruns); the result is simply partial.
    for (size_t size = 0; size <= sizeof(kSphereAfConfiguration); ++size) {
        std::vector<uint8_t> copy(kSphereAfConfiguration, kSphereAfConfiguration + size);
        const auto config = ParseConfiguration(copy.data(), copy.size());
        EXPECT(config.formats.size() <= 15u);
    }
}

TEST(descriptor_random_garbage_is_safe) {
    std::mt19937 random(1234);
    for (int round = 0; round < 2000; ++round) {
        std::vector<uint8_t> bytes(random() % 512);
        for (auto& b : bytes) b = static_cast<uint8_t>(random());
        ParseConfiguration(bytes.data(), bytes.size());
    }
    // Mutations of the real descriptor reach deeper branches than pure noise.
    for (int round = 0; round < 2000; ++round) {
        std::vector<uint8_t> bytes(kSphereAfConfiguration, kSphereAfConfiguration + sizeof(kSphereAfConfiguration));
        for (int k = 0; k < 8; ++k) bytes[random() % bytes.size()] = static_cast<uint8_t>(random());
        ParseConfiguration(bytes.data(), bytes.size());
    }
    EXPECT(ParseConfiguration(nullptr, 100).formats.empty());
}

TEST(descriptor_zero_length_does_not_loop) {
    const uint8_t stuck[] = {0x00, 0x24, 0x01, 0x00};
    EXPECT(ParseConfiguration(stuck, sizeof(stuck)).units.empty());
}
