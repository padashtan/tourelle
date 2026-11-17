#pragma once
// UVC configuration descriptor parsing. Pure code, no Windows dependency, so
// it is unit tested on captured and malformed descriptors.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace tourelle::core {

struct FrameFormat {
    uint8_t formatIndex = 0;  // bFormatIndex
    uint8_t frameIndex = 0;   // bFrameIndex
    bool mjpeg = false;       // false: YUY2
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> intervals;  // 100 ns units, fastest first
};

using Guid = std::array<uint8_t, 16>;  // byte order as in the descriptor

struct ExtensionUnit {
    uint8_t id = 0;
    Guid guid{};
    bool hidden = false;  // declared in a vendor (0x41) descriptor that class drivers ignore
};

struct UvcConfiguration {
    uint16_t bcdUvc = 0x0100;
    uint8_t cameraTerminal = 1;   // bTerminalID of the camera input terminal
    uint8_t processingUnit = 0;   // bUnitID of the processing unit, 0 if none
    uint32_t processingControls = 0;  // bmControls of the processing unit
    uint8_t streamInterface = 1;
    uint8_t streamEndpoint = 0x81;
    std::vector<FrameFormat> formats;
    std::vector<ExtensionUnit> units;

    std::optional<uint8_t> FindUnit(const Guid& guid) const;
};

// Logitech motor control unit {63610682-5070-49AB-B8CC-B3855E8D2256}, descriptor byte order.
inline constexpr Guid kLogitechMotorUnit = {0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xAB, 0x49,
                                            0xB8, 0xCC, 0xB3, 0x85, 0x5E, 0x8D, 0x22, 0x56};

// Parses a USB configuration descriptor. Never reads outside [data, data + size):
// malformed input yields a partial result, not undefined behavior.
UvcConfiguration ParseConfiguration(const uint8_t* data, size_t size);

}  // namespace tourelle::core
