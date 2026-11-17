#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4324)  // structure padded due to alignment specifier, inside the SDK's wrl/module.h
#include <wrl/module.h>
#pragma warning(pop)

#include "TourelleActivate.h"

using namespace Microsoft::WRL;

namespace tourelle {
CoCreatableClass(TourelleActivate);
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* object) {
    return Module<InProc>::GetModule().GetClassObject(clsid, riid, object);
}

STDAPI DllCanUnloadNow() { return Module<InProc>::GetModule().Terminate() ? S_OK : S_FALSE; }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
