// Tourelle.exe: live preview of the "Tourelle Camera" virtual camera, pan/tilt
// controls, and one-click install/uninstall. Run with --install or --uninstall
// it performs the administrator half of the setup (it starts itself that way
// through UAC).

#include <windows.h>
#include <commctrl.h>
#include <dbt.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Setup.h"
#include "TourelleClient.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

using Microsoft::WRL::ComPtr;

namespace {

enum ControlId : int { IdUp = 100, IdDown, IdLeft, IdRight, IdCenter, IdStep, IdInstall, IdUninstall };
constexpr UINT WM_APP_FRAME = WM_APP + 1;
constexpr UINT WM_APP_SETUP_DONE = WM_APP + 2;
constexpr UINT_PTR kFpsTimer = 1;
constexpr UINT_PTR kRefreshTimer = 2;  // re-check the setup after a device change settles
constexpr int kStepDegrees[] = {2, 5, 10, 20};
constexpr int kDefaultStep = 1;  // 5 degrees
constexpr int kUnitsPerDegree = 64;
constexpr DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

struct App {
    HWND window = nullptr, video = nullptr, status = nullptr, setupText = nullptr, step = nullptr;
    HWND install = nullptr, uninstall = nullptr;
    HWND pad[5]{};
    HFONT font = nullptr, titleFont = nullptr, symbolFont = nullptr, smallFont = nullptr;
    int dpi = 96;

    std::mutex lock;  // guards frame, width, height, message, source
    std::vector<BYTE> frame;  // top-down BGRA
    UINT width = 0, height = 0;
    std::wstring message = L"Starting…";
    ComPtr<IUnknown> source;  // live media source, also used for motor commands

    std::thread videoThread;
    std::atomic<bool> running{false};
    std::atomic<int> frames{0};
    int fps = 0;
    std::chrono::steady_clock::time_point lastMove{};

    tourelle::setup::Status setup;
    bool busy = false;
} g;

int Scale(int value) { return MulDiv(value, g.dpi, 96); }

void SetStatus(const std::wstring& text) { SetWindowTextW(g.status, text.c_str()); }

// Video ----------------------------------------------------------------------

void SetVideoMessage(const wchar_t* text) {
    {
        std::lock_guard guard(g.lock);
        g.message = text;
        g.frame.clear();
    }
    PostMessageW(g.window, WM_APP_FRAME, 0, 0);
}

void Pause(int milliseconds) {
    for (int waited = 0; g.running && waited < milliseconds; waited += 100) Sleep(100);
}

void CopyFrame(IMFSample* sample, UINT width, UINT height) {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->GetBufferByIndex(0, &buffer))) return;
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    std::vector<BYTE> pixels(rowBytes * height);

    ComPtr<IMF2DBuffer> buffer2d;
    BYTE* scanline0 = nullptr;
    LONG pitch = 0;
    if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scanline0, &pitch))) {
        for (UINT y = 0; y < height; ++y) memcpy(&pixels[y * rowBytes], scanline0 + static_cast<ptrdiff_t>(y) * pitch, rowBytes);
        buffer2d->Unlock2D();
    } else {
        BYTE* data = nullptr;
        DWORD maxLength = 0, length = 0;
        if (FAILED(buffer->Lock(&data, &maxLength, &length))) return;
        if (length >= pixels.size()) memcpy(pixels.data(), data, pixels.size());
        buffer->Unlock();
    }
    std::lock_guard guard(g.lock);
    g.frame.swap(pixels);
    g.width = width;
    g.height = height;
}

void VideoLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (g.running) {
        ComPtr<IMFMediaSource> source;
        if (FAILED(tourelle::client::FindSource(&source))) {
            SetVideoMessage(L"Tourelle Camera not found.");
            Pause(1000);
            continue;
        }

        ComPtr<IMFAttributes> attributes;
        ComPtr<IMFSourceReader> reader;
        ComPtr<IMFMediaType> type, actual;
        UINT32 width = 0, height = 0;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source.Get(), attributes.Get(), &reader);
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&type);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(kVideoStream, nullptr, type.Get());
        if (SUCCEEDED(hr)) hr = reader->GetCurrentMediaType(kVideoStream, &actual);
        if (SUCCEEDED(hr)) hr = MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &width, &height);
        if (FAILED(hr)) {
            source->Shutdown();
            SetVideoMessage(L"The camera does not start. Is it plugged in?");
            Pause(2000);
            continue;
        }

        {
            std::lock_guard guard(g.lock);
            source.As(&g.source);
        }
        while (g.running) {
            DWORD stream = 0, flags = 0;
            LONGLONG time = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(kVideoStream, 0, &stream, &flags, &time, &sample);
            if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) break;
            if (!sample) continue;
            CopyFrame(sample.Get(), width, height);
            ++g.frames;
            PostMessageW(g.window, WM_APP_FRAME, 0, 0);
        }
        {
            std::lock_guard guard(g.lock);
            g.source.Reset();
        }
        reader.Reset();
        source->Shutdown();
        if (g.running) {
            SetVideoMessage(L"Video interrupted, reconnecting…");
            Pause(1000);
        }
    }
    CoUninitialize();
}

void StartVideo() {
    if (g.running) return;
    g.running = true;
    g.videoThread = std::thread(VideoLoop);
}

void StopVideo() {
    if (!g.running) return;
    g.running = false;
    ComPtr<IMFMediaSource> source;
    {
        std::lock_guard guard(g.lock);
        if (g.source) g.source.As(&source);
    }
    if (source) source->Shutdown();  // unblocks ReadSample
    if (g.videoThread.joinable()) g.videoThread.join();
}

// Motor ----------------------------------------------------------------------

void Move(int panSteps, int tiltSteps) {
    // Key auto-repeat fires about 30 times a second: keep the motor queue sane.
    const auto now = std::chrono::steady_clock::now();
    if (now - g.lastMove < std::chrono::milliseconds(120)) return;
    g.lastMove = now;

    ComPtr<IUnknown> source;
    {
        std::lock_guard guard(g.lock);
        source = g.source;
    }
    if (!source) {
        SetStatus(L"No live video: the head cannot be moved.");
        return;
    }
    const int units = kStepDegrees[ComboBox_GetCurSel(g.step)] * kUnitsPerDegree;
    HRESULT hr = (panSteps == 0 && tiltSteps == 0)
                     ? tourelle::client::PanTiltReset(source.Get())
                     : tourelle::client::PanTiltRelative(source.Get(), static_cast<int16_t>(panSteps * units),
                                                         static_cast<int16_t>(tiltSteps * units));
    if (FAILED(hr)) {
        wchar_t text[96];
        swprintf_s(text, L"Motor command failed (0x%08X).", static_cast<unsigned>(hr));
        SetStatus(text);
    }
}

bool HandleKey(WPARAM key) {
    if (GetFocus() == g.step) return false;  // let the drop-down use its arrows
    switch (key) {
        case VK_LEFT: Move(-1, 0); return true;
        case VK_RIGHT: Move(1, 0); return true;
        case VK_UP: Move(0, 1); return true;
        case VK_DOWN: Move(0, -1); return true;
        case VK_HOME: Move(0, 0); return true;
        default: return false;
    }
}

// Setup ----------------------------------------------------------------------

