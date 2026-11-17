#pragma once
// Xbox controller input through XInput, loaded on demand (no link dependency).

#include <windows.h>

#include <cstdint>

namespace tourelle::app {

struct GamepadState {
    bool connected = false;
    float leftX = 0;  // -1 (left) .. 1 (right), dead zone removed
    float leftY = 0;  // -1 (down) .. 1 (up), dead zone removed
    uint16_t buttons = 0;  // XINPUT_GAMEPAD_* bits
};

class Gamepad {
public:
    Gamepad();
    ~Gamepad();
    Gamepad(const Gamepad&) = delete;
    Gamepad& operator=(const Gamepad&) = delete;

    bool Available() const { return getState_ != nullptr; }
    // Reads the first connected controller.
    GamepadState Read();

private:
    using GetStateFn = DWORD(WINAPI*)(DWORD, void*);
    HMODULE module_ = nullptr;
    GetStateFn getState_ = nullptr;
};

}  // namespace tourelle::app
