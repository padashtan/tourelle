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

// Motor commands through the source's IKsControl. Units: 1/64 degree. They are
// queued by the source and return immediately.
HRESULT PanTiltRelative(IUnknown* source, int16_t pan, int16_t tilt);
HRESULT PanTiltReset(IUnknown* source);
// Absolute position; the source recenters first when it does not know where the head is.
HRESULT PanTiltAbsolute(IUnknown* source, int16_t pan, int16_t tilt);
// Position tracked by the source. known is false until the head was recentered once.
HRESULT GetPosition(IUnknown* source, int16_t* pan, int16_t* tilt, bool* known);
// Focus motor, 0 (far) to 255 (near).
HRESULT SetFocus(IUnknown* source, uint8_t value);
HRESULT GetFocus(IUnknown* source, uint8_t* value);

}  // namespace tourelle::client
