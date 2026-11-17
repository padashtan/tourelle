#include "Setup.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <setupapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <system_error>

#include "Paths.h"
#include "TourelleClient.h"
#include "TourelleProperties.h"

#include <utility>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace tourelle::setup {
namespace {

constexpr wchar_t kVideoFunctionPrefix[] = L"USB\\VID_046D&PID_0994&MI_00\\";
// Must match tourelle::kWinUsbInterfaceGuid (native/uvc/UvcDevice.h).
constexpr wchar_t kWinUsbInterfaceGuid[] = L"{293D3D63-AF2C-42DA-84B9-B9DDC76A7AB7}";
// GUID_DEVCLASS_USBDEVICE: the setup class of the inbox winusb.inf.
constexpr wchar_t kUsbDeviceClass[] = L"{88BAE032-5A81-49F0-BC3D-A4FF138216D6}";
constexpr wchar_t kDllName[] = L"TourelleCamera.dll";
constexpr int kEmbeddedSourceResource = 2;  // RCDATA in Tourelle.rc
// SYSTEM, Administrators, LocalService (Frame Server): full access; nobody else.
constexpr wchar_t kDeviceSecurity[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;LS)";

[[noreturn]] void Fail(const char* what, DWORD error = GetLastError()) {
    throw std::system_error(static_cast<int>(error), std::system_category(), what);
}

std::wstring ClassKey() { return std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + kTourelleCameraClsid; }

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

// Instance id of the camera's video function ("USB\VID_046D&PID_0994&MI_00\..."), or empty.
std::wstring FindVideoFunction(bool presentOnly) {
    DWORD flags = DIGCF_ALLCLASSES | (presentOnly ? DIGCF_PRESENT : 0);
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, flags);
    if (set == INVALID_HANDLE_VALUE) return {};
    std::wstring result;
    SP_DEVINFO_DATA data{sizeof(data)};
    for (DWORD i = 0; result.empty() && SetupDiEnumDeviceInfo(set, i, &data); ++i) {
        wchar_t id[MAX_DEVICE_ID_LEN];
        if (SetupDiGetDeviceInstanceIdW(set, &data, id, ARRAYSIZE(id), nullptr) &&
            _wcsnicmp(id, kVideoFunctionPrefix, wcslen(kVideoFunctionPrefix)) == 0) {
            result = id;
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

// Instance ids of every node of the camera's video function, present or not.
std::vector<std::wstring> VideoFunctions() {
    std::vector<std::wstring> ids;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) return ids;
    SP_DEVINFO_DATA data{sizeof(data)};
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); ++i) {
        wchar_t id[MAX_DEVICE_ID_LEN];
        if (SetupDiGetDeviceInstanceIdW(set, &data, id, ARRAYSIZE(id), nullptr) &&
            _wcsnicmp(id, kVideoFunctionPrefix, wcslen(kVideoFunctionPrefix)) == 0) {
            ids.emplace_back(id);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return ids;
}

std::wstring DevNodeProperty(const std::wstring& id, ULONG property) {
    DEVINST node = 0;
    if (CM_Locate_DevNodeW(&node, const_cast<wchar_t*>(id.c_str()), CM_LOCATE_DEVNODE_PHANTOM) != CR_SUCCESS) return {};
    wchar_t buffer[256]{};
    ULONG length = sizeof(buffer);
    if (CM_Get_DevNode_Registry_PropertyW(node, property, nullptr, buffer, &length, 0) != CR_SUCCESS) return {};
    return buffer;
}

bool IsWinUsb(const std::wstring& id) { return _wcsicmp(DevNodeProperty(id, CM_DRP_SERVICE).c_str(), L"WinUSB") == 0; }

// One device information element, opened by instance id.
class Device {
public:
    explicit Device(const std::wstring& id) {
        set_ = SetupDiCreateDeviceInfoList(nullptr, nullptr);
        if (set_ == INVALID_HANDLE_VALUE) Fail("SetupDiCreateDeviceInfoList");
        data_.cbSize = sizeof(data_);
        if (!SetupDiOpenDeviceInfoW(set_, id.c_str(), nullptr, 0, &data_)) {
            DWORD error = GetLastError();
            SetupDiDestroyDeviceInfoList(set_);
            Fail("SetupDiOpenDeviceInfo", error);
        }
    }
    ~Device() { SetupDiDestroyDeviceInfoList(set_); }
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    HDEVINFO Set() const { return set_; }
    SP_DEVINFO_DATA* Data() { return &data_; }

private:
    HDEVINFO set_ = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA data_{};
};

void SetClassGuid(const std::wstring& id, const std::wstring& classGuid) {
    Device device(id);
    const DWORD bytes = static_cast<DWORD>((classGuid.size() + 1) * sizeof(wchar_t));
    if (!SetupDiSetDeviceRegistryPropertyW(device.Set(), device.Data(), SPDRP_CLASSGUID,
                                           reinterpret_cast<const BYTE*>(classGuid.c_str()), bytes)) {
        Fail("SetupDiSetDeviceRegistryProperty(SPDRP_CLASSGUID)");
    }
}

// Installs Windows' own winusb.inf on the device. The INF does not match the
// camera's hardware id, so its driver is selected explicitly, the way Device
// Manager does when a driver is picked by hand. The device must already be in
// the USBDevice setup class.
void InstallWinUsbDriver(const std::wstring& id) {
    Device device(id);
    SP_DEVINSTALL_PARAMS_W params{};
    params.cbSize = sizeof(params);
    if (!SetupDiGetDeviceInstallParamsW(device.Set(), device.Data(), &params)) Fail("SetupDiGetDeviceInstallParams");
    params.Flags |= DI_ENUMSINGLEINF;
    params.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
    wchar_t windows[MAX_PATH];
    const UINT length = GetSystemWindowsDirectoryW(windows, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) Fail("GetSystemWindowsDirectory");
    swprintf_s(params.DriverPath, L"%s\\INF\\winusb.inf", windows);
    if (!SetupDiSetDeviceInstallParamsW(device.Set(), device.Data(), &params)) Fail("SetupDiSetDeviceInstallParams");
    if (!SetupDiBuildDriverInfoList(device.Set(), device.Data(), SPDIT_CLASSDRIVER)) Fail("SetupDiBuildDriverInfoList");

    SP_DRVINFO_DATA_W driver{};
    driver.cbSize = sizeof(driver);
    bool found = false;
    for (DWORD i = 0; !found && SetupDiEnumDriverInfoW(device.Set(), device.Data(), SPDIT_CLASSDRIVER, i, &driver); ++i) {
        found = Lower(driver.Description).find(L"winusb") != std::wstring::npos;
    }
    if (!found) throw std::runtime_error("The WinUSB driver was not found in winusb.inf.");
    if (!SetupDiSetSelectedDriverW(device.Set(), device.Data(), &driver)) Fail("SetupDiSetSelectedDriver");
    BOOL reboot = FALSE;
    if (!DiInstallDevice(nullptr, device.Set(), device.Data(), &driver, 0, &reboot)) Fail("DiInstallDevice");
}

// WinUSB publishes a device interface only for the GUIDs listed here.
void SetInterfaceGuid(const std::wstring& id) {
    Device device(id);
    HKEY key = SetupDiOpenDevRegKey(device.Set(), device.Data(), DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_SET_VALUE);
    if (key == INVALID_HANDLE_VALUE) Fail("SetupDiOpenDevRegKey");
    std::wstring multi = kWinUsbInterfaceGuid;
    multi.push_back(L'\0');
    multi.push_back(L'\0');
    LSTATUS status = RegSetValueExW(key, L"DeviceInterfaceGUIDs", 0, REG_MULTI_SZ,
                                    reinterpret_cast<const BYTE*>(multi.data()),
                                    static_cast<DWORD>(multi.size() * sizeof(wchar_t)));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) Fail("RegSetValueEx(DeviceInterfaceGUIDs)", static_cast<DWORD>(status));
}

// True when the node carries Tourelle's WinUSB interface GUID (set by SetInterfaceGuid).
bool HasTourelleInterface(const std::wstring& id) {
    Device device(id);
    HKEY key = SetupDiOpenDevRegKey(device.Set(), device.Data(), DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
    if (key == INVALID_HANDLE_VALUE) return false;
    wchar_t value[512]{};
    DWORD size = sizeof(value) - sizeof(wchar_t);  // keep a terminator
    const bool read = RegGetValueW(key, nullptr, L"DeviceInterfaceGUIDs", RRF_RT_REG_MULTI_SZ, nullptr, value,
                                   &size) == ERROR_SUCCESS;
    RegCloseKey(key);
    if (!read) return false;
    for (const wchar_t* entry = value; *entry; entry += wcslen(entry) + 1) {
        if (_wcsicmp(entry, kWinUsbInterfaceGuid) == 0) return true;
    }
    return false;
}

void RestartDevice(const std::wstring& id) {
    Device device(id);
    SP_PROPCHANGE_PARAMS change{};
    change.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    change.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    change.StateChange = DICS_PROPCHANGE;
    change.Scope = DICS_FLAG_GLOBAL;
    if (!SetupDiSetClassInstallParamsW(device.Set(), device.Data(), &change.ClassInstallHeader, sizeof(change))) {
        Fail("SetupDiSetClassInstallParams");
    }
    if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, device.Set(), device.Data())) Fail("DIF_PROPERTYCHANGE");
}

