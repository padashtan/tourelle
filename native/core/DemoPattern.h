#pragma once
// Synthetic camera image for Tourelle.exe --demo (screenshots, translations,
// trying the app without the camera). Pure code.

#include <cstdint>

#include "Motor.h"

namespace tourelle::core {

// Draws a top-down BGRA frame of a simple "room" seen from the head position:
// panning or tilting shifts the scene as the real camera does.
void DrawDemoFrame(uint8_t* bgra, uint32_t width, uint32_t height, PanTilt position, double seconds);

}  // namespace tourelle::core
