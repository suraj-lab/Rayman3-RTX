// Standalone x86 D3D8 ABI/hook check; never loads Rayman3.exe.
// Zero-based COM slots verified against Wine's include/d3d8.h.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exception>
#include <system_error>
#include <typeinfo>

struct IDirect3D8;
struct IDirect3DDevice8;
struct D3DPRESENT_PARAMETERS {
    UINT BackBufferWidth, BackBufferHeight, BackBufferFormat, BackBufferCount;
    UINT MultiSampleType, SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed, EnableAutoDepthStencil;
    UINT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz, FullScreen_PresentationInterval;
};
static_assert(sizeof(void *) == 4, "x86 only");
static_assert(sizeof(D3DPRESENT_PARAMETERS) == 52, "D3D8 presentation ABI mismatch");
enum { GetAdapterCountSlot = 4, GetAdapterMonitorSlot = 14, CreateDeviceSlot = 15 };
typedef IDirect3D8 *(__stdcall *Create8_t)(UINT);
typedef UINT (__stdcall *GetAdapterCount_t)(IDirect3D8 *);
typedef HMONITOR (__stdcall *GetAdapterMonitor_t)(IDirect3D8 *, UINT);
typedef HRESULT (__stdcall *CreateDevice_t)(IDirect3D8 *, UINT, UINT, HWND, DWORD,
                                           D3DPRESENT_PARAMETERS *, IDirect3DDevice8 **);
static FILE *logFile;
#define P(...) do { fprintf(logFile, __VA_ARGS__); fputc('\n', logFile); fflush(logFile); } while (0)

static void Release(void *object)
{
    if (object) ((ULONG (__stdcall *)(void *))(*(void ***)object)[2])(object);
}

// Drives the window messages the shim's handler routes (WM_ACTIVATEAPP/WM_SIZE/
// WM_DESTROY) after device creation. An uncaught shim throw would abort the
// process, so each send is caught and logged before it can reach terminate().
static int SendLifecycleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, const char *name)
{
    try {
        LRESULT result = SendMessageA(hwnd, msg, wp, lp);
        P("lifecycle %s -> LRESULT 0x%lX", name, (unsigned long)result);
        return 1;
    } catch (const std::exception &error) {
        P("lifecycle %s -> C++ exception: %s", name, error.what());
    } catch (...) {
        P("lifecycle %s -> C++ exception of unknown type", name);
    }
    return 0;
}

// Real queue dispatch, not SendMessage: this is how the OS delivers activation,
// resize and display-change traffic. Message IDs are logged so a throwing one is named.
static void PumpMessages(int millis)
{
    MSG msg;
    int dispatched = 0;
    DWORD start = GetTickCount();
    while (GetTickCount() - start < (DWORD)millis) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            try {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
                if (msg.message != WM_PAINT) dispatched++;
            } catch (const std::exception &error) {
                P("lifecycle pump message 0x%04X -> C++ exception: %s", msg.message, error.what());
                return;
            } catch (...) {
                P("lifecycle pump message 0x%04X -> C++ exception of unknown type", msg.message);
                return;
            }
        }
        Sleep(1);
    }
    P("lifecycle pump %dms -> %d messages dispatched", millis, dispatched);
}

