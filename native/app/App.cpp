// Tourelle.exe: live preview of the "Tourelle Camera" virtual camera, pan/tilt
// controls, one-click install/uninstall and an About box. Run with --install or
// --uninstall it performs the administrator half of the setup (it starts itself
// that way through UAC).

#include <windows.h>
#include <commctrl.h>
#include <dbt.h>
#include <dwmapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <wrl/implements.h>
// GDI+ needs min/max, which NOMINMAX removes.
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Setup.h"
#include "Strings.h"
#include "TourelleClient.h"
#include "Version.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::ComPtr;

namespace {

// Look and feel -----------------------------------------------------------------

namespace theme {
constexpr COLORREF Background = RGB(15, 20, 25);
constexpr COLORREF Panel = RGB(24, 32, 41);
constexpr COLORREF Line = RGB(42, 53, 66);
constexpr COLORREF Text = RGB(232, 237, 242);
constexpr COLORREF Muted = RGB(147, 161, 176);
constexpr COLORREF Accent = RGB(245, 165, 36);
constexpr COLORREF AccentPressed = RGB(214, 140, 20);
constexpr COLORREF Ok = RGB(60, 207, 145);
constexpr COLORREF Bad = RGB(255, 107, 107);
constexpr COLORREF Button = RGB(36, 48, 60);
constexpr COLORREF ButtonHover = RGB(50, 65, 80);
}  // namespace theme

// Segoe Fluent Icons (Windows 11) / Segoe MDL2 Assets glyphs.
constexpr wchar_t kGlyphUp[] = L"\uE70E";
constexpr wchar_t kGlyphDown[] = L"\uE70D";
constexpr wchar_t kGlyphLeft[] = L"\uE76B";
constexpr wchar_t kGlyphRight[] = L"\uE76C";
constexpr wchar_t kGlyphHome[] = L"\uE80F";
constexpr wchar_t kGlyphMenu[] = L"\uE700";

enum ControlId : int {
    IdUp = 100, IdDown, IdLeft, IdRight, IdCenter,
    IdStep0 = 110,  // ..IdStep0 + 3
    IdInstall = 120, IdUninstall, IdMenu,
    IdMenuAbout = 200, IdMenuLicense, IdMenuNotices, IdMenuProject, IdMenuDocs, IdMenuRecenter, IdMenuInstall,
    IdMenuUninstall, IdMenuQuit, IdMenuShow, IdMenuAutostart, IdMenuInvertPan, IdMenuInvertTilt,
    IdMenuLanguage0 = 250,  // ..IdMenuLanguage0 + language count
    IdTab0 = 300,  // ..IdTab0 + 2
    IdProject = 310, IdClose,
};

enum class ButtonKind { Pad, Pill, Primary, Secondary, Icon };

constexpr UINT WM_APP_FRAME = WM_APP + 1;
constexpr UINT WM_APP_SETUP_DONE = WM_APP + 2;
constexpr UINT WM_APP_TRAY = WM_APP + 3;  // notification area icon events
constexpr UINT WM_APP_SHOW = WM_APP + 4;  // sent by a second instance
constexpr UINT kTrayIconId = 1;
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Tourelle";
constexpr wchar_t kSettingsKey[] = L"Software\\Tourelle";
constexpr int kLanguageCount = static_cast<int>(std::size(tourelle::ui::kLanguages));
constexpr UINT_PTR kFpsTimer = 1;
constexpr UINT_PTR kRefreshTimer = 2;  // re-check the setup after a device change settles
constexpr int kStepDegrees[] = {2, 5, 10, 20};
constexpr int kUnitsPerDegree = 64;
constexpr int kTiltUp = -1;  // the camera's positive tilt points down
constexpr DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr wchar_t kDocsUrl[] = TOURELLE_PROJECT_URL L"/blob/main/docs/how-it-works.md";

// Layout, in 96-dpi pixels.
constexpr int kClientWidth = 1040, kClientHeight = 668;
constexpr RECT kVideoRect{24, 84, 744, 624};
constexpr RECT kMoveCard{768, 84, 1016, 448};
constexpr RECT kSetupCard{768, 464, 1016, 624};

struct App {
    HINSTANCE instance = nullptr;
    HWND window = nullptr, video = nullptr, menu = nullptr, install = nullptr, uninstall = nullptr;
    HWND pad[5]{};
    HWND steps[4]{};
    HWND hover = nullptr;
    int dpi = 96;
    int stepIndex = 1;  // 5 degrees

    // Settings, kept in HKCU\Software\Tourelle
    int language = 0;  // index in tourelle::ui::kLanguages
    bool invertPan = false;
    bool invertTilt = false;
    std::wstring iconFont = L"Segoe Fluent Icons";
    std::wstring statusText;
    ULONG_PTR gdiplus = 0;

    // About box
    HWND about = nullptr, aboutText = nullptr, tabs[3]{};
    int aboutTab = 0;
    HFONT textFont = nullptr, monoFont = nullptr;
    HBRUSH panelBrush = nullptr;

    // Video
    std::mutex lock;          // guards frame, width, height, message, source
    std::vector<BYTE> frame;  // top-down BGRA
    UINT width = 0, height = 0;
    std::wstring message;
    ComPtr<IUnknown> source;  // live media source, also used for motor commands
    std::thread videoThread;
    HANDLE stopVideo = nullptr;  // signaled to end the current video thread
    std::atomic<int> frames{0};
    int fps = 0;
    std::chrono::steady_clock::time_point lastMove{};

    tourelle::setup::Status setup;
    bool busy = false;

    // Notification area
    UINT taskbarCreated = 0;  // Explorer restarted: the icon must be added again
    bool quitting = false;
    bool trayHintShown = false;
} g;

const tourelle::ui::Texts& T() { return *tourelle::ui::kLanguages[g.language]; }

// Settings -------------------------------------------------------------------

DWORD ReadSetting(const wchar_t* name, DWORD fallback) {
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) ==
                   ERROR_SUCCESS
               ? value
               : fallback;
}

void WriteSetting(const wchar_t* name, DWORD value) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &value, sizeof(value));
}

