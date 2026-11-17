#pragma once
// Contract between the Tourelle virtual camera source and its clients.
// Clients send these through IKsControl::KsProperty on the virtual camera's
// media source (KSPROPERTY_TYPE_SET).

#include <guiddef.h>

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
    // SET, 4 bytes: int16 pan, int16 tilt, in 1/64 degree, relative to the current position.
    TourelleMotor_PanTiltRelative = 1,
    // SET, 1 byte: bit 0 recenters pan, bit 1 recenters tilt.
    TourelleMotor_PanTiltReset = 2,
};
