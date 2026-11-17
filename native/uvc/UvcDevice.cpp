#include "UvcDevice.h"

#include <cfgmgr32.h>

#include <algorithm>
#include <stdexcept>
#include <system_error>

#pragma comment(lib, "winusb.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace tourelle {
namespace {

constexpr uint8_t kSetCur = 0x01;
constexpr uint8_t kRequestOutInterface = 0x21;  // class, interface, host-to-device
constexpr uint8_t kRequestInInterface = 0xA1;   // class, interface, device-to-host

constexpr uint8_t kProbeControl = 0x01;
constexpr uint8_t kCommitControl = 0x02;

// Logitech motor control extension unit selectors.
constexpr uint8_t kPanTiltRelative = 1;
constexpr uint8_t kPanTiltReset = 2;
constexpr uint8_t kFocus = 3;  // 6 bytes: [0] target, [2] current position

// Isochronous transfer shape: high speed needs a multiple of 8 packets (1 ms).
constexpr ULONG kPacketsPerTransfer = 64;
constexpr int kTransfers = 4;
// Consecutive failed submissions before the device is considered gone.
constexpr int kMaxSubmitFailures = 200;
constexpr size_t kMaxMjpegFrame = 4u << 20;

[[noreturn]] void ThrowLastError(const char* what) {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), what);
}

uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24));
}

bool DeviceGone(DWORD error) {
    return error == ERROR_DEVICE_NOT_CONNECTED || error == ERROR_FILE_NOT_FOUND || error == ERROR_BAD_COMMAND ||
           error == ERROR_INVALID_HANDLE || error == ERROR_NO_SUCH_DEVICE;
}

std::wstring FirstInterfacePath() {
    GUID guid = kWinUsbInterfaceGuid;
    ULONG length = 0;
    if (CM_Get_Device_Interface_List_SizeW(&length, &guid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS ||
        length <= 1) {
        throw std::runtime_error("No camera bound to WinUSB. Run the setup first.");
    }
    std::wstring list(length, L'\0');
    if (CM_Get_Device_Interface_ListW(&guid, nullptr, list.data(), length, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) !=
        CR_SUCCESS) {
        throw std::runtime_error("CM_Get_Device_Interface_List failed");
    }
    return std::wstring(list.c_str());  // first entry of the multi-string
}

}  // namespace

std::unique_ptr<UvcDevice> UvcDevice::Open() {
    std::wstring path = FirstInterfacePath();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (file == INVALID_HANDLE_VALUE) ThrowLastError("CreateFile");

    WINUSB_INTERFACE_HANDLE control = nullptr;
    if (!WinUsb_Initialize(file, &control)) {
        DWORD error = GetLastError();
        CloseHandle(file);
        throw std::system_error(static_cast<int>(error), std::system_category(), "WinUsb_Initialize");
    }
    WINUSB_INTERFACE_HANDLE stream = nullptr;
    if (!WinUsb_GetAssociatedInterface(control, 0, &stream)) {
        DWORD error = GetLastError();
        WinUsb_Free(control);
        CloseHandle(file);
        throw std::system_error(static_cast<int>(error), std::system_category(), "WinUsb_GetAssociatedInterface");
    }
    std::unique_ptr<UvcDevice> device(new UvcDevice(file, control, stream));
    device->ReadConfiguration();
    return device;
}

UvcDevice::UvcDevice(HANDLE file, WINUSB_INTERFACE_HANDLE control, WINUSB_INTERFACE_HANDLE stream)
    : file_(file), control_(control), stream_(stream) {}

UvcDevice::~UvcDevice() {
    Stop();
    WinUsb_Free(stream_);
    WinUsb_Free(control_);
    CloseHandle(file_);
}

void UvcDevice::ReadConfiguration() {
    USB_CONFIGURATION_DESCRIPTOR head{};
    ULONG got = 0;
    if (!WinUsb_GetDescriptor(control_, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, reinterpret_cast<PUCHAR>(&head),
                              sizeof(head), &got)) {
        ThrowLastError("WinUsb_GetDescriptor");
    }
    std::vector<uint8_t> d(head.wTotalLength);
    if (!WinUsb_GetDescriptor(control_, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, d.data(), static_cast<ULONG>(d.size()),
                              &got)) {
        ThrowLastError("WinUsb_GetDescriptor");
    }
    config_ = core::ParseConfiguration(d.data(), std::min<size_t>(got, d.size()));
    if (config_.formats.empty()) throw std::runtime_error("No video format found in the configuration descriptor");
    if (auto motor = config_.FindUnit(core::kLogitechMotorUnit)) motorUnit_ = *motor;
}