// First run: follow the Windows display language when it is one we have.
int DefaultLanguage() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_FRENCH: return 1;
        case LANG_SPANISH: return 2;
        case LANG_POLISH: return 3;
        default: return 0;
    }
}

int LanguageFromCode(const wchar_t* code) {
    for (int i = 0; i < kLanguageCount; ++i) {
        if (_wcsicmp(tourelle::ui::kLanguages[i]->code, code) == 0) return i;
    }
    return -1;
}

void LoadSettings() {
    wchar_t code[16]{};
    DWORD size = sizeof(code);
    const int saved = RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"Language", RRF_RT_REG_SZ, nullptr, code,
                                   &size) == ERROR_SUCCESS
                          ? LanguageFromCode(code)
                          : -1;
    g.language = saved >= 0 ? saved : DefaultLanguage();
    g.invertPan = ReadSetting(L"InvertPan", 0) != 0;
    g.invertTilt = ReadSetting(L"InvertTilt", 0) != 0;
    g.stepIndex = static_cast<int>(std::min<DWORD>(ReadSetting(L"Step", 1), 3));
}

void SaveLanguage() {
    const wchar_t* code = T().code;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"Language", REG_SZ, code,
                    static_cast<DWORD>((wcslen(code) + 1) * sizeof(wchar_t)));
}

int Scale(int value) { return MulDiv(value, g.dpi, 96); }
float ScaleF(float value) { return value * static_cast<float>(g.dpi) / 96.0f; }
RECT Scale(const RECT& r) { return RECT{Scale(r.left), Scale(r.top), Scale(r.right), Scale(r.bottom)}; }

Gdiplus::Color Color(COLORREF c, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

void RoundedPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, float radius) {
    const float d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}

void FillRounded(Gdiplus::Graphics& graphics, const Gdiplus::RectF& r, float radius, COLORREF fill, BYTE alpha = 255,
                 const COLORREF* border = nullptr) {
    Gdiplus::GraphicsPath path;
    RoundedPath(path, r, radius);
    Gdiplus::SolidBrush brush(Color(fill, alpha));
    graphics.FillPath(&brush, &path);
    if (border) {
        Gdiplus::Pen pen(Color(*border, alpha), ScaleF(1));
        graphics.DrawPath(&pen, &path);
    }
}

void DrawText(Gdiplus::Graphics& graphics, const std::wstring& text, const wchar_t* family, float size, int style,
              const Gdiplus::RectF& r, COLORREF color, Gdiplus::StringAlignment horizontal = Gdiplus::StringAlignmentNear,
              Gdiplus::StringAlignment vertical = Gdiplus::StringAlignmentNear, BYTE alpha = 255) {
    Gdiplus::Font font(family, ScaleF(size), style, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(Color(color, alpha));
    Gdiplus::StringFormat format;
    format.SetAlignment(horizontal);
    format.SetLineAlignment(vertical);
    graphics.DrawString(text.c_str(), -1, &font, r, &format, &brush);
}

Gdiplus::RectF RectF(const RECT& r) {
    return Gdiplus::RectF(static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right - r.left),
                          static_cast<float>(r.bottom - r.top));
}

void PrepareGraphics(Gdiplus::Graphics& graphics) {
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
}

void UseDarkTitleBar(HWND window) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    COLORREF caption = theme::Background;
    DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
}

// Dark popup menus. uxtheme exports this by ordinal only; it is skipped when absent.
void UseDarkMenus() {
    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;
    using SetPreferredAppMode = int(WINAPI*)(int);
    using FlushMenuThemes = void(WINAPI*)();
    auto setMode = reinterpret_cast<SetPreferredAppMode>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    auto flush = reinterpret_cast<FlushMenuThemes>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (setMode) setMode(2);  // force dark
    if (flush) flush();
}

void SetStatus(const std::wstring& text) {
    g.statusText = text;
    RECT line{0, Scale(632), Scale(kClientWidth), Scale(kClientHeight)};
    InvalidateRect(g.window, &line, FALSE);
}

// Video ----------------------------------------------------------------------

void SetVideoMessage(const wchar_t* text) {
    {
        std::lock_guard guard(g.lock);
        g.message = text;
        g.frame.clear();
    }
    PostMessageW(g.window, WM_APP_FRAME, 0, 0);
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
        for (UINT y = 0; y < height; ++y) {
            memcpy(&pixels[y * rowBytes], scanline0 + static_cast<ptrdiff_t>(y) * pitch, rowBytes);
        }
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

// Asynchronous source reader callback: a pending read can always be cancelled
// with Flush, unlike a blocking ReadSample.
class ReaderCallback
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IMFSourceReaderCallback> {
public:
    ReaderCallback()
        : failed_(CreateEventW(nullptr, TRUE, FALSE, nullptr)), flushed_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
    ~ReaderCallback() override {
        CloseHandle(failed_);
        CloseHandle(flushed_);
    }

    void Attach(IMFSourceReader* reader, UINT32 width, UINT32 height) {
        reader_ = reader;  // weak: the video thread keeps it alive until Detach
        width_ = width;
        height_ = height;
    }
    void Detach() { active_ = false; }
    HANDLE Failed() const { return failed_; }
    HANDLE Flushed() const { return flushed_; }

    IFACEMETHODIMP OnReadSample(HRESULT status, DWORD, DWORD flags, LONGLONG, IMFSample* sample) override {
        if (FAILED(status) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
            SetEvent(failed_);
            return S_OK;
        }
        if (sample) {
            CopyFrame(sample, width_, height_);
            ++g.frames;
            PostMessageW(g.window, WM_APP_FRAME, 0, 0);
        }
        if (active_ && FAILED(reader_->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr))) {
            SetEvent(failed_);
        }
        return S_OK;
    }
    IFACEMETHODIMP OnFlush(DWORD) override {
        SetEvent(flushed_);
        return S_OK;
    }
    IFACEMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }

private:
    HANDLE failed_, flushed_;
    IMFSourceReader* reader_ = nullptr;
    UINT32 width_ = 0, height_ = 0;
    std::atomic<bool> active_{true};
};