static void RunMessageStress(HWND hwnd)
{
    P("STRESS: begin 8 activate/resize cycles");
    int clean = 0, total = 0;
    WINDOWPOS pos = { hwnd, NULL, 0, 0, 512, 384, SWP_NOZORDER | SWP_NOACTIVATE };
    for (int i = 0; i < 8; i++) {
        total++; clean += SendLifecycleMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0, "WM_ACTIVATE(ACTIVE)");
        total++; clean += SendLifecycleMessage(hwnd, WM_NCACTIVATE, TRUE, 0, "WM_NCACTIVATE(TRUE)");
        total++; clean += SendLifecycleMessage(hwnd, WM_SETFOCUS, 0, 0, "WM_SETFOCUS");
        total++; clean += SendLifecycleMessage(hwnd, WM_ACTIVATEAPP, TRUE, 0, "WM_ACTIVATEAPP(TRUE)");
        SetWindowPos(hwnd, NULL, 0, 0, 512 + i * 16, 384 + i * 16,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        PumpMessages(60);
        total++; clean += SendLifecycleMessage(hwnd, WM_SIZE, SIZE_RESTORED,
                                              MAKELPARAM(512 + i * 16, 384 + i * 16), "WM_SIZE");
        total++; clean += SendLifecycleMessage(hwnd, WM_WINDOWPOSCHANGED, 0, (LPARAM)&pos,
                                              "WM_WINDOWPOSCHANGED");
        total++; clean += SendLifecycleMessage(hwnd, WM_DISPLAYCHANGE, 32, MAKELPARAM(800, 600),
                                              "WM_DISPLAYCHANGE(800x600)");
        total++; clean += SendLifecycleMessage(hwnd, WM_KILLFOCUS, 0, 0, "WM_KILLFOCUS");
        total++; clean += SendLifecycleMessage(hwnd, WM_ACTIVATEAPP, FALSE, 0, "WM_ACTIVATEAPP(FALSE)");
        total++; clean += SendLifecycleMessage(hwnd, WM_NCACTIVATE, FALSE, 0, "WM_NCACTIVATE(FALSE)");
    }
    P("STRESS: %s (%d/%d messages clean)", clean == total ? "clean" : "exception observed", clean, total);
}

