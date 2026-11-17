#include "MotorService.h"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <thread>

namespace tourelle {
namespace {

constexpr auto kRecenterTime = std::chrono::milliseconds(5000);  // sweep to the stops and back
constexpr auto kIdleExit = std::chrono::seconds(10);            // worker exits after this long without work
constexpr size_t kMaxQueue = 64;

HRESULT HResultFromException() {
    try {
        throw;
    } catch (const std::system_error& e) {
        return HRESULT_FROM_WIN32(static_cast<DWORD>(e.code().value()));
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

}  // namespace

// CameraHub ----------------------------------------------------------------------

CameraHub& CameraHub::Get() {
    static CameraHub hub;
    return hub;
}

void CameraHub::Attach(UvcDevice* streaming) {
    std::lock_guard guard(lock_);
    streaming_ = streaming;
}

void CameraHub::Detach(UvcDevice* streaming) {
    std::lock_guard guard(lock_);
    if (streaming_ == streaming) streaming_ = nullptr;
}

HRESULT CameraHub::With(const std::function<void(UvcDevice&)>& action) {
    std::lock_guard guard(lock_);
    try {
        if (streaming_ && !streaming_->Lost()) {
            action(*streaming_);
        } else {
            auto temporary = UvcDevice::Open();  // nobody streams: open the camera just for this
            action(*temporary);
        }
        return S_OK;
    } catch (...) {
        return HResultFromException();
    }
}

// MotorService -------------------------------------------------------------------

MotorService& MotorService::Get() {
    static MotorService service;
    return service;
}

core::PanTilt MotorService::Position(bool* known) {
    std::lock_guard guard(lock_);
    if (known) *known = tracker_.Known();
    return tracker_.Position();
}

int MotorService::Focus() {
    std::lock_guard guard(lock_);
    return focus_;
}

void MotorService::Enqueue(const Command& command) {
    std::lock_guard guard(lock_);
    // Merge consecutive relative moves: key auto-repeat must not pile up.
    if (command.kind == Command::Kind::Relative && !queue_.empty() && queue_.back().kind == Command::Kind::Relative) {
        queue_.back().value.pan = core::ClampPan(queue_.back().value.pan + command.value.pan) ;
        queue_.back().value.tilt = core::ClampTilt(queue_.back().value.tilt + command.value.tilt);
    } else if (queue_.size() < kMaxQueue) {
        queue_.push_back(command);
    }
    if (!threadRunning_) {
        // The thread holds a reference on this DLL so COM cannot unload it mid-command;
        // it releases it with FreeLibraryAndExitThread when the queue stays empty.
        HMODULE self = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&ThreadMain),
                               &self)) {
            HANDLE thread = CreateThread(nullptr, 0, &ThreadMain, self, 0, nullptr);
            if (thread) {
                CloseHandle(thread);
                threadRunning_ = true;
            } else {
                FreeLibrary(self);
            }
        }
    }
    signal_.notify_one();
}

DWORD WINAPI MotorService::ThreadMain(LPVOID module) {
    Get().Loop();
    FreeLibraryAndExitThread(static_cast<HMODULE>(module), 0);
}

void MotorService::Loop() {
    for (;;) {
        Command command;
        {
            std::unique_lock guard(lock_);
            if (!signal_.wait_for(guard, kIdleExit, [this] { return !queue_.empty(); })) {
                threadRunning_ = false;  // decided under the lock: Enqueue starts a new thread if needed
                return;
            }
            command = queue_.front();
            queue_.pop_front();
        }
        Execute(command);
    }
}

void MotorService::Execute(const Command& command) {
    bool recentered = false;
    HRESULT hr = CameraHub::Get().With([&](UvcDevice& camera) {
        std::lock_guard guard(lock_);
        switch (command.kind) {
            case Command::Kind::Relative: {
                const core::PanTilt delta = tracker_.Relative(command.value);
                if (delta.pan || delta.tilt) {
                    camera.PanTiltRelative(core::ToMotorValue(delta.pan), core::ToMotorValue(delta.tilt));
                }
                break;
            }
            case Command::Kind::Absolute:
                if (tracker_.Known()) {
                    const core::PanTilt delta = tracker_.MoveTo(command.value);
                    if (delta.pan || delta.tilt) {
                        camera.PanTiltRelative(core::ToMotorValue(delta.pan), core::ToMotorValue(delta.tilt));
                    }
                    break;
                }
                [[fallthrough]];  // unknown position: recenter first, then move
            case Command::Kind::Reset:
                camera.PanTiltReset();
                tracker_.Recentered();
                recentered = true;
                break;
            case Command::Kind::Focus:
                focus_ = std::clamp(command.focus, core::kFocusMin, core::kFocusMax);
                camera.SetFocus(static_cast<uint8_t>(focus_));
                break;
        }
    });
    if (FAILED(hr)) {
        std::lock_guard guard(lock_);
        tracker_.Forget();  // the move may not have happened: stop trusting the estimate
        return;
    }
    if (!recentered) return;
    std::this_thread::sleep_for(kRecenterTime);  // let the sweep finish before the next move
    if (command.kind == Command::Kind::Absolute) Execute(command);  // the position is known now
}

}  // namespace tourelle