void VideoLoop(HANDLE stop) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto stopped = [stop](DWORD milliseconds) { return WaitForSingleObject(stop, milliseconds) == WAIT_OBJECT_0; };
    while (!stopped(0)) {
        ComPtr<IMFMediaSource> source;
        if (FAILED(tourelle::client::FindSource(&source))) {
            SetVideoMessage(T().notFound);
            if (stopped(1000)) break;
            continue;
        }

        auto callback = Microsoft::WRL::Make<ReaderCallback>();
        ComPtr<IMFAttributes> attributes;
        ComPtr<IMFSourceReader> reader;
        ComPtr<IMFMediaType> type, actual;
        UINT32 width = 0, height = 0;
        HRESULT hr = callback ? MFCreateAttributes(&attributes, 2) : E_OUTOFMEMORY;
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.Get());
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source.Get(), attributes.Get(), &reader);
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&type);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(kVideoStream, nullptr, type.Get());
        if (SUCCEEDED(hr)) hr = reader->GetCurrentMediaType(kVideoStream, &actual);
        if (SUCCEEDED(hr)) hr = MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &width, &height);
        if (SUCCEEDED(hr)) {
            callback->Attach(reader.Get(), width, height);
            hr = reader->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr);
        }
        if (FAILED(hr)) {
            reader.Reset();
            source->Shutdown();
            SetVideoMessage(T().noStart);
            if (stopped(2000)) break;
            continue;
        }

        ComPtr<IUnknown> mine;
        source.As(&mine);
        {
            std::lock_guard guard(g.lock);
            g.source = mine;
        }
        HANDLE events[] = {stop, callback->Failed()};
        const DWORD reason = WaitForMultipleObjects(2, events, FALSE, INFINITE);

        // Tear down on this thread, in order: no new reads, cancel the pending one, release.
        callback->Detach();
        if (SUCCEEDED(reader->Flush(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS)))) WaitForSingleObject(callback->Flushed(), 2000);
        {
            std::lock_guard guard(g.lock);
            if (g.source == mine) g.source.Reset();
        }
        reader.Reset();
        source->Shutdown();
        if (reason == WAIT_OBJECT_0) break;
        SetVideoMessage(T().interrupted);
        if (stopped(1000)) break;
    }
    CoUninitialize();
}

void StartVideo() {
    if (g.videoThread.joinable()) return;
    g.stopVideo = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g.videoThread = std::thread(VideoLoop, g.stopVideo);
}

void StopVideo() {
    if (!g.videoThread.joinable()) return;
    SetEvent(g.stopVideo);
    // Never let a camera that stopped answering freeze the window.
    if (WaitForSingleObject(g.videoThread.native_handle(), 3000) == WAIT_OBJECT_0) {
        g.videoThread.join();
        CloseHandle(g.stopVideo);
    } else {
        g.videoThread.detach();  // it still owns its stop event and exits on its own
    }
    g.stopVideo = nullptr;
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
    ComPtr<IMFMediaSource> temporary;  // no preview running (window hidden): open the camera just for this
    if (!source) {
        if (FAILED(tourelle::client::FindSource(&temporary))) {
            SetStatus(T().cannotMove);
            return;
        }
        temporary.As(&source);
    }
    const int units = kStepDegrees[g.stepIndex] * kUnitsPerDegree;
    HRESULT hr = (panSteps == 0 && tiltSteps == 0)
                     ? tourelle::client::PanTiltReset(source.Get())
                     : tourelle::client::PanTiltRelative(
                           source.Get(), static_cast<int16_t>(panSteps * units * (g.invertPan ? -1 : 1)),
                           static_cast<int16_t>(tiltSteps * units * kTiltUp * (g.invertTilt ? -1 : 1)));
    if (temporary) temporary->Shutdown();
    if (FAILED(hr)) {
        wchar_t text[96];
        swprintf_s(text, T().motorFailed, static_cast<unsigned>(hr));
        SetStatus(text);
    }
}

void SelectStep(int index) {
    g.stepIndex = index;
    WriteSetting(L"Step", static_cast<DWORD>(index));
    for (HWND button : g.steps) InvalidateRect(button, nullptr, FALSE);
}

void ShowAbout(int tab);
void CloseAbout();
void SetAutostart(bool enabled);
void SetLanguage(int language);

bool HandleKey(WPARAM key) {
    if (g.about && GetActiveWindow() == g.about) {
        if (key == VK_ESCAPE) {
            CloseAbout();
            return true;
        }
        return false;
    }
    if (GetActiveWindow() != g.window || !g.setup.Ready()) return false;
    switch (key) {
        case VK_LEFT: Move(-1, 0); return true;
        case VK_RIGHT: Move(1, 0); return true;
        case VK_UP: Move(0, 1); return true;
        case VK_DOWN: Move(0, -1); return true;
        case VK_HOME: Move(0, 0); return true;
        case '1': case '2': case '3': case '4': SelectStep(static_cast<int>(key - '1')); return true;
        default: return false;
    }
}

// Setup ----------------------------------------------------------------------

void RefreshSetup() {
    g.setup = tourelle::setup::Query();
    EnableWindow(g.install, !g.busy && !g.setup.Ready());
    EnableWindow(g.uninstall, !g.busy && g.setup.AnythingInstalled());
    for (HWND button : g.pad) EnableWindow(button, g.setup.Ready());
    RECT card = Scale(kSetupCard);
    InvalidateRect(g.window, &card, FALSE);

    if (g.setup.Ready()) {
        if (IsWindowVisible(g.window)) StartVideo();  // no preview while hidden in the notification area
    } else {
        StopVideo();
        SetVideoMessage(g.setup.cameraPresent ? T().clickInstall : T().connectThenInstall);
    }
}

void StartSetup(bool install) {
    if (g.busy) return;
    if (install && !g.setup.cameraPresent) {
        MessageBoxW(g.window, T().connectFirst, L"Tourelle", MB_ICONINFORMATION);
        return;
    }
    if (!install && MessageBoxW(g.window,
                                T().confirmUninstall,
                                L"Tourelle", MB_ICONQUESTION | MB_YESNO) != IDYES) {
        return;
    }
    g.busy = true;
    EnableWindow(g.install, FALSE);
    EnableWindow(g.uninstall, FALSE);
    SetStatus(T().waitingAdmin);
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
        SetStatus(T().cancelled);
    } else if (code != 0) {
        SetStatus(T().setupFailed);
    } else if (install) {
        HRESULT hr = tourelle::client::EnableVirtualCamera();  // per user: not from the elevated process
        if (FAILED(hr)) {
            wchar_t text[96];
            swprintf_s(text, T().registrationFailed, static_cast<unsigned>(hr));
            SetStatus(text);
        } else {
            SetStatus(T().installed);
        }
    } else {
        SetAutostart(false);
        SetStatus(T().uninstalled);
    }
    RefreshSetup();
}