// Removing the device node drops the forced driver; re-enumeration then
// installs the camera's normal driver (usbvideo.sys) again.
void RemoveAndRescan(const std::wstring& id) {
    {
        Device device(id);
        SP_REMOVEDEVICE_PARAMS remove{};
        remove.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        remove.ClassInstallHeader.InstallFunction = DIF_REMOVE;
        remove.Scope = DI_REMOVEDEVICE_GLOBAL;
        if (!SetupDiSetClassInstallParamsW(device.Set(), device.Data(), &remove.ClassInstallHeader, sizeof(remove))) {
            Fail("SetupDiSetClassInstallParams");
        }
        if (!SetupDiCallClassInstaller(DIF_REMOVE, device.Set(), device.Data())) Fail("DIF_REMOVE");
    }
    DEVINST root = 0;
    if (CM_Locate_DevNodeW(&root, nullptr, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS) {
        CM_Reenumerate_DevNode(root, CM_REENUMERATE_SYNCHRONOUS);
    }
}

// The Frame Server service loads the source: it must live outside user profiles.
std::wstring InstallDirectory() {
    PWSTR programFiles = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &programFiles))) {
        throw std::runtime_error("Program Files folder not found.");
    }
    std::wstring directory = std::wstring(programFiles) + L"\\Tourelle";
    CoTaskMemFree(programFiles);
    return directory;
}