void UvcDevice::Control(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                        WINUSB_INTERFACE_HANDLE handle, std::vector<uint8_t>& data) {
    WINUSB_SETUP_PACKET setup{};
    setup.RequestType = requestType;
    setup.Request = request;
    setup.Value = value;
    setup.Index = index;
    setup.Length = static_cast<USHORT>(data.size());
    ULONG transferred = 0;
    std::lock_guard lock(controlLock_);
    if (!WinUsb_ControlTransfer(handle, setup, data.data(), static_cast<ULONG>(data.size()), &transferred, nullptr)) {
        ThrowLastError("WinUsb_ControlTransfer");
    }
    data.resize(transferred);
}

void UvcDevice::Start(const FrameFormat& format, uint32_t interval, FrameCallback callback) {
    Stop();

    // UVC 1.0 probe/commit is 26 bytes, UVC 1.1 is 34.
    const size_t probeSize = config_.bcdUvc >= 0x0110 ? 34 : 26;
    std::vector<uint8_t> probe(probeSize, 0);
    probe[0] = 0x01;  // bmHint: keep dwFrameInterval
    probe[2] = format.formatIndex;
    probe[3] = format.frameIndex;
    memcpy(&probe[4], &interval, 4);

    const uint16_t index = config_.streamInterface;
    Control(kRequestOutInterface, kSetCur, kProbeControl << 8, index, stream_, probe);
    std::vector<uint8_t> negotiated(probeSize, 0);
    Control(kRequestInInterface, kGetCur, kProbeControl << 8, index, stream_, negotiated);
    if (negotiated.size() < 26) throw std::runtime_error("Short probe response");
    const uint32_t payloadSize = Le32(&negotiated[22]);  // dwMaxPayloadTransferSize
    Control(kRequestOutInterface, kSetCur, kCommitControl << 8, index, stream_, negotiated);

    // Smallest alternate setting whose bandwidth fits one payload per (micro)frame.
    UCHAR chosen = 0;
    ULONG bestBytes = ULONG_MAX;
    for (UCHAR alt = 1; alt < 32; ++alt) {
        USB_INTERFACE_DESCRIPTOR descriptor{};
        if (!WinUsb_QueryInterfaceSettings(stream_, alt, &descriptor)) break;
        for (UCHAR e = 0; e < descriptor.bNumEndpoints; ++e) {
            WINUSB_PIPE_INFORMATION_EX pipe{};
            if (!WinUsb_QueryPipeEx(stream_, alt, e, &pipe) || pipe.PipeId != config_.streamEndpoint) continue;
            if (pipe.MaximumBytesPerInterval >= payloadSize && pipe.MaximumBytesPerInterval < bestBytes) {
                bestBytes = pipe.MaximumBytesPerInterval;
                chosen = alt;
            }
        }
    }
    if (chosen == 0) throw std::runtime_error("No alternate setting has enough bandwidth");
    if (!WinUsb_SetCurrentAlternateSetting(stream_, chosen)) ThrowLastError("WinUsb_SetCurrentAlternateSetting");

    const size_t expected = format.mjpeg ? 0 : static_cast<size_t>(format.width) * format.height * 2;
    assembler_.Reset(expected, format.mjpeg ? kMaxMjpegFrame : expected, std::move(callback));
    delivered_ = dropped_ = 0;
    lost_ = false;
    running_ = true;
    thread_ = std::thread(&UvcDevice::StreamLoop, this);
}

void UvcDevice::Stop() {
    if (running_.exchange(false)) WinUsb_AbortPipe(stream_, config_.streamEndpoint);
    if (thread_.joinable()) thread_.join();
    // Always back to alternate setting 0, even if the stream thread already
    // ended on an error: it releases the bandwidth and the camera turns its LED off.
    WinUsb_SetCurrentAlternateSetting(stream_, 0);
}