void OpenUrl(const wchar_t* url) { ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); }

// Start with Windows ----------------------------------------------------------

bool IsAutostart() {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) ==
           ERROR_SUCCESS;
}

void SetAutostart(bool enabled) {
    if (!enabled) {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
        return;
    }
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring command = L"\"" + std::wstring(self) + L"\" --tray";
    RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, command.c_str(),
                    static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
}

// Notification area -----------------------------------------------------------

void AddTrayIcon() {
    NOTIFYICONDATAW icon{sizeof(icon)};
    icon.hWnd = g.window;
    icon.uID = kTrayIconId;
    icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    icon.uCallbackMessage = WM_APP_TRAY;
    icon.hIcon = static_cast<HICON>(LoadImageW(g.instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wcscpy_s(icon.szTip, T().trayTip);
    Shell_NotifyIconW(NIM_ADD, &icon);
    icon.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &icon);
}

void UpdateTrayTip() {
    NOTIFYICONDATAW icon{sizeof(icon)};
    icon.hWnd = g.window;
    icon.uID = kTrayIconId;
    icon.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcscpy_s(icon.szTip, T().trayTip);
    Shell_NotifyIconW(NIM_MODIFY, &icon);
}

void RemoveTrayIcon() {
    NOTIFYICONDATAW icon{sizeof(icon)};
    icon.hWnd = g.window;
    icon.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &icon);
}

void TrayNotification(const wchar_t* title, const wchar_t* text) {
    NOTIFYICONDATAW icon{sizeof(icon)};
    icon.hWnd = g.window;
    icon.uID = kTrayIconId;
    icon.uFlags = NIF_INFO;
    icon.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(icon.szInfoTitle, title);
    wcscpy_s(icon.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &icon);
}

void ShowMain() {
    if (IsIconic(g.window)) ShowWindow(g.window, SW_RESTORE);
    ShowWindow(g.window, SW_SHOW);
    SetForegroundWindow(g.window);
    if (g.setup.Ready()) StartVideo();
}

void HideMain() {
    StopVideo();  // free the camera and the CPU while nobody looks at the preview
    ShowWindow(g.window, SW_HIDE);
    if (!g.trayHintShown) {
        g.trayHintShown = true;
        TrayNotification(T().trayHintTitle, T().trayHintText);
    }
}

void Quit() {
    g.quitting = true;
    PostMessageW(g.window, WM_CLOSE, 0, 0);
}

void ShowTrayMenu(int x, int y) {
    HMENU menu = CreatePopupMenu();
    const bool visible = IsWindowVisible(g.window) != FALSE;
    AppendMenuW(menu, MF_STRING, IdMenuShow, visible ? T().menuHide : T().menuShow);
    SetMenuDefaultItem(menu, IdMenuShow, FALSE);
    AppendMenuW(menu, MF_STRING | (g.setup.Ready() ? 0 : MF_GRAYED), IdMenuRecenter, T().menuRecenter);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (IsAutostart() ? MF_CHECKED : 0), IdMenuAutostart, T().menuAutostart);
    AppendMenuW(menu, MF_STRING, IdMenuAbout, T().menuAbout);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdMenuQuit, T().menuQuit);

    SetForegroundWindow(g.window);  // required so the menu closes when clicking elsewhere
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, x, y, 0, g.window,
                                       nullptr);
    PostMessageW(g.window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    switch (command) {
        case IdMenuShow: visible ? HideMain() : ShowMain(); break;
        case IdMenuRecenter: g.lastMove = {}; Move(0, 0); break;
        case IdMenuAutostart: SetAutostart(!IsAutostart()); break;
        case IdMenuAbout: ShowMain(); ShowAbout(0); break;
        case IdMenuQuit: Quit(); break;
        default: break;
    }
}

// Owner-drawn buttons ---------------------------------------------------------

ButtonKind KindOf(int id) {
    if (id >= IdUp && id <= IdCenter) return ButtonKind::Pad;
    if ((id >= IdStep0 && id < IdStep0 + 4) || (id >= IdTab0 && id < IdTab0 + 3)) return ButtonKind::Pill;
    if (id == IdInstall || id == IdClose) return ButtonKind::Primary;
    if (id == IdMenu) return ButtonKind::Icon;
    return ButtonKind::Secondary;
}

const wchar_t* GlyphOf(int id) {
    switch (id) {
        case IdUp: return kGlyphUp;
        case IdDown: return kGlyphDown;
        case IdLeft: return kGlyphLeft;
        case IdRight: return kGlyphRight;
        case IdCenter: return kGlyphHome;
        case IdMenu: return kGlyphMenu;
        default: return nullptr;
    }
}

bool IsSelected(int id) {
    if (id >= IdStep0 && id < IdStep0 + 4) return id - IdStep0 == g.stepIndex;
    if (id >= IdTab0 && id < IdTab0 + 3) return id - IdTab0 == g.aboutTab;
    return false;
}

// Buttons sit on a card (Panel) or directly on the window background.
COLORREF BackdropOf(int id) {
    return (id >= IdUp && id < IdMenu) ? theme::Panel : theme::Background;
}