std::wstring RegisteredDllPath() {
    wchar_t path[MAX_PATH]{};
    DWORD size = sizeof(path);
    const std::wstring key = ClassKey() + L"\\InprocServer32";
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, path, &size) != ERROR_SUCCESS) {
        return {};
    }
    return path;
}

// Deletes a file now, or at the next reboot if a process still has it loaded.
void DeleteOrSchedule(const std::wstring& path) {
    if (DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) return;
    const std::wstring parked = path + L".old." + std::to_wstring(GetTickCount64());
    if (MoveFileExW(path.c_str(), parked.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(parked.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    } else {
        MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
}

void SetRegistryString(HKEY key, const wchar_t* name, const std::wstring& value) {
    LSTATUS status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                    static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) Fail("RegSetValueEx", static_cast<DWORD>(status));
}

// The camera source is embedded in Tourelle.exe (RCDATA resource): the elevated
// process writes it from its own image, never from a file next to it that the user,
// or anything running as the user, could have swapped (DLL planting).
std::pair<const void*, DWORD> EmbeddedSource() {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(kEmbeddedSourceResource), MAKEINTRESOURCEW(10));  // RT_RCDATA
    HGLOBAL loaded = resource ? LoadResource(nullptr, resource) : nullptr;
    const void* data = loaded ? LockResource(loaded) : nullptr;
    const DWORD size = resource ? SizeofResource(nullptr, resource) : 0;
    if (!data || size == 0) throw std::runtime_error("The camera source is missing from Tourelle.exe.");
    return {data, size};
}

std::wstring FullPath(const std::wstring& path) {
    wchar_t full[MAX_PATH];
    const DWORD length = GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
    return length == 0 || length >= MAX_PATH ? std::wstring() : std::wstring(full);
}

// True when path is a file inside directory (after normalizing "..", ".", separators).
bool IsInside(const std::wstring& path, const std::wstring& directory) {
    return core::IsPathInside(FullPath(path), FullPath(directory));
}

// Writes data to target through a temporary file in the same (administrator-only)
// folder, so a failure never leaves a half-written or missing DLL behind.
void WriteFileReplacing(const std::wstring& target, const void* data, DWORD size) {
    const std::wstring temporary = target + L".new";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) Fail("CreateFile(TourelleCamera.dll.new)");
    DWORD written = 0;
    const bool ok = WriteFile(file, data, size, &written, nullptr) && written == size && FlushFileBuffers(file);
    const DWORD writeError = GetLastError();
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(temporary.c_str());
        Fail("WriteFile(TourelleCamera.dll.new)", writeError);
    }
    if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;

    const DWORD error = GetLastError();
    if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) {
        DeleteFileW(temporary.c_str());
        Fail("MoveFileEx(TourelleCamera.dll)", error);
    }
    // The Frame Server still has the previous version loaded. A loaded DLL can be
    // renamed: park it (deleted at the next reboot), then move the new one in.
    const std::wstring parked = target + L".old." + std::to_wstring(GetTickCount64());
    if (!MoveFileExW(target.c_str(), parked.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const DWORD parkError = GetLastError();
        DeleteFileW(temporary.c_str());
        Fail("MoveFileEx(park TourelleCamera.dll)", parkError);
    }
    MoveFileExW(parked.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const DWORD moveError = GetLastError();
        MoveFileExW(parked.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);  // put the old one back
        DeleteFileW(temporary.c_str());
        Fail("MoveFileEx(TourelleCamera.dll)", moveError);
    }
}