void RefreshSetup() {
    g.setup = tourelle::setup::Query();
    std::wstring text;
    text += g.setup.cameraPresent ? L"✔ Camera connected\n" : L"✖ Camera not connected\n";
    text += g.setup.winusbBound ? L"✔ Driver: Tourelle (WinUSB)\n" : L"✖ Driver: Windows default\n";
    text += g.setup.sourceInstalled ? L"✔ Camera source installed\n" : L"✖ Camera source not installed\n";
    text += g.setup.virtualCameraListed ? L"✔ \"Tourelle Camera\" available" : L"✖ \"Tourelle Camera\" not available";
    SetWindowTextW(g.setupText, text.c_str());
    EnableWindow(g.install, !g.busy && !g.setup.Ready());
    EnableWindow(g.uninstall, !g.busy && g.setup.AnythingInstalled());
    for (HWND button : g.pad) EnableWindow(button, g.setup.Ready());

    if (g.setup.Ready()) {
        StartVideo();
    } else {
        StopVideo();
        SetVideoMessage(g.setup.cameraPresent ? L"Click “Install” to set up Tourelle."
                                              : L"Connect the camera, then click “Install”.");
    }
}

void StartSetup(bool install) {
    if (install && !g.setup.cameraPresent) {
        MessageBoxW(g.window, L"Connect the camera first.", L"Tourelle", MB_ICONINFORMATION);
        return;
    }
    if (!install && MessageBoxW(g.window,
                                L"Uninstall Tourelle and give the camera back to the Windows driver?\n\n"
                                L"The motors will not be controllable any more.",
                                L"Tourelle", MB_ICONQUESTION | MB_YESNO) != IDYES) {
        return;
    }
    g.busy = true;
    EnableWindow(g.install, FALSE);
    EnableWindow(g.uninstall, FALSE);
    SetStatus(L"Waiting for administrator approval…");
    if (!install) {
        StopVideo();  // release the camera before its driver changes
        tourelle::client::RemoveVirtualCamera();
    }
    std::thread([install] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const int code = tourelle::setup::RunElevated(install ? L"--install" : L"--uninstall");
        CoUninitialize();
        PostMessageW(g.window, WM_APP_SETUP_DONE, install, static_cast<LPARAM>(code));
    }).detach();
}

void SetupDone(bool install, int code) {
    g.busy = false;
    if (code == -1) {
        SetStatus(L"Cancelled.");
    } else if (code != 0) {
        SetStatus(L"Setup failed.");
    } else if (install) {
        HRESULT hr = tourelle::client::EnableVirtualCamera();  // per user: not from the elevated process
        if (FAILED(hr)) {
            wchar_t text[96];
            swprintf_s(text, L"Virtual camera registration failed (0x%08X).", static_cast<unsigned>(hr));
            SetStatus(text);
        } else {
            SetStatus(L"Installed. Select “Tourelle Camera” in your apps.");
        }
    } else {
        SetStatus(L"Uninstalled. The camera uses the Windows driver again.");
    }
    RefreshSetup();
}

// Painting -------------------------------------------------------------------

