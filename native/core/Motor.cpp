#include "Motor.h"

#include <algorithm>
#include <cmath>

namespace tourelle::core {

int ClampPan(int pan) { return std::clamp(pan, -kPanLimit, kPanLimit); }
int ClampTilt(int tilt) { return std::clamp(tilt, -kTiltLimit, kTiltLimit); }

PanTilt PositionTracker::Relative(PanTilt delta) {
    if (!known_) return delta;
    const PanTilt target{ClampPan(position_.pan + delta.pan), ClampTilt(position_.tilt + delta.tilt)};
    const PanTilt applied{target.pan - position_.pan, target.tilt - position_.tilt};
    position_ = target;
    return applied;
}

PanTilt PositionTracker::MoveTo(PanTilt target) {
    target = {ClampPan(target.pan), ClampTilt(target.tilt)};
    const PanTilt delta{target.pan - position_.pan, target.tilt - position_.tilt};
    position_ = target;
    return delta;
}

PanTilt AimAt(double dx, double dy, double frameWidth) {
    if (frameWidth <= 0) return {};
    const double scale = kCalibrationWidth / frameWidth;
    // A point right of center needs the scene to move left: negative pan.
    // A point below center needs the scene to move up: positive tilt.
    return {static_cast<int>(std::lround(-dx * scale * kPanUnitsPerPixel)),
            static_cast<int>(std::lround(dy * scale * kTiltUnitsPerPixel))};
}

int16_t ToMotorValue(int units) { return static_cast<int16_t>(std::clamp(units, -32768, 32767)); }

}  // namespace tourelle::core