void InstallSource() {
    const auto [data, size] = EmbeddedSource();
    const std::wstring directory = InstallDirectory();
    CreateDirectoryW(directory.c_str(), nullptr);
    const std::wstring target = directory + L"\\" + kDllName;
    WriteFileReplacing(target, data, size);

    HKEY key = nullptr;
    const std::wstring server = ClassKey() + L"\\InprocServer32";
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, server.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                                     nullptr);
    if (status != ERROR_SUCCESS) Fail("RegCreateKeyEx", static_cast<DWORD>(status));
    SetRegistryString(key, nullptr, target);
    SetRegistryString(key, L"ThreadingModel", L"Both");
    RegCloseKey(key);
    RegSetKeyValueW(HKEY_LOCAL_MACHINE, ClassKey().c_str(), nullptr, REG_SZ, L"Tourelle Camera Source",
                    static_cast<DWORD>(sizeof(L"Tourelle Camera Source")));
}

void RemoveSource() {
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, ClassKey().c_str());
    // Only ever delete our own file: never a path read from the registry.
    const std::wstring directory = InstallDirectory();
    DeleteOrSchedule(directory + L"\\" + kDllName);
    DeleteFileW((directory + L"\\" + kDllName + L".new").c_str());
    RemoveDirectoryW(directory.c_str());  // fails harmlessly while files remain or wait for reboot
}

// Only SYSTEM, administrators and LocalService (the Frame Server's account) may open
// the WinUSB-bound camera: apps go through the virtual camera and the Windows camera
// privacy settings, not around them.
void RestrictDeviceAccess(const std::wstring& id) {
    Device device(id);
    const std::wstring sddl = kDeviceSecurity;
    if (!SetupDiSetDeviceRegistryPropertyW(device.Set(), device.Data(), SPDRP_SECURITY_SDS,
                                           reinterpret_cast<const BYTE*>(sddl.c_str()),
                                           static_cast<DWORD>((sddl.size() + 1) * sizeof(wchar_t)))) {
        Fail("SetupDiSetDeviceRegistryProperty(SPDRP_SECURITY_SDS)");
    }
}

}  // namespace

Status Query() {
    Status status;
    const std::wstring id = FindVideoFunction(true);
    status.cameraPresent = !id.empty();
    status.winusbBound = status.cameraPresent && IsWinUsb(id);
    const std::wstring dll = RegisteredDllPath();
    status.sourceInstalled = !dll.empty() && GetFileAttributesW(dll.c_str()) != INVALID_FILE_ATTRIBUTES;
    status.virtualCameraListed = client::IsVirtualCameraListed();
    return status;
}