void UvcDevice::StreamLoop() {
    const uint8_t endpoint = config_.streamEndpoint;
    UCHAR alt = 0;
    WinUsb_GetCurrentAlternateSetting(stream_, &alt);
    WINUSB_PIPE_INFORMATION_EX pipe{};
    for (UCHAR e = 0; WinUsb_QueryPipeEx(stream_, alt, e, &pipe); ++e) {
        if (pipe.PipeId == endpoint) break;
    }
    const ULONG packetSize = pipe.MaximumBytesPerInterval;
    const ULONG transferSize = packetSize * kPacketsPerTransfer;

    std::vector<uint8_t> buffer(static_cast<size_t>(transferSize) * kTransfers);
    WINUSB_ISOCH_BUFFER_HANDLE isoch = nullptr;
    if (transferSize == 0 ||
        !WinUsb_RegisterIsochBuffer(stream_, endpoint, buffer.data(), static_cast<ULONG>(buffer.size()), &isoch)) {
        lost_ = DeviceGone(GetLastError());
        running_ = false;
        return;
    }

    std::vector<USBD_ISO_PACKET_DESCRIPTOR> packets(kPacketsPerTransfer * kTransfers);
    OVERLAPPED overlapped[kTransfers]{};
    for (auto& o : overlapped) o.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    auto submit = [&](int slot, bool continueStream) {
        ResetEvent(overlapped[slot].hEvent);
        BOOL ok = WinUsb_ReadIsochPipeAsap(isoch, slot * transferSize, transferSize, continueStream,
                                           kPacketsPerTransfer, &packets[slot * kPacketsPerTransfer],
                                           &overlapped[slot]);
        return ok || GetLastError() == ERROR_IO_PENDING;
    };

    bool submitted[kTransfers]{};
    for (int slot = 0; slot < kTransfers && running_; ++slot) submitted[slot] = submit(slot, slot > 0);

    int failures = 0;
    for (int slot = 0; running_; slot = (slot + 1) % kTransfers) {
        if (!submitted[slot]) {
            submitted[slot] = submit(slot, FALSE);  // restart the stream after an error
            if (submitted[slot]) {
                failures = 0;
            } else if (DeviceGone(GetLastError()) || ++failures > kMaxSubmitFailures) {
                lost_ = true;  // unplugged: let the owner reopen the camera later
                break;
            } else {
                Sleep(1);
            }
            continue;
        }
        ULONG done = 0;
        BOOL ok = WinUsb_GetOverlappedResult(stream_, &overlapped[slot], &done, TRUE);
        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        submitted[slot] = false;
        if (!running_) break;
        if (ok) {
            const uint8_t* base = buffer.data() + static_cast<size_t>(slot) * transferSize;
            for (ULONG k = 0; k < kPacketsPerTransfer; ++k) {
                const auto& packet = packets[slot * kPacketsPerTransfer + k];
                if (packet.Status == 0 && packet.Length > 0 && packet.Offset + packet.Length <= transferSize) {
                    assembler_.OnPayload(base + packet.Offset, packet.Length);
                }
            }
            delivered_ = assembler_.Delivered();
            dropped_ = assembler_.Dropped();
        } else if (DeviceGone(error)) {
            lost_ = true;
            break;
        } else {
            assembler_.MarkError();  // lost data: the frame in progress is incomplete
        }
        submitted[slot] = submit(slot, ok != FALSE);
    }
    running_ = false;

    // A transfer may have been resubmitted after Stop() aborted the pipe: abort again
    // from this thread so every pending transfer completes before the buffer goes away.
    WinUsb_AbortPipe(stream_, endpoint);
    for (int slot = 0; slot < kTransfers; ++slot) {
        if (submitted[slot]) {
            ULONG ignored = 0;
            WinUsb_GetOverlappedResult(stream_, &overlapped[slot], &ignored, TRUE);
        }
        CloseHandle(overlapped[slot].hEvent);
    }
    WinUsb_UnregisterIsochBuffer(isoch);
}

void UvcDevice::PanTiltRelative(int16_t pan, int16_t tilt) {
    std::vector<uint8_t> data(4);
    memcpy(&data[0], &pan, 2);
    memcpy(&data[2], &tilt, 2);
    Control(kRequestOutInterface, kSetCur, kPanTiltRelative << 8, static_cast<uint16_t>(motorUnit_ << 8), control_,
            data);
}

void UvcDevice::PanTiltReset(bool pan, bool tilt) {
    std::vector<uint8_t> data{static_cast<uint8_t>((pan ? 1 : 0) | (tilt ? 2 : 0))};
    if (data[0] != 0) {
        Control(kRequestOutInterface, kSetCur, kPanTiltReset << 8, static_cast<uint16_t>(motorUnit_ << 8), control_,
                data);
    }
}

void UvcDevice::SetFocus(uint8_t value) {
    std::vector<uint8_t> data(6, 0);
    data[0] = value;
    data[5] = 1;  // as the camera reports it (GET_CUR ends with 01)
    Control(kRequestOutInterface, kSetCur, kFocus << 8, static_cast<uint16_t>(motorUnit_ << 8), control_, data);
}

uint8_t UvcDevice::Focus() {
    const auto data = GetControl(kGetCur, motorUnit_, kFocus, 6);
    return data.empty() ? 0 : data[0];
}

std::vector<uint8_t> UvcDevice::GetControl(uint8_t request, uint8_t unit, uint8_t selector, uint16_t length) {
    std::vector<uint8_t> data(length);
    Control(kRequestInInterface, request, static_cast<uint16_t>(selector << 8), static_cast<uint16_t>(unit << 8),
            control_, data);
    return data;
}

void UvcDevice::SetControl(uint8_t unit, uint8_t selector, std::vector<uint8_t> data) {
    Control(kRequestOutInterface, kSetCur, static_cast<uint16_t>(selector << 8), static_cast<uint16_t>(unit << 8),
            control_, data);
}

void UvcDevice::SetConstantFrameRate(bool constant) {
    constexpr uint8_t kAutoExposurePriority = 0x03;  // CT_AE_PRIORITY_CONTROL
    std::vector<uint8_t> data{static_cast<uint8_t>(constant ? 0 : 1)};
    Control(kRequestOutInterface, kSetCur, kAutoExposurePriority << 8,
            static_cast<uint16_t>(config_.cameraTerminal << 8), control_, data);
}

}  // namespace tourelle
