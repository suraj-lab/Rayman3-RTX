// Minimal DirectInput8 probe: does the local dinput8.dll (the input fix) enumerate any device?
// Build: see build_dinput_probe.cmd (x86). Run it from a folder that contains the dinput8.dll under test.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>

static BOOL CALLBACK EnumCb(LPCDIDEVICEINSTANCEA inst, LPVOID ctx) {
    (void)ctx;
    printf("  device: \"%s\"  product=\"%s\"\n", inst->tszInstanceName, inst->tszProductName);
    return DIENUM_CONTINUE;
}

int main(void) {
    HINSTANCE h = LoadLibraryA("dinput8.dll");
    if (!h) { printf("LoadLibrary(dinput8.dll) failed: %lu\n", GetLastError()); return 1; }
    char path[MAX_PATH]; GetModuleFileNameA(h, path, MAX_PATH);
    printf("dinput8.dll in use: %s\n", path);

    typedef HRESULT (WINAPI *PFN)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
    PFN create = (PFN)GetProcAddress(h, "DirectInput8Create");
    if (!create) { printf("no DirectInput8Create export\n"); return 1; }

    IDirectInput8A *di = NULL;
    HRESULT hr = create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8A, (LPVOID *)&di, NULL);
    printf("DirectInput8Create -> 0x%08lX\n", (unsigned long)hr);
    if (FAILED(hr)) return 1;

    int n = 0;
    struct Ctx { int *n; } ctx = { &n };
    printf("EnumDevices(DI8DEVCLASS_GAMECTRL):\n");
    hr = di->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumCb, &ctx, DIEDFL_ATTACHEDONLY);
    printf("  enum hr=0x%08lX\n", (unsigned long)hr);
    printf("  (see above for devices; the callback prints one line each)\n");

    // Also try the raw-joystick class in case the fix hides pads behind it
    printf("EnumDevices(DI8DEVCLASS_ALL):\n");
    di->EnumDevices(DI8DEVCLASS_ALL, EnumCb, &ctx, DIEDFL_ATTACHEDONLY);

    di->Release();
    return 0;
}