void DrawButton(const DRAWITEMSTRUCT& item) {
    const int id = static_cast<int>(item.CtlID);
    const int width = item.rcItem.right - item.rcItem.left, height = item.rcItem.bottom - item.rcItem.top;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool hover = item.hwndItem == g.hover && !disabled;
    const BYTE alpha = disabled ? 90 : 255;

    HDC dc = CreateCompatibleDC(item.hDC);
    HBITMAP bitmap = CreateCompatibleBitmap(item.hDC, width, height);
    HGDIOBJ old = SelectObject(dc, bitmap);
    {
        Gdiplus::Graphics graphics(dc);
        PrepareGraphics(graphics);
        graphics.Clear(Color(BackdropOf(id)));
        const Gdiplus::RectF box(0.5f, 0.5f, width - 1.0f, height - 1.0f);
        wchar_t label[64]{};
        GetWindowTextW(item.hwndItem, label, ARRAYSIZE(label));

        switch (KindOf(id)) {
            case ButtonKind::Pad: {
                const COLORREF fill = pressed ? theme::Accent : hover ? theme::ButtonHover : theme::Button;
                Gdiplus::SolidBrush brush(Color(fill, alpha));
                graphics.FillEllipse(&brush, box);
                Gdiplus::Pen pen(Color(theme::Line, alpha), ScaleF(1));
                graphics.DrawEllipse(&pen, box);
                const COLORREF glyph = pressed ? theme::Background : id == IdCenter ? theme::Accent : theme::Text;
                DrawText(graphics, GlyphOf(id), g.iconFont.c_str(), 20, Gdiplus::FontStyleRegular, box, glyph,
                         Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter, alpha);
                break;
            }
            case ButtonKind::Pill: {
                const bool selected = IsSelected(id);
                const COLORREF fill = selected ? theme::Accent : hover || pressed ? theme::ButtonHover : theme::Button;
                FillRounded(graphics, box, box.Height / 2, fill, alpha);
                DrawText(graphics, label, L"Segoe UI", 13, selected ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                         box, selected ? theme::Background : hover ? theme::Text : theme::Muted,
                         Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter, alpha);
                break;
            }
            case ButtonKind::Primary: {
                const COLORREF fill = pressed ? theme::AccentPressed : theme::Accent;
                FillRounded(graphics, box, ScaleF(8), fill, hover ? 230 : alpha);
                DrawText(graphics, label, L"Segoe UI", 13, Gdiplus::FontStyleBold, box, theme::Background,
                         Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter, alpha);
                break;
            }
            case ButtonKind::Secondary: {
                const COLORREF border = theme::Line;
                FillRounded(graphics, box, ScaleF(8), hover || pressed ? theme::ButtonHover : BackdropOf(id), alpha,
                            &border);
                DrawText(graphics, label, L"Segoe UI", 13, Gdiplus::FontStyleRegular, box, theme::Text,
                         Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter, alpha);
                break;
            }
            case ButtonKind::Icon: {
                if (hover || pressed) {
                    Gdiplus::SolidBrush brush(Color(theme::Button));
                    graphics.FillEllipse(&brush, box);
                }
                DrawText(graphics, GlyphOf(id), g.iconFont.c_str(), 17, Gdiplus::FontStyleRegular, box, theme::Text,
                         Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter);
                break;
            }
        }
    }
    BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

// Tracks the hovered button so it can light up.
LRESULT CALLBACK HoverProc(HWND button, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    switch (message) {
        case WM_MOUSEMOVE:
            if (g.hover != button) {
                HWND previous = g.hover;
                g.hover = button;
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, button, 0};
                TrackMouseEvent(&track);
                if (previous) InvalidateRect(previous, nullptr, FALSE);
                InvalidateRect(button, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            if (g.hover == button) {
                g.hover = nullptr;
                InvalidateRect(button, nullptr, FALSE);
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        default:
            break;
    }
    return DefSubclassProc(button, message, wParam, lParam);
}

HWND MakeButton(HWND parent, int id, const wchar_t* text, int x, int y, int width, int height) {
    HWND button = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, Scale(x), Scale(y),
                                  Scale(width), Scale(height), parent,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.instance, nullptr);
    SetWindowSubclass(button, HoverProc, 0, 0);
    return button;
}

bool IsClick(WPARAM wParam) { return HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == BN_DOUBLECLICKED; }

// Main window painting --------------------------------------------------------

void PaintMain(HDC target, const RECT& area) {
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, area.right, area.bottom);
    HGDIOBJ old = SelectObject(dc, bitmap);
    {
        Gdiplus::Graphics graphics(dc);
        PrepareGraphics(graphics);
        graphics.Clear(Color(theme::Background));
        const auto R = [](float x, float y, float w, float h) {
            return Gdiplus::RectF(ScaleF(x), ScaleF(y), ScaleF(w), ScaleF(h));
        };
        const auto Near = Gdiplus::StringAlignmentNear;
        const auto Far = Gdiplus::StringAlignmentFar;

        // Header
        DrawText(graphics, L"Tourelle", L"Segoe UI", 30, Gdiplus::FontStyleBold, R(22, 14, 300, 44), theme::Text);
        DrawText(graphics, T().subtitle, L"Segoe UI", 14, Gdiplus::FontStyleRegular,
                 R(24, 54, 600, 22), theme::Muted);

        // Video frame
        const COLORREF line = theme::Line;
        FillRounded(graphics, RectF(Scale(RECT{23, 83, 745, 625})), ScaleF(4), theme::Panel, 255, &line);

        // Move card
        FillRounded(graphics, RectF(Scale(kMoveCard)), ScaleF(14), theme::Panel, 255, &line);
        DrawText(graphics, T().move, L"Segoe UI", 12, Gdiplus::FontStyleBold, R(788, 98, 200, 18), theme::Accent);
        DrawText(graphics, T().step, L"Segoe UI", 12, Gdiplus::FontStyleBold, R(788, 348, 200, 18), theme::Accent);
        DrawText(graphics, T().hint, L"Segoe UI", 12,
                 Gdiplus::FontStyleRegular, R(788, 404, 220, 40), theme::Muted);

        // Setup card
        FillRounded(graphics, RectF(Scale(kSetupCard)), ScaleF(14), theme::Panel, 255, &line);
        DrawText(graphics, T().setup, L"Segoe UI", 12, Gdiplus::FontStyleBold, R(788, 478, 200, 18), theme::Accent);
        const struct {
            bool ok;
            const wchar_t* yes;
            const wchar_t* no;
        } lines[] = {
            {g.setup.cameraPresent, T().cameraYes, T().cameraNo},
            {g.setup.winusbBound, T().driverYes, T().driverNo},
            {g.setup.sourceInstalled, T().sourceYes, T().sourceNo},
            {g.setup.virtualCameraListed, T().virtualCameraYes, T().virtualCameraNo},
        };
        for (int i = 0; i < 4; ++i) {
            const float y = 502.0f + 19.0f * static_cast<float>(i);
            Gdiplus::SolidBrush dot(Color(lines[i].ok ? theme::Ok : theme::Bad));
            graphics.FillEllipse(&dot, R(790, y + 5, 8, 8));
            DrawText(graphics, lines[i].ok ? lines[i].yes : lines[i].no, L"Segoe UI", 13, Gdiplus::FontStyleRegular,
                     R(806, y, 200, 19), lines[i].ok ? theme::Text : theme::Muted);
        }

        // Status line
        DrawText(graphics, g.statusText, L"Segoe UI", 13, Gdiplus::FontStyleRegular, R(24, 636, 760, 22), theme::Muted);
        DrawText(graphics, L"v" TOURELLE_VERSION_WIDE L" · MIT", L"Segoe UI", 13, Gdiplus::FontStyleRegular,
                 R(816, 636, 200, 22), theme::Muted, Far, Near);
    }
    BitBlt(target, 0, 0, area.right, area.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void PaintVideo(HDC target, const RECT& area) {
    const int width = area.right, height = area.bottom;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ old = SelectObject(dc, bitmap);
    {
        std::lock_guard guard(g.lock);
        HBRUSH background = CreateSolidBrush(RGB(10, 14, 18));
        FillRect(dc, &area, background);
        DeleteObject(background);
        const bool live = !g.frame.empty() && g.width && g.height;
        if (live) {
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
        }
        Gdiplus::Graphics graphics(dc);
        PrepareGraphics(graphics);
        if (live) {
            const Gdiplus::RectF pill(ScaleF(14), ScaleF(14), ScaleF(118), ScaleF(30));
            FillRounded(graphics, pill, pill.Height / 2, RGB(0, 0, 0), 150);
            Gdiplus::SolidBrush dot(Color(theme::Ok));
            graphics.FillEllipse(&dot, Gdiplus::RectF(pill.X + ScaleF(12), pill.Y + ScaleF(11), ScaleF(8), ScaleF(8)));
            DrawText(graphics, L"LIVE  " + std::to_wstring(g.fps) + L" fps", L"Segoe UI", 13, Gdiplus::FontStyleBold,
                     Gdiplus::RectF(pill.X + ScaleF(26), pill.Y, pill.Width - ScaleF(28), pill.Height), theme::Text,
                     Gdiplus::StringAlignmentNear, Gdiplus::StringAlignmentCenter);
        } else {
            DrawText(graphics, g.message, L"Segoe UI", 16, Gdiplus::FontStyleRegular, RectF(area), theme::Muted,
                     Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter);
        }
    }
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
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
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

// Menu -----------------------------------------------------------------------

void ShowMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IdMenuAbout, T().menuAbout);
    AppendMenuW(menu, MF_STRING, IdMenuLicense, T().menuLicense);
    AppendMenuW(menu, MF_STRING, IdMenuNotices, T().menuNotices);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdMenuProject, T().menuProject);
    AppendMenuW(menu, MF_STRING, IdMenuDocs, T().menuDocs);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const std::wstring recenter = std::wstring(T().menuRecenter) + L"\tHome";
    AppendMenuW(menu, MF_STRING | (g.setup.Ready() ? 0 : MF_GRAYED), IdMenuRecenter, recenter.c_str());
    AppendMenuW(menu, MF_STRING | (g.invertPan ? MF_CHECKED : 0), IdMenuInvertPan, T().menuInvertPan);
    AppendMenuW(menu, MF_STRING | (g.invertTilt ? MF_CHECKED : 0), IdMenuInvertTilt, T().menuInvertTilt);
    HMENU languages = CreatePopupMenu();
    for (int i = 0; i < kLanguageCount; ++i) {
        AppendMenuW(languages, MF_STRING | (i == g.language ? MF_CHECKED : 0), IdMenuLanguage0 + i,
                    tourelle::ui::kLanguages[i]->name);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(languages), T().menuLanguage);  // owned by menu
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (!g.busy && !g.setup.Ready() ? 0 : MF_GRAYED), IdMenuInstall, T().install);
    AppendMenuW(menu, MF_STRING | (!g.busy && g.setup.AnythingInstalled() ? 0 : MF_GRAYED), IdMenuUninstall,
                T().uninstall);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (IsAutostart() ? MF_CHECKED : 0), IdMenuAutostart, T().menuAutostart);
    AppendMenuW(menu, MF_STRING, IdMenuQuit, T().menuQuit);

    RECT button;
    GetWindowRect(g.menu, &button);
    const int command = TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, button.right,
                                       button.bottom + Scale(4), 0, g.window, nullptr);
    DestroyMenu(menu);
    switch (command) {
        case IdMenuAbout: ShowAbout(0); break;
        case IdMenuLicense: ShowAbout(1); break;
        case IdMenuNotices: ShowAbout(2); break;
        case IdMenuProject: OpenUrl(TOURELLE_PROJECT_URL); break;
        case IdMenuDocs: OpenUrl(kDocsUrl); break;
        case IdMenuRecenter: Move(0, 0); break;
        case IdMenuInstall: StartSetup(true); break;
        case IdMenuUninstall: StartSetup(false); break;
        case IdMenuAutostart: SetAutostart(!IsAutostart()); break;
        case IdMenuInvertPan:
            g.invertPan = !g.invertPan;
            WriteSetting(L"InvertPan", g.invertPan);
            break;
        case IdMenuInvertTilt:
            g.invertTilt = !g.invertTilt;
            WriteSetting(L"InvertTilt", g.invertTilt);
            break;
        case IdMenuQuit: Quit(); break;
        default:
            if (command >= IdMenuLanguage0 && command < IdMenuLanguage0 + kLanguageCount) {
                SetLanguage(command - IdMenuLanguage0);
            }
            break;
    }
}

