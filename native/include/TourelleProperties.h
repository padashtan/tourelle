#pragma once
// Contract between the Tourelle virtual camera source and its clients.
// Clients send these through IKsControl::KsProperty on the virtual camera's
// media source. The source also answers the standard PROPSETID_VIDCAP_CAMERACONTROL
// (pan, tilt, focus) and PROPSETID_VIDCAP_VIDEOPROCAMP (image settings) sets.

#include <guiddef.h>

#include <cstdint>

// Media source COM class (InprocServer32) loaded by the Windows Camera Frame Server.
// {7268DC8D-D8F5-47C9-B981-EC9E9199EAEE}
inline constexpr GUID CLSID_TourelleCamera = {
    0x7268dc8d, 0xd8f5, 0x47c9, {0xb9, 0x81, 0xec, 0x9e, 0x91, 0x99, 0xea, 0xee}};
inline constexpr wchar_t kTourelleCameraClsid[] = L"{7268DC8D-D8F5-47C9-B981-EC9E9199EAEE}";
inline constexpr wchar_t kTourelleCameraName[] = L"Tourelle Camera";

// {73A18ABC-B68D-4667-A1FF-8BB65783F9BD}
inline constexpr GUID PROPSETID_TourelleMotor = {
    0x73a18abc, 0xb68d, 0x4667, {0xa1, 0xff, 0x8b, 0xb6, 0x57, 0x83, 0xf9, 0xbd}};

enum TourelleMotorProperty : unsigned long {
    // SET, TourellePanTilt: move relative to the current position, in 1/64 degree.
    TourelleMotor_PanTiltRelative = 1,
    // SET, 1 byte: bit 0 recenters pan, bit 1 recenters tilt.
    TourelleMotor_PanTiltReset = 2,
    // SET, TourellePanTilt: go to an absolute position (recenters first if unknown).
    // GET, TourellePosition: current position as tracked by the source.
    TourelleMotor_PanTiltAbsolute = 3,
    // SET/GET, 1 byte: focus motor, 0 (far) to 255 (near).
    TourelleMotor_Focus = 4,
};

#pragma pack(push, 1)
struct TourellePanTilt {
    int16_t pan;
    int16_t tilt;
};
struct TourellePosition {
    int16_t pan;
    int16_t tilt;
    uint32_t flags;  // TourellePosition_Known when the head was recentered since the source started
};
#pragma pack(pop)

inline constexpr uint32_t TourellePosition_Known = 1;
