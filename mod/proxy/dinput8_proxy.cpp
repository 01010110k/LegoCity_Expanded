// =============================================================================================
//  dinput8.dll - auto-loads lcudrive.dll (the driving overhaul) into LEGO City Undercover.
//
//  Windows searches the EXE's folder before System32 for non-KnownDLLs, so this file wins the
//  dinput8 slot; every export forwards to the genuine System32 dinput8 (by ABSOLUTE path, or it
//  would load itself). A worker thread waits for the game window, settles, then loads
//  lcudrive.dll - beside this proxy first, then from the toolkit build folder.
//
//  Successor of the vehsim proxy (VehicleSim\proxy): same forwarding, new payload. vehsim.dll is
//  deliberately NOT loaded any more - the two would hook the same vtable slot.
//
//  TO UNINSTALL: delete dinput8.dll from the game folder. Nothing else is touched.
// =============================================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>   // LEAN_AND_MEAN strips COM, but the dinput8 exports speak REFIID/LPUNKNOWN

static HMODULE g_real = nullptr;

static void LoadRealDinput8()
{
    if (g_real) return;
    wchar_t p[MAX_PATH];
    UINT n = GetSystemDirectoryW(p, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 16) return;
    lstrcatW(p, L"\\dinput8.dll");
    g_real = LoadLibraryW(p);
}

static FARPROC Real(const char* name)
{
    if (!g_real) LoadRealDinput8();
    return g_real ? GetProcAddress(g_real, name) : nullptr;
}

// ---------------------------------------------------------------------------------------------
//  Where lcudrive.dll lives. Beside this proxy first, so a copied-in DLL wins; then the toolkit,
//  so the normal case - rebuild in LCU-Modding, relaunch the game - picks up the fresh build
//  with no copy step and no way to run a stale one by accident. Stale copies have burned this
//  project before; the KNOWNGOOD/WORKING snapshots exist because of it.
// ---------------------------------------------------------------------------------------------
// [release1] The public build (LCU_RELEASE) has no toolkit fallback: it loads <game>\LCUMod\lcudrive.dll (the
// installer's layout), then lcudrive.dll beside this proxy.
#ifndef LCU_RELEASE
static const wchar_t* kToolkitDll = L"C:\\LCU-Modding\\Drive\\lcudrive.dll";
#endif

static bool TryLoadPayloadFrom(const wchar_t* path)
{
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return false;
    return LoadLibraryW(path) != nullptr;   // lcudrive's own DllMain takes it from here
}

struct FindCtx { DWORD pid; HWND hit; };

static BOOL CALLBACK FindOurWindow(HWND h, LPARAM lp)
{
    FindCtx* c = reinterpret_cast<FindCtx*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == c->pid && IsWindowVisible(h)) { c->hit = h; return FALSE; }
    return TRUE;
}

static DWORD WINAPI LoadPayload(LPVOID)
{
    const DWORD me = GetCurrentProcessId();

    // EnumWindows rather than GetForegroundWindow: the game may legitimately start unfocused,
    // and a loader that only fires when the window happens to be in front fails intermittently
    // for no visible reason - the exact class of bug this project exists to stop shipping.
    for (int waited = 0; waited < 180000; waited += 250)
    {
        FindCtx ctx = { me, nullptr };
        EnumWindows(FindOurWindow, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.hit)
        {
            Sleep(5000);   // let D3D, ReShade and 3Dmigoto finish coming up first

            wchar_t local[MAX_PATH];
            HMODULE self = nullptr;
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(&LoadPayload), &self) && self)
            {
                DWORD n = GetModuleFileNameW(self, local, MAX_PATH);
                if (n > 0 && n < MAX_PATH)
                {
                    int cut = -1;
                    for (DWORD i = 0; i < n; ++i)
                        if (local[i] == L'\\' || local[i] == L'/') cut = static_cast<int>(i);
                    if (cut >= 0 && cut < MAX_PATH - 24)
                    {
                        local[cut + 1] = 0;
                        wchar_t sub[MAX_PATH];                       // [release1] <game>\LCUMod\lcudrive.dll first
                        lstrcpyW(sub, local);
                        lstrcatW(sub, L"LCUMod\\lcudrive.dll");
                        if (TryLoadPayloadFrom(sub)) return 0;
                        lstrcatW(local, L"lcudrive.dll");
                        if (TryLoadPayloadFrom(local)) return 0;
                    }
                }
            }
#ifndef LCU_RELEASE
            if (TryLoadPayloadFrom(kToolkitDll)) return 0;
#endif

            // No lcudrive anywhere. The game must still run normally - this proxy failing quietly
            // into "just input forwarding" is a feature - but the failure must not be silent,
            // and there is no console to print to. One low beep is the whole error channel.
            Beep(220, 400);
            return 0;
        }
        Sleep(250);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        LoadRealDinput8();
        if (!g_real) return FALSE;   // no real dinput8 means no input; fail the load loudly
        HANDLE t = CreateThread(nullptr, 0, LoadPayload, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
//  Export forwarding. dinput8's export surface is small and has been stable for twenty years,
//  so explicit typed thunks are cheaper than being clever, and the .def stays readable.
// ---------------------------------------------------------------------------------------------
extern "C" {

HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE hinst, DWORD ver, REFIID riid,
                                        LPVOID* out, LPUNKNOWN outer)
{
    typedef HRESULT (WINAPI *FN)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    FN f = reinterpret_cast<FN>(Real("DirectInput8Create"));
    return f ? f(hinst, ver, riid, out, outer) : E_FAIL;
}

HRESULT WINAPI Proxy_DllCanUnloadNow()
{
    typedef HRESULT (WINAPI *FN)();
    FN f = reinterpret_cast<FN>(Real("DllCanUnloadNow"));
    return f ? f() : S_FALSE;
}

HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID cls, REFIID iid, LPVOID* out)
{
    typedef HRESULT (WINAPI *FN)(REFCLSID, REFIID, LPVOID*);
    FN f = reinterpret_cast<FN>(Real("DllGetClassObject"));
    return f ? f(cls, iid, out) : E_FAIL;
}

HRESULT WINAPI Proxy_DllRegisterServer()
{
    typedef HRESULT (WINAPI *FN)();
    FN f = reinterpret_cast<FN>(Real("DllRegisterServer"));
    return f ? f() : E_FAIL;
}

HRESULT WINAPI Proxy_DllUnregisterServer()
{
    typedef HRESULT (WINAPI *FN)();
    FN f = reinterpret_cast<FN>(Real("DllUnregisterServer"));
    return f ? f() : E_FAIL;
}

LPCVOID WINAPI Proxy_GetdfDIJoystick()
{
    typedef LPCVOID (WINAPI *FN)();
    FN f = reinterpret_cast<FN>(Real("GetdfDIJoystick"));
    return f ? f() : nullptr;
}

} // extern "C"
