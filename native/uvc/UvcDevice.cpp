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
constexpr uint8_t kGetCur = 0x81;
constexpr uint8_t kRequestOutInterface = 0x21;  // class, interface, host-to-device
constexpr uint8_t kRequestInInterface = 0xA1;   // class, interface, device-to-host

constexpr uint8_t kProbeControl = 0x01;
constexpr uint8_t kCommitControl = 0x02;

// Logitech motor control extension unit, hidden in a vendor descriptor.
constexpr uint8_t kMotorUnit = 9;
constexpr uint8_t kPanTiltRelative = 1;
constexpr uint8_t kPanTiltReset = 2;

// Isochronous transfer shape: high speed needs a multiple of 8 packets (1 ms).
constexpr ULONG kPacketsPerTransfer = 64;
constexpr int kTransfers = 4;

[[noreturn]] void ThrowLastError(const char* what) {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), what);
}

uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24));
}

std::wstring FirstInterfacePath() {
    GUID guid = kWinUsbInterfaceGuid;
    ULONG length = 0;
    if (CM_Get_Device_Interface_List_SizeW(&length, &guid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS ||
        length <= 1) {
        throw std::runtime_error("No camera bound to WinUSB. Run the bind tool first.");
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
    device->ParseConfiguration();
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

void UvcDevice::ParseConfiguration() {
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
    d.resize(got);

    int interfaceClass = -1, interfaceSubclass = -1;
    FrameFormat* currentFormat = nullptr;  // template for frames of the current format
    FrameFormat formatTemplate;
    for (size_t i = 0; i + 2 <= d.size() && d[i] >= 2 && i + d[i] <= d.size(); i += d[i]) {
        const uint8_t* p = &d[i];
        const uint8_t len = p[0], type = p[1];
        if (type == USB_INTERFACE_DESCRIPTOR_TYPE && len >= 9) {
            interfaceClass = p[5];
            interfaceSubclass = p[6];
            if (interfaceClass == 0x0E && interfaceSubclass == 2) streamInterface_ = p[2];
            continue;
        }
        if (type == USB_ENDPOINT_DESCRIPTOR_TYPE && interfaceClass == 0x0E && interfaceSubclass == 2 && len >= 7) {
            if ((p[3] & 0x03) == 0x01) streamEndpoint_ = p[2];  // isochronous IN
            continue;
        }
        if (type != 0x24 || len < 3 || interfaceClass != 0x0E) continue;

        const uint8_t subtype = p[2];
        if (interfaceSubclass == 1 && subtype == 0x01 && len >= 5) {
            bcdUvc_ = Le16(p + 3);  // VC_HEADER
        } else if (interfaceSubclass == 1 && subtype == 0x02 && len >= 8 && Le16(p + 4) == 0x0201) {
            cameraTerminal_ = p[3];  // VC_INPUT_TERMINAL of type ITT_CAMERA
        } else if (interfaceSubclass == 2 && (subtype == 0x04 || subtype == 0x06) && len >= 5) {
            formatTemplate = FrameFormat{};
            formatTemplate.formatIndex = p[3];
            formatTemplate.mjpeg = subtype == 0x06;
            currentFormat = &formatTemplate;
        } else if (interfaceSubclass == 2 && (subtype == 0x05 || subtype == 0x07) && len >= 26 && currentFormat) {
            FrameFormat f = *currentFormat;
            f.frameIndex = p[3];
            f.width = Le16(p + 5);
            f.height = Le16(p + 7);
            const uint8_t count = p[25];
            if (count == 0 && len >= 38) {
                f.intervals.push_back(Le32(p + 26));  // continuous: keep the minimum
            }
            for (uint8_t k = 0; k < count && 26 + 4 * (k + 1) <= len; ++k) f.intervals.push_back(Le32(p + 26 + 4 * k));
            std::sort(f.intervals.begin(), f.intervals.end());
            formats_.push_back(std::move(f));
        }
    }
    if (formats_.empty()) throw std::runtime_error("No video format found in the configuration descriptor");
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
    const size_t probeSize = bcdUvc_ >= 0x0110 ? 34 : 26;
    std::vector<uint8_t> probe(probeSize, 0);
    probe[0] = 0x01;  // bmHint: keep dwFrameInterval
    probe[2] = format.formatIndex;
    probe[3] = format.frameIndex;
    memcpy(&probe[4], &interval, 4);

    const uint16_t index = streamInterface_;
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
            if (!WinUsb_QueryPipeEx(stream_, alt, e, &pipe) || pipe.PipeId != streamEndpoint_) continue;
            if (pipe.MaximumBytesPerInterval >= payloadSize && pipe.MaximumBytesPerInterval < bestBytes) {
                bestBytes = pipe.MaximumBytesPerInterval;
                chosen = alt;
            }
        }
    }
    if (chosen == 0) throw std::runtime_error("No alternate setting has enough bandwidth");
    if (!WinUsb_SetCurrentAlternateSetting(stream_, chosen)) ThrowLastError("WinUsb_SetCurrentAlternateSetting");

    callback_ = std::move(callback);
    expectedSize_ = format.mjpeg ? 0 : static_cast<size_t>(format.width) * format.height * 2;
    frame_.clear();
    frame_.reserve(format.mjpeg ? 1 << 20 : expectedSize_);
    haveFid_ = frameError_ = false;
    delivered_ = dropped_ = 0;
    running_ = true;
    thread_ = std::thread(&UvcDevice::StreamLoop, this);
}

void UvcDevice::Stop() {
    if (running_.exchange(false)) WinUsb_AbortPipe(stream_, streamEndpoint_);
    if (thread_.joinable()) thread_.join();
    // Always back to alternate setting 0, even if the stream thread already
    // ended on an error: it releases the bandwidth and the camera turns its LED off.
    WinUsb_SetCurrentAlternateSetting(stream_, 0);
}

void UvcDevice::StreamLoop() {
    UCHAR alt = 0;
    WinUsb_GetCurrentAlternateSetting(stream_, &alt);
    WINUSB_PIPE_INFORMATION_EX pipe{};
    for (UCHAR e = 0; WinUsb_QueryPipeEx(stream_, alt, e, &pipe); ++e) {
        if (pipe.PipeId == streamEndpoint_) break;
    }
    const ULONG packetSize = pipe.MaximumBytesPerInterval;
    const ULONG transferSize = packetSize * kPacketsPerTransfer;

    std::vector<uint8_t> buffer(static_cast<size_t>(transferSize) * kTransfers);
    WINUSB_ISOCH_BUFFER_HANDLE isoch = nullptr;
    if (!WinUsb_RegisterIsochBuffer(stream_, streamEndpoint_, buffer.data(), static_cast<ULONG>(buffer.size()),
                                    &isoch)) {
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

    for (int slot = 0; running_; slot = (slot + 1) % kTransfers) {
        if (!submitted[slot]) {
            submitted[slot] = submit(slot, FALSE);  // restart the stream after an error
            if (!submitted[slot]) Sleep(1);
            continue;
        }
        ULONG done = 0;
        BOOL ok = WinUsb_GetOverlappedResult(stream_, &overlapped[slot], &done, TRUE);
        submitted[slot] = false;
        if (!running_) break;
        if (ok) {
            const uint8_t* base = buffer.data() + static_cast<size_t>(slot) * transferSize;
            for (ULONG k = 0; k < kPacketsPerTransfer; ++k) {
                const auto& packet = packets[slot * kPacketsPerTransfer + k];
                if (packet.Status == 0 && packet.Length > 0) OnPayload(base + packet.Offset, packet.Length);
            }
        } else {
            frameError_ = true;  // lost data: the frame in progress is incomplete
        }
        submitted[slot] = submit(slot, ok != FALSE);
    }

    // A transfer may have been resubmitted after Stop() aborted the pipe: abort again
    // from this thread so every pending transfer completes before the buffer goes away.
    WinUsb_AbortPipe(stream_, streamEndpoint_);
    for (int slot = 0; slot < kTransfers; ++slot) {
        if (submitted[slot]) {
            ULONG ignored = 0;
            WinUsb_GetOverlappedResult(stream_, &overlapped[slot], &ignored, TRUE);
        }
        CloseHandle(overlapped[slot].hEvent);
    }
    WinUsb_UnregisterIsochBuffer(isoch);
}

void UvcDevice::OnPayload(const uint8_t* data, size_t size) {
    // UVC payload header: bHeaderLength, bmHeaderInfo (bit0 FID, bit1 EOF, bit6 ERR).
    if (size < 2 || data[0] < 2 || data[0] > size) return;
    const uint8_t headerLength = data[0];
    const uint8_t info = data[1];
    const bool fid = (info & 0x01) != 0;

    if (haveFid_ && fid != lastFid_ && !frame_.empty()) EmitFrame();  // new frame began without EOF
    haveFid_ = true;
    lastFid_ = fid;
    if (info & 0x40) frameError_ = true;

    frame_.insert(frame_.end(), data + headerLength, data + size);
    if (info & 0x02) EmitFrame();
}

void UvcDevice::EmitFrame() {
    const bool complete = !frameError_ && !frame_.empty() && (expectedSize_ == 0 || frame_.size() == expectedSize_);
    if (complete && callback_) {
        callback_(frame_.data(), frame_.size());
        ++delivered_;
    } else if (!frame_.empty()) {
        ++dropped_;
    }
    frame_.clear();
    frameError_ = false;
}

void UvcDevice::PanTiltRelative(int16_t pan, int16_t tilt) {
    std::vector<uint8_t> data(4);
    memcpy(&data[0], &pan, 2);
    memcpy(&data[2], &tilt, 2);
    Control(kRequestOutInterface, kSetCur, kPanTiltRelative << 8, kMotorUnit << 8, control_, data);
}

void UvcDevice::SetConstantFrameRate(bool constant) {
    constexpr uint8_t kAutoExposurePriority = 0x03;  // CT_AE_PRIORITY_CONTROL
    std::vector<uint8_t> data{static_cast<uint8_t>(constant ? 0 : 1)};
    Control(kRequestOutInterface, kSetCur, kAutoExposurePriority << 8, static_cast<uint16_t>(cameraTerminal_ << 8),
            control_, data);
}

void UvcDevice::PanTiltReset(bool pan, bool tilt) {
    std::vector<uint8_t> data{static_cast<uint8_t>((pan ? 1 : 0) | (tilt ? 2 : 0))};
    if (data[0] != 0) Control(kRequestOutInterface, kSetCur, kPanTiltReset << 8, kMotorUnit << 8, control_, data);
}

}  // namespace tourelle
