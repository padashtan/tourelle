#pragma once
// Installation state and one-click setup of Tourelle.
//
// Setup has two halves:
//  - machine-wide (administrator): hand the camera's video function to WinUSB
//    and install the virtual camera media source in Program Files;
//  - per user: register the "Tourelle Camera" virtual camera.

#include <string>

namespace tourelle::setup {

struct Status {
    bool cameraPresent = false;    // USB 046D:0994 video function connected
    bool winusbBound = false;      // video function driven by WinUSB (Tourelle) instead of usbvideo.sys
    bool sourceInstalled = false;  // media source DLL + COM registration in place
    bool virtualCameraListed = false;

    bool Ready() const { return cameraPresent && winusbBound && sourceInstalled && virtualCameraListed; }
    bool AnythingInstalled() const { return winusbBound || sourceInstalled || virtualCameraListed; }
};

// Needs COM and Media Foundation started on the calling thread.
Status Query();

// Technical details for bug reports: device ids, drivers, installed files, services.
// Contains no personal data (no user name, no paths under the user profile).
std::wstring Describe();

// Administrator half; run inside the elevated process. Throw std::exception on failure.
void InstallElevated(const std::wstring& sourceDll);
void UninstallElevated();

// Starts this executable elevated with one argument and waits for it.
// Returns its exit code, or -1 when the user declined the UAC prompt.
int RunElevated(const wchar_t* argument);

}  // namespace tourelle::setup
