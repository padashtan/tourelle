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

namespace tourelle {

// Device interface GUID registered for the WinUSB-bound video function
// (DeviceInterfaceGUIDs written by the bind tool).
// {293D3D63-AF2C-42DA-84B9-B9DDC76A7AB7}
inline constexpr GUID kWinUsbInterfaceGuid = {
    0x293d3d63, 0xaf2c, 0x42da, {0x84, 0xb9, 0xb9, 0xdd, 0xc7, 0x6a, 0x7a, 0xb7}};

struct FrameFormat {
    uint8_t formatIndex = 0;  // bFormatIndex
    uint8_t frameIndex = 0;   // bFrameIndex
    bool mjpeg = false;       // false: YUY2
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> intervals;  // 100 ns units, fastest first
};

class UvcDevice {
public:
    // Called on the streaming thread with one complete frame.
    using FrameCallback = std::function<void(const uint8_t* data, size_t size)>;

    // Opens the first WinUSB-bound camera. Throws std::runtime_error.
    static std::unique_ptr<UvcDevice> Open();
    ~UvcDevice();

    UvcDevice(const UvcDevice&) = delete;
    UvcDevice& operator=(const UvcDevice&) = delete;

    const std::vector<FrameFormat>& Formats() const { return formats_; }

    // Negotiates the format, selects the alternate setting and starts streaming.
    void Start(const FrameFormat& format, uint32_t interval, FrameCallback callback);
    void Stop();
    bool Streaming() const { return running_; }

    // Relative move in 1/64 degree. Pan range is about +/-70 degrees, tilt +/-30.
    void PanTiltRelative(int16_t pan, int16_t tilt);
    void PanTiltReset(bool pan = true, bool tilt = true);

    // true: keep the negotiated frame rate in low light (UVC auto-exposure priority = 0).
    // false: let the camera lower the frame rate to expose longer.
    void SetConstantFrameRate(bool constant);

    // Diagnostics: frames delivered and dropped (incomplete or error) since Start.
    uint64_t FramesDelivered() const { return delivered_; }
    uint64_t FramesDropped() const { return dropped_; }

private:
    UvcDevice(HANDLE file, WINUSB_INTERFACE_HANDLE control, WINUSB_INTERFACE_HANDLE stream);

    void ParseConfiguration();
    void Control(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                 WINUSB_INTERFACE_HANDLE handle, std::vector<uint8_t>& data);
    void StreamLoop();
    void OnPayload(const uint8_t* data, size_t size);
    void EmitFrame();

    HANDLE file_;
    WINUSB_INTERFACE_HANDLE control_;  // interface 0: video control
    WINUSB_INTERFACE_HANDLE stream_;   // interface 1: video streaming

    uint16_t bcdUvc_ = 0x0100;
    uint8_t cameraTerminal_ = 1;  // bTerminalID of the camera input terminal
    uint8_t streamInterface_ = 1;
    uint8_t streamEndpoint_ = 0x81;
    std::vector<FrameFormat> formats_;

    std::mutex controlLock_;  // one control transfer at a time on endpoint 0
    std::thread thread_;
    std::atomic<bool> running_{false};
    FrameCallback callback_;
    size_t expectedSize_ = 0;  // exact size for YUY2, 0 for MJPEG

    std::vector<uint8_t> frame_;
    bool haveFid_ = false;
    bool lastFid_ = false;
    bool frameError_ = false;
    std::atomic<uint64_t> delivered_{0};
    std::atomic<uint64_t> dropped_{0};
};

}  // namespace tourelle