// About box --------------------------------------------------------------------

constexpr wchar_t kLicenseText[] =
    L"MIT License\r\n"
    L"\r\n"
    L"Copyright (c) 2026 Tourelle contributors\r\n"
    L"\r\n"
    L"Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated "
    L"documentation files (the \"Software\"), to deal in the Software without restriction, including without "
    L"limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the "
    L"Software, and to permit persons to whom the Software is furnished to do so, subject to the following "
    L"conditions:\r\n"
    L"\r\n"
    L"The above copyright notice and this permission notice shall be included in all copies or substantial portions "
    L"of the Software.\r\n"
    L"\r\n"
    L"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED "
    L"TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL "
    L"THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF "
    L"CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER "
    L"DEALINGS IN THE SOFTWARE.\r\n";

constexpr RECT kAboutTextRect{24, 124, 576, 488};

void SelectAboutTab(int tab) {
    g.aboutTab = tab;
    const std::wstring license = std::wstring(T().licenseNote) + kLicenseText;
    const wchar_t* texts[] = {T().aboutText, license.c_str(), T().noticesText};
    SendMessageW(g.aboutText, WM_SETFONT, reinterpret_cast<WPARAM>(tab == 1 ? g.monoFont : g.textFont), FALSE);
    SetWindowTextW(g.aboutText, texts[tab]);
    for (HWND button : g.tabs) InvalidateRect(button, nullptr, FALSE);
}