static void RunLifecycle(HWND hwnd, int idleMillis, int stress)
{
    int clean = 0, total = 0;
    P("LIFECYCLE: begin hwnd=%p image=%p", (void *)hwnd, (void *)GetModuleHandleA(NULL));
    // Never activate: the machine is in use and the probe must not steal focus.
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
    PumpMessages(400);
    total++; clean += SendLifecycleMessage(hwnd, WM_ACTIVATEAPP, TRUE, 0, "WM_ACTIVATEAPP(TRUE)");
    // Real size change through the OS path, then let the queue deliver it.
    SetWindowPos(hwnd, NULL, 0, 0, 512, 384, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    PumpMessages(400);
    total++; clean += SendLifecycleMessage(hwnd, WM_SIZE, SIZE_RESTORED,
                                          MAKELPARAM(512, 384), "WM_SIZE(512x384)");
    total++; clean += SendLifecycleMessage(hwnd, WM_ACTIVATEAPP, FALSE, 0, "WM_ACTIVATEAPP(FALSE)");
    PumpMessages(idleMillis);
    if (stress) RunMessageStress(hwnd);
    P("LIFECYCLE: %s (%d/%d messages clean)", clean == total ? "clean" : "exception observed", clean, total);
}

static void DestroyLifecycleWindow(HWND hwnd)
{
    try {
        P("lifecycle WM_DESTROY -> entering DestroyWindow");
        DestroyWindow(hwnd);
        P("lifecycle WM_DESTROY -> DestroyWindow returned");
    } catch (const std::exception &error) {
        P("lifecycle WM_DESTROY -> C++ exception: %s", error.what());
    } catch (...) {
        P("lifecycle WM_DESTROY -> C++ exception of unknown type");
    }
}

// First-chance view of a C++ throw: the live exception object and throw info,
// which the final unhandled record no longer carries. Read-only inspection.
static void DumpStrings(const char *label, const unsigned char *base, int size)
{
    char run[65];
    int used = 0, shown = 0;
    for (int i = 0; i < size && shown < 6; i++) {
        char c = (char)base[i];
        if (c >= 0x20 && c < 0x7F) {
            if (used < 64) run[used++] = c;
        } else {
            if (used >= 4) { run[used] = 0; P("  %s string @+0x%X: \"%s\"", label, i - used, run); shown++; }
            used = 0;
        }
    }
    if (used >= 4 && shown < 6) { run[used] = 0; P("  %s string @+0x%X: \"%s\"", label, size - used, run); }
}

static void DumpTypeName(const unsigned char *throwInfo)
{
    // MSVC x86: ThrowInfo{attributes, pmfnUnwind, pForwardCompat, pCatchableTypeArray},
    // CatchableTypeArray{count, pType[]}, CatchableType{properties, pType, ...},
    // TypeDescriptor{vftable, spare, name[]}.
    const unsigned char *array = *(const unsigned char *const *)(throwInfo + 12);
    if (!array) { P("  throwInfo has no catchable type array"); return; }
    int count = *(const int *)array;
    P("  catchable types=%d", count);
    for (int i = 0; i < count && i < 4; i++) {
        const unsigned char *catchable = *(const unsigned char *const *)(array + 4 + i * 4);
        if (!catchable) continue;
        const unsigned char *descriptor = *(const unsigned char *const *)(catchable + 4);
        if (!descriptor) continue;
        P("  catchable type[%d] name=%s", i, (const char *)(descriptor + 8));
    }
}

static LONG CALLBACK FirstChance(EXCEPTION_POINTERS *info)
{
    EXCEPTION_RECORD *record = info->ExceptionRecord;
    if (record->ExceptionCode != 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH;
    P("FIRSTCHANCE C++ throw tid=%lu addr=%p params=%lu", (unsigned long)GetCurrentThreadId(),
      (void *)record->ExceptionAddress, record->NumberParameters);
    if (record->NumberParameters >= 3) {
        const unsigned char *object = (const unsigned char *)record->ExceptionInformation[1];
        const unsigned char *throwInfo = (const unsigned char *)record->ExceptionInformation[2];
        P("  exception object=%p throwInfo=%p", (void *)object, (void *)throwInfo);
        if (throwInfo) DumpTypeName(throwInfo);
        if (object) DumpStrings("object", object, 192);
        void *frames[12];
        USHORT count = CaptureStackBackTrace(1, 12, frames, NULL);
        P("  throw stack: %u frames", count);
        for (USHORT i = 0; i < count; i++) {
            char module[MAX_PATH] = "?";
            HMODULE owner = NULL;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)frames[i], &owner);
            if (owner) GetModuleFileNameA(owner, module, sizeof(module));
            P("  throw frame[%u]=%p module=%s offset=0x%lX", i, frames[i], module,
              owner ? (unsigned long)((ULONG_PTR)frames[i] - (ULONG_PTR)owner) : 0ul);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Re-entrancy test: the shim's WndProc handler runs, then calls into this proc.
// Sending synchronously from inside it re-enters that handler while the outer
// invocation is still on the stack, which is how a non-recursive lock gets
// locked twice by one thread.
static int g_reenterDepth;
static int g_reenters;
static LRESULT CALLBACK ReentrantProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // Bounded: unbounded nesting ping-pongs through the queue forever (observed).
    if (g_reenterDepth == 0 && g_reenters < 4 && (msg == WM_SIZE || msg == WM_ACTIVATEAPP)) {
        g_reenterDepth++;
        g_reenters++;
        if (g_reenters == 4) P("reentrant: cap reached, no further nesting");
        if (msg == WM_SIZE) {
            P("reentrant: WM_SIZE -> nested WM_ACTIVATEAPP(TRUE)");
            SendMessageA(hwnd, WM_ACTIVATEAPP, TRUE, 0);
        } else {
            P("reentrant: WM_ACTIVATEAPP -> nested WM_SIZE(512x384)");
            SendMessageA(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(512, 384));
        }
        g_reenterDepth--;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int HasArg(int argc, char **argv, const char *wanted)
{
    for (int i = 1; i < argc; i++) if (strcmp(argv[i], wanted) == 0) return 1;
    return 0;
}

// The abort may run on a shim-owned thread, where no handler of ours is reachable.
static DWORD harnessTid;
static void OnTerminate(void)
{
    P("TERMINATE on tid=%lu (harness tid=%lu)", (unsigned long)GetCurrentThreadId(),
      (unsigned long)harnessTid);
    abort();
}

// Names the fault before the CRT's top-level filter turns it into exit(3).
static LONG WINAPI LogUnhandled(EXCEPTION_POINTERS *info)
{
    EXCEPTION_RECORD *record = info->ExceptionRecord;
    char module[MAX_PATH] = "?";
    HMODULE owner = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)record->ExceptionAddress, &owner) && owner) {
        GetModuleFileNameA(owner, module, sizeof(module));
    }
    P("UNHANDLED code=0x%08lX flags=0x%08lX addr=%p module=%s offset=0x%lX params=%lu",
      record->ExceptionCode, record->ExceptionFlags, (void *)record->ExceptionAddress, module,
      owner ? (unsigned long)((ULONG_PTR)record->ExceptionAddress - (ULONG_PTR)owner) : 0ul,
      record->NumberParameters);
    for (DWORD i = 0; i < record->NumberParameters && i < 4; i++)
        P("  unhandled arg[%lu]=0x%lX", i, (unsigned long)record->ExceptionInformation[i]);
    return EXCEPTION_CONTINUE_SEARCH;
}

static int Check(HMODULE library, HWND hwnd, int lifecycle, int fullscreen, int idleMillis, int stress,
                 int reuseWindow)
{
    Create8_t create8 = (Create8_t)GetProcAddress(library, "Direct3DCreate8");
    if (!create8) { P("FAIL: Direct3DCreate8 export missing"); return 3; }
    IDirect3D8 *d3d = create8(220);
    if (!d3d) { P("FAIL: Direct3DCreate8 returned null"); return 4; }
    // A second object catches recursive re-hooking of a shared vtable.
    IDirect3D8 *second = create8(220);
    if (!second) { P("FAIL: second Direct3DCreate8 returned null"); Release(d3d); return 4; }
    Release(second);
    void **vt = *(void ***)d3d;
    UINT count = ((GetAdapterCount_t)vt[GetAdapterCountSlot])(d3d);
    P("GetAdapterCount -> %u", count);
    if (count == 0) { P("FAIL: no adapter"); Release(d3d); return 5; }
    HMONITOR monitor = ((GetAdapterMonitor_t)vt[GetAdapterMonitorSlot])(d3d, 0);
    MONITORINFO mi = { sizeof(mi) };
    BOOL validMonitor = monitor && GetMonitorInfoA(monitor, &mi);
    P("slot 14 GetAdapterMonitor -> %p valid=%d", (void *)monitor, validMonitor);
    if (!validMonitor) { P("FAIL: adapter monitor invalid"); Release(d3d); return 6; }

    CreateDevice_t createDevice = (CreateDevice_t)vt[CreateDeviceSlot];
    IDirect3DDevice8 *dev = NULL;
    HRESULT hr = E_FAIL;
    // Rayman used fullscreen at the desktop size with behaviour 0x44
    // (hardware vertex processing + FPU preserve); match that when asked.
    const DWORD flags[] = { (DWORD)(fullscreen ? 0x44 : 0x20), (DWORD)(fullscreen ? 0x44 : 0x40) };
    for (int i = 0; i < 2; i++) {
        D3DPRESENT_PARAMETERS pp = {};
        pp.BackBufferWidth = fullscreen ? GetSystemMetrics(SM_CXSCREEN) : 320;
        pp.BackBufferHeight = fullscreen ? GetSystemMetrics(SM_CYSCREEN) : 240;
        pp.BackBufferCount = 1; pp.SwapEffect = 1;
        // UNKNOWN (0) is windowed-only in D3D9; fullscreen needs a real format.
        // Rayman's recorded request was A8R8G8B8 (21) at 2880x1800 fullscreen.
        pp.BackBufferFormat = fullscreen ? 21 : 0;
        pp.hDeviceWindow = hwnd; pp.Windowed = fullscreen ? FALSE : TRUE;
        // UNKNOWN (0) requests the desktop format for a windowed device.
        // A shim throw propagates out of the COM call, so catch it here and keep
        // the message text instead of letting terminate() erase it.
        try {
            hr = createDevice(d3d, 0, 1, hwnd, flags[i], &pp, &dev);
            P("slot 15 CreateDevice(HAL, %s) -> hr=0x%08lX device=%p present=%ux%u windowed=%d flags=0x%lX",
              i == 0 ? "SW" : "HW", (unsigned long)hr, (void *)dev, pp.BackBufferWidth,
              pp.BackBufferHeight, (int)pp.Windowed, (unsigned long)flags[i]);
        } catch (const std::system_error &error) {
            P("slot 15 CreateDevice(HAL, %s) -> threw std::system_error code=%d (%s) what=\"%s\"",
              i == 0 ? "SW" : "HW", error.code().value(), error.code().message().c_str(), error.what());
            hr = E_FAIL; dev = NULL; break;
        } catch (const std::exception &error) {
            P("slot 15 CreateDevice(HAL, %s) -> threw %s what=\"%s\"",
              i == 0 ? "SW" : "HW", typeid(error).name(), error.what());
            hr = E_FAIL; dev = NULL; break;
        } catch (...) {
            P("slot 15 CreateDevice(HAL, %s) -> threw an exception of unknown type", i == 0 ? "SW" : "HW");
            hr = E_FAIL; dev = NULL; break;
        }
        if (SUCCEEDED(hr) && dev) break;
        Release(dev); dev = NULL;
    }
    if (FAILED(hr) || !dev) { P("FAIL: no device"); Release(d3d); return 7; }

    void **dvt = *(void ***)dev;
    typedef HRESULT (__stdcall *SetVS_t)(IDirect3DDevice8 *, DWORD);
    typedef HRESULT (__stdcall *Scene_t)(IDirect3DDevice8 *);
    typedef HRESULT (__stdcall *DrawUP_t)(IDirect3DDevice8 *, UINT, UINT, const void *, UINT);
    typedef HRESULT (__stdcall *SetTransform_t)(IDirect3DDevice8 *, UINT, const float *);
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    hr = ((SetTransform_t)dvt[37])(dev, 2, identity); // VIEW
    P("SetTransform(VIEW) -> 0x%08lX", (unsigned long)hr);
    bool ok = SUCCEEDED(hr);
    hr = ((SetVS_t)dvt[76])(dev, 0x44); // XYZRHW | DIFFUSE
    P("SetVertexShader(FVF 0x44) -> 0x%08lX", (unsigned long)hr);
    ok = ok && SUCCEEDED(hr);
    hr = ((Scene_t)dvt[34])(dev);
    P("BeginScene -> 0x%08lX", (unsigned long)hr);
    ok = ok && SUCCEEDED(hr);
    if (SUCCEEDED(hr)) {
        struct Vertex { float x, y, z, rhw; DWORD color; };
        const Vertex triangle[3] = {{10,10,0.5f,1,0xffff0000},
                                   {160,220,0.5f,1,0xff00ff00},
                                   {310,10,0.5f,1,0xff0000ff}};
        hr = ((DrawUP_t)dvt[72])(dev, 4, 1, triangle, sizeof(Vertex));
        P("DrawPrimitiveUP -> 0x%08lX", (unsigned long)hr);
        ok = ok && SUCCEEDED(hr);
        hr = ((Scene_t)dvt[35])(dev);
        P("EndScene -> 0x%08lX", (unsigned long)hr);
        ok = ok && SUCCEEDED(hr);
    }
    if (ok) {
        typedef HRESULT (__stdcall *Present_t)(IDirect3DDevice8 *, const RECT *, const RECT *, HWND, const RGNDATA *);
        hr = ((Present_t)dvt[15])(dev, NULL, NULL, NULL, NULL);
        P("Present -> 0x%08lX", (unsigned long)hr);
        ok = SUCCEEDED(hr);
    }
    if (lifecycle) RunLifecycle(hwnd, idleMillis, stress);
    if (reuseWindow) {
        // PR #144's shape: a second device on the same HWND re-runs window setup.
        IDirect3DDevice8 *second = NULL;
        D3DPRESENT_PARAMETERS pp = {};
        pp.BackBufferWidth = 320; pp.BackBufferHeight = 240;
        pp.BackBufferCount = 1; pp.SwapEffect = 1;
        pp.hDeviceWindow = hwnd; pp.Windowed = TRUE;
        try {
            hr = createDevice(d3d, 0, 1, hwnd, 0x44, &pp, &second);
            P("reuse-window: second CreateDevice on same hwnd -> hr=0x%08lX device=%p",
              (unsigned long)hr, (void *)second);
            Release(second);
            P("reuse-window: second device released");
        } catch (const std::exception &error) {
            P("reuse-window: second CreateDevice threw %s what=\"%s\"", typeid(error).name(), error.what());
        } catch (...) {
            P("reuse-window: second CreateDevice threw an exception of unknown type");
        }
        if (lifecycle) PumpMessages(600);
    }
    Release(dev);
    Release(d3d);
    P("%s: device creation and triangle submission", ok ? "PASS" : "FAIL");
    return ok ? 0 : 8;
}

static int CheckExceptionRecord(EXCEPTION_POINTERS *info, DWORD code, DWORD flags, volatile bool *valid, int action)
{
    EXCEPTION_RECORD *record = info->ExceptionRecord;
    if (record->ExceptionCode != code) return EXCEPTION_CONTINUE_SEARCH;
    DWORD observedError = GetLastError();
    *valid = (record->ExceptionFlags & EXCEPTION_NONCONTINUABLE) == flags &&
        record->NumberParameters == 2 && record->ExceptionInformation[0] == 0x13572468 &&
        record->ExceptionInformation[1] == 0x24681357 && observedError == 0x12345678;
    P("Exception record: code=0x%08lX flags=0x%08lX count=%lu args=0x%08lX,0x%08lX error=0x%08lX valid=%d",
      record->ExceptionCode, record->ExceptionFlags, record->NumberParameters,
      (DWORD)record->ExceptionInformation[0], (DWORD)record->ExceptionInformation[1], observedError, *valid);
    return action;
}

__declspec(noinline) static void ThrowCppException(void) { throw 42; }

static bool CheckCppException(void)
{
    try { ThrowCppException(); }
    catch (int value) { return value == 42; }
    return false;
}

static bool CheckExceptionForwarding(void)
{
    const ULONG_PTR args[2] = { 0x13572468, 0x24681357 };
    // Continuing restores register context; filter results must be reloaded from memory.
    volatile bool noncontinuable = false, continuable = false;
    bool continued = false, caught = false;
    SetLastError(0x12345678);
    __try { RaiseException(0xE0420001, EXCEPTION_NONCONTINUABLE, 2, args); }
    __except (CheckExceptionRecord(GetExceptionInformation(), 0xE0420001, EXCEPTION_NONCONTINUABLE,
                                  &noncontinuable, EXCEPTION_EXECUTE_HANDLER)) { caught = true; }
    SetLastError(0x12345678);
    __try { RaiseException(0xE0420002, 0, 2, args); continued = true; }
    __except (CheckExceptionRecord(GetExceptionInformation(), 0xE0420002, 0,
                                  &continuable, EXCEPTION_CONTINUE_EXECUTION)) { }
    bool cpp = CheckCppException();
    P("Exception forwarding: caught=%d noncontinuable=%d continuable=%d continued=%d cpp=%d",
      caught, noncontinuable, continuable, continued, cpp);
    return caught && noncontinuable && continuable && continued && cpp;
}

int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::set_terminate(OnTerminate);
    SetUnhandledExceptionFilter(LogUnhandled);
    AddVectoredExceptionHandler(1, FirstChance);
    logFile = fopen("abi.log", "w");
    if (!logFile) return 1;
    P("harness pid=%lu tid=%lu", (unsigned long)GetCurrentProcessId(),
      (unsigned long)(harnessTid = GetCurrentThreadId()));
    const char *dll = argc > 1 ? argv[1] : ".\\d3d8_real.dll";
    HMODULE library = LoadLibraryA(dll);
    if (!library) {
        P("FAIL: LoadLibrary(%s) error=%lu", dll, GetLastError());
        fclose(logFile); return 2;
    }
    WNDCLASSA wc = {};
    int reentrant = HasArg(argc, argv, "--reentrant");
    wc.lpfnWndProc = reentrant ? ReentrantProc : DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "D3D8AbiTest";
    ATOM atom = RegisterClassA(&wc);
    HWND hwnd = atom ? CreateWindowExA(0, wc.lpszClassName, "ABI test", WS_OVERLAPPEDWINDOW,
                                     0, 0, 320, 240, NULL, NULL, wc.hInstance, NULL) : NULL;
    int lifecycle = HasArg(argc, argv, "--lifecycle");
    int fullscreen = HasArg(argc, argv, "--fullscreen");
    // --idle holds the post-bridge state for the duration the game survived.
    int idleMillis = HasArg(argc, argv, "--idle") ? 8000 : 600;
    int stress = HasArg(argc, argv, "--stress");
    int reuseWindow = HasArg(argc, argv, "--reuse-window");
    int result;
    if (!hwnd) { P("FAIL: window creation error=%lu", GetLastError()); result = 9; }
    else {
        result = Check(library, hwnd, lifecycle, fullscreen, idleMillis, stress, reuseWindow);
        if (result == 0 && !CheckExceptionForwarding()) result = 10;
        if (lifecycle) DestroyLifecycleWindow(hwnd); else DestroyWindow(hwnd);
    }
    if (atom) UnregisterClassA(wc.lpszClassName, wc.hInstance);
    // Patched vtables retain proxy pointers until process exit; do not unload.
    bool explicitExit = result == 0 && HasArg(argc, argv, "--exit-process");
    if (explicitExit) P("PASS: forwarding ExitProcess(73)");
    fclose(logFile);
    if (explicitExit) ExitProcess(73);
    return result;
}