std::wstring Describe() {
    std::wstring text;
    const auto line = [&](const std::wstring& name, const std::wstring& value) {
        text += name + L": " + (value.empty() ? L"-" : value) + L"\r\n";
    };
    const std::wstring id = FindVideoFunction(false);
    line(L"Video function", id);
    if (!id.empty()) {
        line(L"  present", FindVideoFunction(true).empty() ? L"no" : L"yes");
        line(L"  service", DevNodeProperty(id, CM_DRP_SERVICE));
        line(L"  class", DevNodeProperty(id, CM_DRP_CLASSGUID));
    }
    const std::wstring dll = RegisteredDllPath();
    // Never print a path outside Program Files: it could name a user profile.
    line(L"Camera source", dll.empty() ? L"" : IsInside(dll, InstallDirectory()) ? dll : L"<outside Program Files>");
    if (!dll.empty()) {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetFileAttributesExW(dll.c_str(), GetFileExInfoStandard, &data)) {
            SYSTEMTIME written{};
            FileTimeToSystemTime(&data.ftLastWriteTime, &written);
            wchar_t info[96];
            swprintf_s(info, L"%lu bytes, written %04u-%02u-%02u %02u:%02u UTC", data.nFileSizeLow, written.wYear,
                       written.wMonth, written.wDay, written.wHour, written.wMinute);
            line(L"  file", info);
        } else {
            line(L"  file", L"missing");
        }
    }
    line(L"Virtual camera listed", client::IsVirtualCameraListed() ? L"yes" : L"no");

    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    for (const wchar_t* name : {L"FrameServer", L"FrameServerMonitor"}) {
        std::wstring state = L"unknown";
        if (SC_HANDLE service = manager ? OpenServiceW(manager, name, SERVICE_QUERY_STATUS) : nullptr) {
            SERVICE_STATUS status{};
            if (QueryServiceStatus(service, &status)) {
                state = status.dwCurrentState == SERVICE_RUNNING   ? L"running"
                        : status.dwCurrentState == SERVICE_STOPPED ? L"stopped"
                                                                   : std::to_wstring(status.dwCurrentState);
            }
            CloseServiceHandle(service);
        }
        line(std::wstring(L"Service ") + name, state);
    }
    if (manager) CloseServiceHandle(manager);
    return text;
}

void InstallElevated(bool requireCamera) {
    InstallSource();

    const std::wstring id = FindVideoFunction(true);
    if (id.empty()) {
        if (!requireCamera) return;
        throw std::runtime_error("Camera not connected (USB 046D:0994).");
    }
    const bool switching = !IsWinUsb(id);
    const std::wstring originalClass = DevNodeProperty(id, CM_DRP_CLASSGUID);
    try {
        if (switching) {
            SetClassGuid(id, kUsbDeviceClass);
            InstallWinUsbDriver(id);
        }
        SetInterfaceGuid(id);
        RestrictDeviceAccess(id);
        RestartDevice(id);
    } catch (...) {
        if (switching) {
            // Give the camera back to Windows: restore its class, or when that is not
            // possible, remove the device node so the rescan reinstalls usbvideo.sys.
            try {
                if (originalClass.empty()) throw std::runtime_error("original class unknown");
                SetClassGuid(id, originalClass);
                RemoveAndRescan(id);
            } catch (...) {
                try {
                    RemoveAndRescan(id);
                } catch (...) {
                }
            }
        }
        throw;
    }
}

void UninstallElevated() {
    // Every node of the camera's video function, plugged in or not (a phantom node
    // keeps its driver for next time). Only nodes Tourelle bound are touched: a
    // WinUSB binding made by another tool (without our interface GUID) is left alone.
    for (const std::wstring& id : VideoFunctions()) {
        if (IsWinUsb(id) && HasTourelleInterface(id)) RemoveAndRescan(id);
    }
    RemoveSource();
}

int RunElevated(const wchar_t* argument) {
    wchar_t self[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return static_cast<int>(ERROR_FILENAME_EXCED_RANGE);
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = self;
    info.lpParameters = argument;
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        const DWORD error = GetLastError();
        return error == ERROR_CANCELLED ? -1 : static_cast<int>(error);
    }
    if (!info.hProcess) return static_cast<int>(ERROR_INVALID_HANDLE);  // no process to wait for
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return static_cast<int>(code);
}

}  // namespace tourelle::setup