void CloseAbout() {
    if (g.about) DestroyWindow(g.about);
}

LRESULT CALLBACK AboutProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            g.about = window;
            UseDarkTitleBar(window);
            const wchar_t* names[] = {T().tabAbout, T().tabLicense, T().tabNotices};
            for (int i = 0; i < 3; ++i) g.tabs[i] = MakeButton(window, IdTab0 + i, names[i], 24 + i * 104, 80, 96, 32);
            const RECT text = Scale(kAboutTextRect);
            g.aboutText = CreateWindowExW(0, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                          text.left + Scale(12), text.top + Scale(10), text.right - text.left - Scale(16),
                                          text.bottom - text.top - Scale(16), window, nullptr, g.instance, nullptr);
            MakeButton(window, IdProject, T().projectPage, 24, 504, 150, 36);
            MakeButton(window, IdClose, T().close, 452, 504, 124, 36);
            SelectAboutTab(static_cast<int>(reinterpret_cast<INT_PTR>(
                reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams)));
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC target = BeginPaint(window, &paint);
            RECT area;
            GetClientRect(window, &area);
            HDC dc = CreateCompatibleDC(target);
            HBITMAP bitmap = CreateCompatibleBitmap(target, area.right, area.bottom);
            HGDIOBJ old = SelectObject(dc, bitmap);
            {
                Gdiplus::Graphics graphics(dc);
                PrepareGraphics(graphics);
                graphics.Clear(Color(theme::Background));
                const COLORREF line = theme::Line;
                FillRounded(graphics, RectF(Scale(kAboutTextRect)), ScaleF(12), theme::Panel, 255, &line);
                DrawText(graphics, L"Tourelle", L"Segoe UI", 26, Gdiplus::FontStyleBold,
                         Gdiplus::RectF(ScaleF(80), ScaleF(16), ScaleF(300), ScaleF(36)), theme::Text);
                DrawText(graphics, T().versionLine, L"Segoe UI", 13,
                         Gdiplus::FontStyleRegular, Gdiplus::RectF(ScaleF(82), ScaleF(50), ScaleF(400), ScaleF(20)),
                         theme::Muted);
            }
            HICON icon = static_cast<HICON>(LoadImageW(g.instance, MAKEINTRESOURCEW(1), IMAGE_ICON, Scale(44), Scale(44), 0));
            if (icon) {
                DrawIconEx(dc, Scale(24), Scale(18), icon, Scale(44), Scale(44), 0, nullptr, DI_NORMAL);
                DestroyIcon(icon);
            }
            BitBlt(target, 0, 0, area.right, area.bottom, dc, 0, 0, SRCCOPY);
            SelectObject(dc, old);
            DeleteObject(bitmap);
            DeleteDC(dc);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_CTLCOLORSTATIC:  // read-only edit
            SetTextColor(reinterpret_cast<HDC>(wParam), theme::Text);
            SetBkColor(reinterpret_cast<HDC>(wParam), theme::Panel);
            return reinterpret_cast<LRESULT>(g.panelBrush);
        case WM_DRAWITEM:
            DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (!IsClick(wParam)) return 0;
            if (id >= IdTab0 && id < IdTab0 + 3) SelectAboutTab(id - IdTab0);
            if (id == IdProject) OpenUrl(TOURELLE_PROJECT_URL);
            if (id == IdClose) CloseAbout();
            return 0;
        }
        case WM_CLOSE:
            CloseAbout();
            return 0;
        case WM_DESTROY:
            EnableWindow(g.window, TRUE);
            SetActiveWindow(g.window);
            g.about = nullptr;
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

void ShowAbout(int tab) {
    if (g.about) {
        SelectAboutTab(tab);
        SetForegroundWindow(g.about);
        return;
    }
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT bounds{0, 0, Scale(600), Scale(560)};
    AdjustWindowRect(&bounds, style, FALSE);
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    RECT owner;
    GetWindowRect(g.window, &owner);
    EnableWindow(g.window, FALSE);  // modal
    HWND about = CreateWindowExW(0, L"TourelleAbout", T().aboutCaption, style,
                                 owner.left + (owner.right - owner.left - width) / 2,
                                 owner.top + (owner.bottom - owner.top - height) / 2, width, height, g.window, nullptr,
                                 g.instance, reinterpret_cast<LPVOID>(static_cast<INT_PTR>(tab)));
    ShowWindow(about, SW_SHOW);
}

// Main window ----------------------------------------------------------------