void PaintVideo(HDC target, const RECT& area) {
    const int width = area.right, height = area.bottom;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, g.font);
    {
        std::lock_guard guard(g.lock);
        if (!g.frame.empty() && g.width && g.height) {
            // Letterbox, keep the aspect ratio.
            int drawWidth = width, drawHeight = MulDiv(width, static_cast<int>(g.height), static_cast<int>(g.width));
            if (drawHeight > height) {
                drawHeight = height;
                drawWidth = MulDiv(height, static_cast<int>(g.width), static_cast<int>(g.height));
            }
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(info.bmiHeader);
            info.bmiHeader.biWidth = static_cast<LONG>(g.width);
            info.bmiHeader.biHeight = -static_cast<LONG>(g.height);  // top-down
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, nullptr);
            StretchDIBits(dc, (width - drawWidth) / 2, (height - drawHeight) / 2, drawWidth, drawHeight, 0, 0,
                          static_cast<int>(g.width), static_cast<int>(g.height), g.frame.data(), &info, DIB_RGB_COLORS,
                          SRCCOPY);

            wchar_t badge[32];
            swprintf_s(badge, L"● LIVE  %d fps", g.fps);
            RECT badgeRect{Scale(12), Scale(10), width, Scale(40)};
            SetTextColor(dc, RGB(0, 0, 0));
            OffsetRect(&badgeRect, 1, 1);
            DrawTextW(dc, badge, -1, &badgeRect, DT_LEFT | DT_SINGLELINE);
            OffsetRect(&badgeRect, -1, -1);
            SetTextColor(dc, RGB(60, 207, 145));
            DrawTextW(dc, badge, -1, &badgeRect, DT_LEFT | DT_SINGLELINE);
        } else {
            RECT textRect = area;
            SetTextColor(dc, RGB(200, 208, 216));
            DrawTextW(dc, g.message.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

LRESULT CALLBACK VideoProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            RECT area;
            GetClientRect(window, &area);
            PaintVideo(dc, area);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_LBUTTONDOWN:
            SetFocus(GetParent(window));  // arrow keys go back to the camera
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

// Main window ----------------------------------------------------------------

HWND Child(const wchar_t* className, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id,
           HFONT font) {
    HWND child = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style, Scale(x), Scale(y), Scale(width),
                                 Scale(height), g.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr,
                                 nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return child;
}

void CreateControls() {
    g.video = CreateWindowExW(0, L"TourelleVideo", nullptr, WS_CHILD | WS_VISIBLE, Scale(16), Scale(16), Scale(720),
                              Scale(540), g.window, nullptr, nullptr, nullptr);

    const int x = 760;
    Child(L"STATIC", L"Tourelle", SS_LEFT, x, 12, 240, 40, 0, g.titleFont);
    Child(L"STATIC", L"Pan / tilt", SS_LEFT, x, 58, 240, 22, 0, g.smallFont);

    const int padX = x + 8, padY = 86, size = 68, gap = 8;
    g.pad[0] = Child(L"BUTTON", L"▲", BS_PUSHBUTTON, padX + size + gap, padY, size, size, IdUp, g.symbolFont);
    g.pad[1] = Child(L"BUTTON", L"◀", BS_PUSHBUTTON, padX, padY + size + gap, size, size, IdLeft, g.symbolFont);
    g.pad[2] = Child(L"BUTTON", L"⌂", BS_PUSHBUTTON, padX + size + gap, padY + size + gap, size, size, IdCenter,
                     g.symbolFont);
    g.pad[3] = Child(L"BUTTON", L"▶", BS_PUSHBUTTON, padX + 2 * (size + gap), padY + size + gap, size, size, IdRight,
                     g.symbolFont);
    g.pad[4] = Child(L"BUTTON", L"▼", BS_PUSHBUTTON, padX + size + gap, padY + 2 * (size + gap), size, size, IdDown,
                     g.symbolFont);

    Child(L"STATIC", L"Step", SS_LEFT, x, 336, 60, 24, 0, g.font);
    g.step = Child(WC_COMBOBOXW, nullptr, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, x + 64, 332, 100, 200, IdStep,
                   g.font);
    for (int degrees : kStepDegrees) ComboBox_AddString(g.step, (std::to_wstring(degrees) + L"°").c_str());
    ComboBox_SetCurSel(g.step, kDefaultStep);
    Child(L"STATIC", L"Arrow keys move the head, Home recenters it.", SS_LEFT, x, 368, 240, 40, 0, g.smallFont);

    g.setupText = Child(L"STATIC", L"", SS_LEFT, x, 420, 240, 86, 0, g.smallFont);
    g.install = Child(L"BUTTON", L"Install", BS_PUSHBUTTON | WS_TABSTOP, x, 508, 116, 32, IdInstall, g.font);
    g.uninstall = Child(L"BUTTON", L"Uninstall", BS_PUSHBUTTON | WS_TABSTOP, x + 124, 508, 116, 32, IdUninstall, g.font);

    g.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, g.window, nullptr, nullptr,
                               nullptr);
    SendMessageW(g.status, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
}

HFONT MakeFont(const wchar_t* face, int points, int weight) {
    return CreateFontW(-MulDiv(points, g.dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            g.window = window;
            CreateControls();
            SetTimer(window, kFpsTimer, 1000, nullptr);
            RefreshSetup();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IdUp: Move(0, 1); break;
                case IdDown: Move(0, -1); break;
                case IdLeft: Move(-1, 0); break;
                case IdRight: Move(1, 0); break;
                case IdCenter: Move(0, 0); break;
                case IdInstall: StartSetup(true); break;
                case IdUninstall: StartSetup(false); break;
                default: return 0;
            }
            if (HIWORD(wParam) == BN_CLICKED) SetFocus(window);  // keep arrow keys on the camera
            return 0;
        case WM_TIMER:
            if (wParam == kFpsTimer) {
                g.fps = g.frames.exchange(0);
            } else if (wParam == kRefreshTimer) {
                KillTimer(window, kRefreshTimer);
                if (!g.busy) RefreshSetup();
            }
            return 0;
        case WM_APP_FRAME:
            InvalidateRect(g.video, nullptr, FALSE);
            return 0;
        case WM_APP_SETUP_DONE:
            SetupDone(wParam != 0, static_cast<int>(lParam));
            return 0;
        case WM_DEVICECHANGE:
            if (!g.busy && wParam == DBT_DEVNODES_CHANGED) SetTimer(window, kRefreshTimer, 1500, nullptr);
            return TRUE;
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_SIZE:
            SendMessageW(g.status, WM_SIZE, 0, 0);
            return 0;
        case WM_CLOSE:
            StopVideo();
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

int RunApp(HINSTANCE instance, int show) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);

    g.dpi = static_cast<int>(GetDpiForSystem());
    g.font = MakeFont(L"Segoe UI", 10, FW_NORMAL);
    g.smallFont = MakeFont(L"Segoe UI", 9, FW_NORMAL);
    g.titleFont = MakeFont(L"Segoe UI", 20, FW_BOLD);
    g.symbolFont = MakeFont(L"Segoe UI Symbol", 18, FW_NORMAL);

    WNDCLASSEXW videoClass{sizeof(videoClass)};
    videoClass.lpfnWndProc = VideoProc;
    videoClass.hInstance = instance;
    videoClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    videoClass.lpszClassName = L"TourelleVideo";
    RegisterClassExW(&videoClass);

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.lpszClassName = L"TourelleWindow";
    RegisterClassExW(&windowClass);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT bounds{0, 0, Scale(1016), Scale(600)};
    AdjustWindowRect(&bounds, style, FALSE);
    CreateWindowExW(0, L"TourelleWindow", L"Tourelle", style, CW_USEDEFAULT, CW_USEDEFAULT,
                    bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, instance, nullptr);
    ShowWindow(g.window, show);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        if (message.message == WM_KEYDOWN && HandleKey(message.wParam)) continue;
        if (!IsDialogMessageW(g.window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    DeleteObject(g.font);
    DeleteObject(g.smallFont);
    DeleteObject(g.titleFont);
    DeleteObject(g.symbolFont);
    MFShutdown();
    CoUninitialize();
    return 0;
}

int RunAdminTask(bool install) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        if (install) {
            wchar_t self[MAX_PATH];
            GetModuleFileNameW(nullptr, self, MAX_PATH);
            std::wstring dll = self;
            dll = dll.substr(0, dll.find_last_of(L'\\') + 1) + L"TourelleCamera.dll";
            tourelle::setup::InstallElevated(dll);
        } else {
            tourelle::setup::UninstallElevated();
        }
        CoUninitialize();
        return 0;
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Tourelle setup", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int result;
    if (argc > 1 && _wcsicmp(argv[1], L"--install") == 0) {
        result = RunAdminTask(true);
    } else if (argc > 1 && _wcsicmp(argv[1], L"--uninstall") == 0) {
        result = RunAdminTask(false);
    } else {
        result = RunApp(instance, show);
    }
    LocalFree(argv);
    return result;
}
