#include "Gamepad.h"

#include <Xinput.h>

#include <cmath>

namespace tourelle::app {
namespace {

float Axis(SHORT raw, SHORT deadZone) {
    const float value = static_cast<float>(raw);
    const float zone = static_cast<float>(deadZone);
    if (std::fabs(value) <= zone) return 0.0f;
    const float scaled = (std::fabs(value) - zone) / (32767.0f - zone);
    return std::copysign(scaled > 1.0f ? 1.0f : scaled, value);
}

}  // namespace

Gamepad::Gamepad() {
    // xinput1_4 ships with Windows 10 and 11; load it from System32 only.
    module_ = LoadLibraryExW(L"xinput1_4.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module_) getState_ = reinterpret_cast<GetStateFn>(GetProcAddress(module_, "XInputGetState"));
}

Gamepad::~Gamepad() {
    if (module_) FreeLibrary(module_);
}

GamepadState Gamepad::Read() {
    GamepadState state;
    if (!getState_) return state;
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE raw{};
        if (getState_(index, &raw) != ERROR_SUCCESS) continue;
        state.connected = true;
        state.leftX = Axis(raw.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        state.leftY = Axis(raw.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        state.buttons = raw.Gamepad.wButtons;
        break;
    }
    return state;
}

}  // namespace tourelle::app
