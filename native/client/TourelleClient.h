#pragma once
// Client side of the "Tourelle Camera" virtual camera: registration for the
// current user, lookup through the Windows Camera Frame Server, and motor
// commands. Callers own COM and Media Foundation initialization.

#include <windows.h>
#include <mfidl.h>

#include <cstdint>

namespace tourelle::client {

// Registers (or re-enables) the virtual camera for the current user. Not elevated.
HRESULT EnableVirtualCamera();
HRESULT RemoveVirtualCamera();

// True when Windows lists the virtual camera. Does not activate it.
bool IsVirtualCameraListed();

// Activates the virtual camera's media source (shared through the Frame Server).
HRESULT FindSource(IMFMediaSource** source);

// Motor commands through the source's IKsControl. Units: 1/64 degree.
HRESULT PanTiltRelative(IUnknown* source, int16_t pan, int16_t tilt);
HRESULT PanTiltReset(IUnknown* source);

}  // namespace tourelle::client
