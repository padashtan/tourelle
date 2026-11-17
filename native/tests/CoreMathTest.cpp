#include <cmath>
#include <vector>

#include "Autofocus.h"
#include "Imaging.h"
#include "Motor.h"
#include "Test.h"

using namespace tourelle::core;

TEST(yuy2_to_nv12_values) {
    // 2x2 image: Y = 10,20 / 30,40; U = 100 then 110; V = 200 then 210.
    const uint8_t yuy2[] = {10, 100, 20, 200, 30, 110, 40, 210};
    uint8_t nv12[6]{};  // pitch 2: Y plane 2x2, UV plane 1 row
    Yuy2ToNv12(yuy2, 2, 2, nv12, 2);
    EXPECT_EQ(nv12[0], 10);
    EXPECT_EQ(nv12[1], 20);
    EXPECT_EQ(nv12[2], 30);
    EXPECT_EQ(nv12[3], 40);
    EXPECT_EQ(nv12[4], 105);  // U averaged over the two rows
    EXPECT_EQ(nv12[5], 205);  // V averaged
}

TEST(sharpness_flat_vs_texture) {
    const uint32_t w = 30, h = 30;
    std::vector<uint8_t> flat(w * h * 2, 128), checker(w * h * 2, 0);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) checker[(y * w + x) * 2] = ((x + y) & 1) ? 255 : 0;
    EXPECT(Sharpness(flat.data(), w, h, 2, 0) < 1e-9);
    EXPECT(Sharpness(checker.data(), w, h, 2, 0) > 1000.0);
    EXPECT_EQ(Sharpness(nullptr, w, h, 2, 0), 0.0);
    EXPECT_EQ(Sharpness(flat.data(), 4, 4, 2, 0), 0.0);  // too small
}

TEST(tracker_clamps_at_limits) {
    PositionTracker t;
    EXPECT(!t.Known());
    EXPECT(t.Relative({100, 100}) == (PanTilt{100, 100}));  // unknown: passed through
    t.Recentered();
    EXPECT(t.Relative({kPanLimit + 1000, 0}) == (PanTilt{kPanLimit, 0}));
    EXPECT(t.Position() == (PanTilt{kPanLimit, 0}));
    EXPECT(t.Relative({500, 0}) == (PanTilt{0, 0}));  // already at the stop
    EXPECT(t.MoveTo({0, -5000}) == (PanTilt{-kPanLimit, -kTiltLimit}));
    EXPECT(t.Position() == (PanTilt{0, -kTiltLimit}));
}

TEST(aim_directions_and_scale) {
    const PanTilt right = AimAt(100, 0, 640);
    EXPECT(right.pan < 0 && right.tilt == 0);  // positive pan moves the scene right
    const PanTilt below = AimAt(0, 50, 640);
    EXPECT(below.tilt > 0 && below.pan == 0);  // positive tilt moves the scene up
    EXPECT_EQ(AimAt(100, 0, 640).pan, AimAt(200, 0, 1280).pan);  // resolution independent
    EXPECT(AimAt(0, 0, 640) == (PanTilt{0, 0}));
    EXPECT(AimAt(10, 10, 0) == (PanTilt{0, 0}));
}

TEST(motor_value_saturates) {
    EXPECT_EQ(ToMotorValue(40000), 32767);
    EXPECT_EQ(ToMotorValue(-40000), -32768);
    EXPECT_EQ(ToMotorValue(123), 123);
}

TEST(autofocus_finds_peak) {
    // Sharpness peaks at 70.
    const auto curve = [](int p) { return 1000.0 - (p - 70) * (p - 70); };
    AutofocusSearch search(0, 255, 32, 8);
    int steps = 0;
    while (auto next = search.Next()) {
        search.Record(curve(*next));
        ++steps;
    }
    EXPECT(std::abs(search.Best() - 70) <= 8);
    EXPECT(steps < 20);
}

TEST(autofocus_peak_at_edge) {
    AutofocusSearch search(0, 255, 32, 8);
    while (auto next = search.Next()) search.Record(-*next);  // best at 0
    EXPECT_EQ(search.Best(), 0);
}