void CreateControls() {
    const RECT video = Scale(kVideoRect);
    g.video = CreateWindowExW(0, L"TourelleVideo", nullptr, WS_CHILD | WS_VISIBLE, video.left, video.top,
                              video.right - video.left, video.bottom - video.top, g.window, nullptr, g.instance, nullptr);
    g.menu = MakeButton(g.window, IdMenu, L"Menu", 972, 22, 44, 44);

    // D-pad, centered in the move card.
    const int size = 64, gap = 10, center = (kMoveCard.left + kMoveCard.right) / 2 - size / 2, top = 124;
    g.pad[0] = MakeButton(g.window, IdUp, L"Up", center, top, size, size);
    g.pad[1] = MakeButton(g.window, IdLeft, L"Left", center - size - gap, top + size + gap, size, size);
    g.pad[2] = MakeButton(g.window, IdCenter, L"Center", center, top + size + gap, size, size);
    g.pad[3] = MakeButton(g.window, IdRight, L"Right", center + size + gap, top + size + gap, size, size);
    g.pad[4] = MakeButton(g.window, IdDown, L"Down", center, top + 2 * (size + gap), size, size);

    for (int i = 0; i < 4; ++i) {
        g.steps[i] = MakeButton(g.window, IdStep0 + i, (std::to_wstring(kStepDegrees[i]) + L"°").c_str(), 788 + i * 54,
                                370, 48, 30);
    }
    g.install = MakeButton(g.window, IdInstall, T().install, 788, 584, 100, 30);
    g.uninstall = MakeButton(g.window, IdUninstall, T().uninstall, 896, 584, 100, 30);
}

void SetLanguage(int language) {
    g.language = language;
    SaveLanguage();
    CloseAbout();  // reopens in the new language next time
    SetWindowTextW(g.install, T().install);
    SetWindowTextW(g.uninstall, T().uninstall);
    UpdateTrayTip();
    g.statusText.clear();
    RedrawWindow(g.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    if (!g.setup.Ready()) RefreshSetup();  // the video area shows a setup message: redo it
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            g.window = window;
            UseDarkTitleBar(window);
            CreateControls();
            AddTrayIcon();
            SetTimer(window, kFpsTimer, 1000, nullptr);
            RefreshSetup();
            return 0;
        case WM_APP_TRAY:  // NOTIFYICON_VERSION_4: event in LOWORD(lParam), anchor point in wParam
            switch (LOWORD(lParam)) {
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    IsWindowVisible(window) && GetForegroundWindow() == window ? HideMain() : ShowMain();
                    break;
                case WM_CONTEXTMENU:
                    ShowTrayMenu(GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam));
                    break;
                default:
                    break;
            }
            return 0;
        case WM_APP_SHOW:
            ShowMain();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            RECT area;
            GetClientRect(window, &area);
            PaintMain(dc, area);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DRAWITEM:
            DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (!IsClick(wParam)) return 0;
            switch (id) {
                case IdUp: Move(0, 1); break;
                case IdDown: Move(0, -1); break;
                case IdLeft: Move(-1, 0); break;
                case IdRight: Move(1, 0); break;
                case IdCenter: Move(0, 0); break;
                case IdInstall: StartSetup(true); break;
                case IdUninstall: StartSetup(false); break;
                case IdMenu: ShowMenu(); break;
                default:
                    if (id >= IdStep0 && id < IdStep0 + 4) SelectStep(id - IdStep0);
                    break;
            }
            SetFocus(window);  // keep the arrow keys on the camera
            return 0;
        }
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
        case WM_CLOSE:
            CloseAbout();
            if (!g.quitting) {
                HideMain();  // the close button sends Tourelle to the notification area
                return 0;
            }
            StopVideo();
            DestroyWindow(window);
            return 0;
        case WM_QUERYENDSESSION:
            g.quitting = true;
            return TRUE;
        case WM_DESTROY:
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;
        default:
            if (message == g.taskbarCreated && message != 0) {
                AddTrayIcon();
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

void RegisterClass(const wchar_t* name, WNDPROC procedure, HBRUSH background) {
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = procedure;
    windowClass.hInstance = g.instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = background;
    windowClass.hIcon = LoadIconW(g.instance, MAKEINTRESOURCEW(1));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(g.instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    windowClass.lpszClassName = name;
    RegisterClassExW(&windowClass);
}

HFONT MakeFont(const wchar_t* face, int points) {
    return CreateFontW(-MulDiv(points, g.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

int RunApp(HINSTANCE instance, int show, bool trayOnly) {
    g.instance = instance;
    g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    LoadSettings();
    g.message = T().starting;
    g.trayHintShown = trayOnly;  // started with Windows: the user knows where it lives
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);
    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&g.gdiplus, &gdiplusInput, nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    UseDarkMenus();

    g.dpi = static_cast<int>(GetDpiForSystem());
    if (Gdiplus::FontFamily(g.iconFont.c_str()).GetLastStatus() != Gdiplus::Ok) g.iconFont = L"Segoe MDL2 Assets";
    g.textFont = MakeFont(L"Segoe UI", 10);
    g.monoFont = MakeFont(L"Consolas", 10);
    g.panelBrush = CreateSolidBrush(theme::Panel);

    RegisterClass(L"TourelleVideo", VideoProc, nullptr);
    RegisterClass(L"TourelleWindow", WindowProc, nullptr);
    RegisterClass(L"TourelleAbout", AboutProc, nullptr);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT bounds{0, 0, Scale(kClientWidth), Scale(kClientHeight)};
    AdjustWindowRect(&bounds, style, FALSE);
    CreateWindowExW(0, L"TourelleWindow", L"Tourelle", style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
                    bounds.bottom - bounds.top, nullptr, nullptr, instance, nullptr);
    if (!trayOnly) {
        ShowWindow(g.window, show);
        ShowMain();
    }

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        if (message.message == WM_KEYDOWN && HandleKey(message.wParam)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    DeleteObject(g.textFont);
    DeleteObject(g.monoFont);
    DeleteObject(g.panelBrush);
    Gdiplus::GdiplusShutdown(g.gdiplus);
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
        const bool trayOnly = argc > 1 && _wcsicmp(argv[1], L"--tray") == 0;
        // One instance: a second launch just brings the running one forward.
        HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\Tourelle.App");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if (HWND running = FindWindowW(L"TourelleWindow", nullptr)) {
                AllowSetForegroundWindow(ASFW_ANY);
                if (!trayOnly) PostMessageW(running, WM_APP_SHOW, 0, 0);
            }
            result = 0;
        } else {
            result = RunApp(instance, show, trayOnly);
        }
        if (single) CloseHandle(single);
    }
    LocalFree(argv);
    return result;
}
