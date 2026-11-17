#pragma once
// Motor geometry: units, limits, position tracking and aiming. Pure code.
//
// The motor counts in 1/64 degree. Directions, measured on a Sphere AF:
// positive pan moves the scene to the right in the image, positive tilt moves
// it up.

#include <cstdint>

namespace tourelle::core {

inline constexpr int kUnitsPerDegree = 64;
inline constexpr int kPanLimit = 4480;   // +-70 degrees (GET_MIN/GET_MAX of the motor control)
inline constexpr int kTiltLimit = 1920;  // +-30 degrees

// Measured at 640x480 with tools/uvcgrab fov: a 20 degree pan shifts the image
// by about 204 px, a 10 degree tilt by about 122 px.
inline constexpr double kPanUnitsPerPixel = 6.3;
inline constexpr double kTiltUnitsPerPixel = 5.3;
inline constexpr double kCalibrationWidth = 640.0;

// Focus motor: 0 = far, 255 = near.
inline constexpr int kFocusMin = 0;
inline constexpr int kFocusMax = 255;

struct PanTilt {
    int pan = 0;
    int tilt = 0;
    bool operator==(const PanTilt&) const = default;
};

int ClampPan(int pan);
int ClampTilt(int tilt);

// Head position since the last recenter. Relative moves are clamped to the
// mechanical range so the estimate stays exact.
class PositionTracker {
public:
    void Recentered() {
        known_ = true;
        position_ = {};
    }
    void Forget() { known_ = false; }
    bool Known() const { return known_; }
    PanTilt Position() const { return position_; }

    // Returns the move to send. While the position is unknown it is passed through unchanged.
    PanTilt Relative(PanTilt delta);
    // Returns the relative move that reaches target (clamped). Needs a known position.
    PanTilt MoveTo(PanTilt target);

private:
    bool known_ = false;
    PanTilt position_;
};

// Relative move that brings the image point (dx, dy) to the center.
// dx > 0 is right of center, dy > 0 below, in pixels of an image frameWidth wide.
PanTilt AimAt(double dx, double dy, double frameWidth);

// Converts a move to 16-bit protocol values, saturating instead of wrapping.
int16_t ToMotorValue(int units);

}  // namespace tourelle::core
