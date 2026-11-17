#include "UvcDescriptors.h"

#include <algorithm>
#include <cstring>

namespace tourelle::core {
namespace {

constexpr uint8_t kInterfaceDescriptor = 0x04;
constexpr uint8_t kEndpointDescriptor = 0x05;
constexpr uint8_t kClassSpecificInterface = 0x24;
constexpr uint8_t kLogitechHiddenUnit = 0x41;  // vendor type Logitech uses to hide extension units
constexpr uint8_t kVideoClass = 0x0E;
constexpr uint8_t kVideoControl = 0x01;
constexpr uint8_t kVideoStreaming = 0x02;

uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

ExtensionUnit ReadUnit(const uint8_t* p, bool hidden) {
    ExtensionUnit unit;
    unit.id = p[3];
    std::memcpy(unit.guid.data(), p + 4, unit.guid.size());
    unit.hidden = hidden;
    return unit;
}

}  // namespace

std::optional<uint8_t> UvcConfiguration::FindUnit(const Guid& guid) const {
    for (const auto& unit : units) {
        if (unit.guid == guid) return unit.id;
    }
    return std::nullopt;
}

UvcConfiguration ParseConfiguration(const uint8_t* data, size_t size) {
    UvcConfiguration config;
    int interfaceClass = -1, interfaceSubclass = -1;
    bool haveFormat = false;
    FrameFormat formatTemplate;

    for (size_t i = 0; data && i + 2 <= size;) {
        const uint8_t* p = data + i;
        const uint8_t length = p[0];
        if (length < 2 || length > size - i) break;  // corrupt length: stop, never overrun
        const uint8_t type = p[1];
        i += length;

        if (type == kInterfaceDescriptor) {
            if (length < 9) continue;
            interfaceClass = p[5];
            interfaceSubclass = p[6];
            if (interfaceClass == kVideoClass && interfaceSubclass == kVideoStreaming) config.streamInterface = p[2];
            continue;
        }
        if (interfaceClass != kVideoClass) continue;

        if (type == kEndpointDescriptor) {
            if (length >= 7 && interfaceSubclass == kVideoStreaming && (p[3] & 0x03) == 0x01 && (p[2] & 0x80)) {
                config.streamEndpoint = p[2];  // isochronous IN
            }
            continue;
        }
        if (type == kLogitechHiddenUnit) {
            if (interfaceSubclass == kVideoControl && length >= 21) config.units.push_back(ReadUnit(p, true));
            continue;
        }
        if (type != kClassSpecificInterface || length < 3) continue;

        const uint8_t subtype = p[2];
        if (interfaceSubclass == kVideoControl) {
            switch (subtype) {
                case 0x01:  // VC_HEADER
                    if (length >= 5) config.bcdUvc = Le16(p + 3);
                    break;
                case 0x02:  // VC_INPUT_TERMINAL
                    if (length >= 8 && Le16(p + 4) == 0x0201) config.cameraTerminal = p[3];
                    break;
                case 0x05:  // VC_PROCESSING_UNIT
                    if (length >= 8) {
                        config.processingUnit = p[3];
                        const uint8_t controlSize = p[7];
                        uint32_t controls = 0;
                        for (uint8_t b = 0; b < controlSize && b < 4 && 8u + b < length; ++b) {
                            controls |= static_cast<uint32_t>(p[8 + b]) << (8 * b);
                        }
                        config.processingControls = controls;
                    }
                    break;
                case 0x06:  // VC_EXTENSION_UNIT
                    if (length >= 21) config.units.push_back(ReadUnit(p, false));
                    break;
                default:
                    break;
            }
            continue;
        }
        if (interfaceSubclass != kVideoStreaming) continue;

        if ((subtype == 0x04 || subtype == 0x06) && length >= 5) {  // VS_FORMAT_UNCOMPRESSED / VS_FORMAT_MJPEG
            formatTemplate = FrameFormat{};
            formatTemplate.formatIndex = p[3];
            formatTemplate.mjpeg = subtype == 0x06;
            haveFormat = true;
        } else if ((subtype == 0x05 || subtype == 0x07) && length >= 26 && haveFormat) {  // VS_FRAME_*
            FrameFormat frame = formatTemplate;
            frame.frameIndex = p[3];
            frame.width = Le16(p + 5);
            frame.height = Le16(p + 7);
            const uint8_t count = p[25];
            if (count == 0) {
                if (length >= 30) frame.intervals.push_back(Le32(p + 26));  // continuous: keep the minimum
            } else {
                for (size_t k = 0; k < count && 26 + 4 * (k + 1) <= length; ++k) {
                    frame.intervals.push_back(Le32(p + 26 + 4 * k));
                }
            }
            std::sort(frame.intervals.begin(), frame.intervals.end());
            frame.intervals.erase(std::remove(frame.intervals.begin(), frame.intervals.end(), 0u),
                                  frame.intervals.end());
            if (frame.width && frame.height && !frame.intervals.empty()) config.formats.push_back(std::move(frame));
        }
    }
    return config;
}

}  // namespace tourelle::core
