// DSR FPS Limiter — caps Dark Souls Remastered (x64, D3D11) frame rate so the GPU
// isn't running flat out. Works by replacing IDXGISwapChain::Present in the shared
// vtable, so it chains cleanly with other mods/overlays (e.g. Seamless Co-op).
//
// Build modes:
//   default       -> acts as dinput8.dll proxy (drop-in, forwards DirectInput8Create)
//   -DNO_PROXY    -> plain DLL, load it with any injector / mod loader instead
//
// Config: dsr_fps_limit.ini next to the DLL:  [Limiter]  FPS=30

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#ifdef _MSC_VER
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "winmm.lib")
#endif

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);

static PresentFn g_origPresent = nullptr;
static HMODULE g_self = nullptr;
static LARGE_INTEGER g_freq, g_next;
static LONGLONG g_interval = 0;
static HANDLE g_timer = nullptr;
static bool g_needTimePeriod = false;

// ---------------------------------------------------------------- limiter
static void LimitFrame()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (g_next.QuadPart == 0) g_next.QuadPart = now.QuadPart;
    g_next.QuadPart += g_interval;

    // Fell far behind (loading screen, alt-tab, hitch): resync instead of bursting.
    if (now.QuadPart > g_next.QuadPart + g_interval * 2) {
        g_next.QuadPart = now.QuadPart;
        return;
    }

    // Coarse wait on a waitable timer, leaving ~1.5 ms to spin for precision.
    LONGLONG coarse = (g_next.QuadPart - now.QuadPart) - g_freq.QuadPart * 3 / 2000;
    if (coarse > 0 && g_timer) {
        LARGE_INTEGER due;
        due.QuadPart = -(coarse * 10000000LL / g_freq.QuadPart); // 100 ns units, relative
        if (SetWaitableTimer(g_timer, &due, 0, nullptr, nullptr, FALSE))
            WaitForSingleObject(g_timer, 100);
    }
    do {
        YieldProcessor();
        QueryPerformanceCounter(&now);
    } while (now.QuadPart < g_next.QuadPart);
}

static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* sc, UINT sync, UINT flags)
{
    if (!(flags & DXGI_PRESENT_TEST)) LimitFrame();
    return g_origPresent(sc, sync, flags);
}

// ---------------------------------------------------------------- hooking
static LRESULT CALLBACK DummyProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static bool InstallHook()
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DummyProc;
    wc.hInstance = g_self;
    wc.lpszClassName = L"DSRFpsLimitDummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 100, 100,
                              nullptr, nullptr, g_self, nullptr);
    if (!hwnd) return false;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                               nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    bool ok = false;
    if (SUCCEEDED(hr) && sc) {
        void** vtbl = *reinterpret_cast<void***>(sc);
        const int PRESENT_INDEX = 8; // IDXGISwapChain::Present
        DWORD old;
        if (VirtualProtect(&vtbl[PRESENT_INDEX], sizeof(void*), PAGE_READWRITE, &old)) {
            g_origPresent = reinterpret_cast<PresentFn>(vtbl[PRESENT_INDEX]);
            vtbl[PRESENT_INDEX] = reinterpret_cast<void*>(&HookedPresent);
            VirtualProtect(&vtbl[PRESENT_INDEX], sizeof(void*), old, &old);
            ok = true;
        }
    }
    if (sc) sc->Release();
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, g_self);
    return ok;
}

static DWORD WINAPI InitThread(LPVOID)
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *(slash + 1) = 0;
    wcscat_s(path, L"dsr_fps_limit.ini");

    int fps = GetPrivateProfileIntW(L"Limiter", L"FPS", 30, path);
    if (fps < 10) fps = 10;
    if (fps > 1000) fps = 1000;

    QueryPerformanceFrequency(&g_freq);
    g_interval = g_freq.QuadPart / fps;

    g_timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/,
                                     TIMER_ALL_ACCESS);
    if (!g_timer) {
        g_timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
        timeBeginPeriod(1);
        g_needTimePeriod = true;
    }

    // Wait for the game to load D3D11/DXGI, then hook (retry for up to ~60 s).
    for (int i = 0; i < 600; ++i) {
        if (GetModuleHandleW(L"d3d11.dll") && GetModuleHandleW(L"dxgi.dll")) {
            Sleep(500);
            if (InstallHook()) return 0;
        }
        Sleep(100);
    }
    return 0;
}

// ---------------------------------------------------------------- entry / proxy
#ifndef NO_PROXY
typedef HRESULT(WINAPI* DI8CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DI8CreateFn g_realDI8 = nullptr;

extern "C" __declspec(dllexport)
HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD ver, REFIID riid, LPVOID* out, LPUNKNOWN unk)
{
    if (!g_realDI8) {
        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        wcscat_s(sys, L"\\dinput8.dll");
        HMODULE real = LoadLibraryW(sys);
        if (real) g_realDI8 = reinterpret_cast<DI8CreateFn>(GetProcAddress(real, "DirectInput8Create"));
    }
    return g_realDI8 ? g_realDI8(hinst, ver, riid, out, unk) : E_FAIL;
}
#endif

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h;
        DisableThreadLibraryCalls(h);
        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
