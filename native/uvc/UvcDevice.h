#pragma once
// User-mode UVC driver for the Logitech QuickCam Orbit/Sphere AF over WinUSB.
//
// Windows' usbvideo.sys cannot reach the motor extension unit (Logitech hides it
// in a vendor 0x41 descriptor), so the video function (interfaces 0 and 1) is
// bound to WinUSB and this class does both jobs: isochronous video streaming and
// motor control.

#include <windows.h>
#include <winusb.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "FrameAssembler.h"
#include "UvcDescriptors.h"

namespace tourelle {

// Device interface GUID registered for the WinUSB-bound video function
// (DeviceInterfaceGUIDs written by the setup).
// {293D3D63-AF2C-42DA-84B9-B9DDC76A7AB7}
inline constexpr GUID kWinUsbInterfaceGuid = {
    0x293d3d63, 0xaf2c, 0x42da, {0x84, 0xb9, 0xb9, 0xdd, 0xc7, 0x6a, 0x7a, 0xb7}};

using FrameFormat = core::FrameFormat;

// UVC request codes used by Tourelle.
inline constexpr uint8_t kGetCur = 0x81, kGetMin = 0x82, kGetMax = 0x83, kGetRes = 0x84, kGetDef = 0x87;

class UvcDevice {
public:
    // Called on the streaming thread with one complete frame.
    using FrameCallback = std::function<void(const uint8_t* data, size_t size)>;

    // Opens the first WinUSB-bound camera. Throws std::runtime_error.
    static std::unique_ptr<UvcDevice> Open();
    ~UvcDevice();

    UvcDevice(const UvcDevice&) = delete;
    UvcDevice& operator=(const UvcDevice&) = delete;

    const std::vector<FrameFormat>& Formats() const { return config_.formats; }
    const core::UvcConfiguration& Configuration() const { return config_; }

    // Negotiates the format, selects the alternate setting and starts streaming.
    void Start(const FrameFormat& format, uint32_t interval, FrameCallback callback);
    void Stop();
    // The stream stopped by itself because the device went away (unplugged).
    bool Lost() const { return lost_; }

    // Relative move in 1/64 degree. Pan range is about +/-70 degrees, tilt +/-30.
    void PanTiltRelative(int16_t pan, int16_t tilt);
    void PanTiltReset(bool pan = true, bool tilt = true);
    // Focus motor target, 0 (far) to 255 (near).
    void SetFocus(uint8_t value);

    // Raw class requests to a unit of the video control interface (see kGet*).
    std::vector<uint8_t> GetControl(uint8_t request, uint8_t unit, uint8_t selector, uint16_t length);
    void SetControl(uint8_t unit, uint8_t selector, std::vector<uint8_t> data);

    // true: keep the negotiated frame rate in low light (UVC auto-exposure priority = 0).
    // false: let the camera lower the frame rate to expose longer.
    void SetConstantFrameRate(bool constant);

    // Diagnostics: frames delivered and dropped (incomplete or error) since Start.
    uint64_t FramesDelivered() const { return delivered_; }
    uint64_t FramesDropped() const { return dropped_; }

private:
    UvcDevice(HANDLE file, WINUSB_INTERFACE_HANDLE control, WINUSB_INTERFACE_HANDLE stream);

    void ReadConfiguration();
    void Control(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                 WINUSB_INTERFACE_HANDLE handle, std::vector<uint8_t>& data);
    void StreamLoop();

    HANDLE file_;
    WINUSB_INTERFACE_HANDLE control_;  // interface 0: video control
    WINUSB_INTERFACE_HANDLE stream_;   // interface 1: video streaming

    core::UvcConfiguration config_;
    uint8_t motorUnit_ = 9;  // found by GUID in the descriptor; 9 on a Sphere AF

    std::mutex controlLock_;  // one control transfer at a time on endpoint 0
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> lost_{false};
    core::FrameAssembler assembler_;  // streaming thread only
    std::atomic<uint64_t> delivered_{0};
    std::atomic<uint64_t> dropped_{0};
};

}  // namespace tourelle
