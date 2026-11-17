#pragma once
// Camera access and motor commands shared by every media source of the DLL.
//
// The Frame Server creates a media source per session and clients may shut
// theirs down right after sending a command, while a recenter takes seconds:
// commands therefore run on a DLL-wide worker thread, independent of sources.

#include <windows.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

#include "Motor.h"
#include "UvcDevice.h"

namespace tourelle {

// Gives access to the camera: the one a source is streaming from, or a
// temporary handle while nobody streams.
class CameraHub {
public:
    static CameraHub& Get();

    void Attach(UvcDevice* streaming);
    void Detach(UvcDevice* streaming);  // waits for any action using it to finish

    // Runs action with the camera. Exceptions become HRESULTs.
    HRESULT With(const std::function<void(UvcDevice&)>& action);

private:
    std::mutex lock_;
    UvcDevice* streaming_ = nullptr;
};

class MotorService {
public:
    struct Command {
        enum class Kind { Relative, Reset, Absolute, Focus } kind = Kind::Relative;
        core::PanTilt value{};
        int focus = 0;
    };

    static MotorService& Get();

    void Enqueue(const Command& command);
    core::PanTilt Position(bool* known);
    int Focus();

private:
    static DWORD WINAPI ThreadMain(LPVOID);
    void Loop();
    void Execute(const Command& command);

    std::mutex lock_;  // queue, thread state, tracker, focus
    std::condition_variable signal_;
    std::deque<Command> queue_;
    bool threadRunning_ = false;
    core::PositionTracker tracker_;
    int focus_ = 0;
};

}  // namespace tourelle
