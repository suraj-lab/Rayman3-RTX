// Dump what DirectInput exposes for each game controller: capabilities + objects (axes/buttons/hats).
// Same build as dinput_probe (x86). Run with or without a local dinput8.dll to compare.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>

static const char *TypeName(DWORD t) {
    switch (t) {
    case DIDFT_ABSAXIS: return "abs-axis";
    case DIDFT_RELAXIS: return "rel-axis";
    case DIDFT_PSHBUTTON: return "button";
    case DIDFT_TGLBUTTON: return "toggle";
    case DIDFT_POV: return "pov";
    default: return "other";
    }
}

static BOOL CALLBACK ObjCb(LPCDIDEVICEOBJECTINSTANCEA o, LPVOID ctx) {
    (void)ctx;
    printf("      %-9s offset=%-3lu type=0x%08lX flags=0x%08lX  \"%s\"\n",
           TypeName(o->dwType & DIDFT_GETTYPE(0xFFFFFFFF) & (DIDFT_ABSAXIS | DIDFT_RELAXIS | DIDFT_PSHBUTTON | DIDFT_TGLBUTTON | DIDFT_POV)),
           (unsigned long)o->dwOfs, (unsigned long)o->dwType, (unsigned long)o->dwFlags, o->tszName);
    return DIENUM_CONTINUE;
}

static BOOL CALLBACK EnumCb(LPCDIDEVICEINSTANCEA inst, LPVOID ctx) {
    IDirectInput8A *di = (IDirectInput8A *)ctx;
    printf("device: \"%s\"\n", inst->tszInstanceName);
    IDirectInputDevice8A *dev = NULL;
    if (FAILED(di->CreateDevice(inst->guidInstance, &dev, NULL)) || !dev) { printf("    CreateDevice failed\n"); return DIENUM_CONTINUE; }
    DIDEVCAPS caps = { sizeof(caps) };
    if (SUCCEEDED(dev->GetCapabilities(&caps)))
        printf("    caps: axes=%lu buttons=%lu povs=%lu flags=0x%08lX\n",
               (unsigned long)caps.dwAxes, (unsigned long)caps.dwButtons, (unsigned long)caps.dwPOVs, (unsigned long)caps.dwFlags);
    dev->EnumObjects(ObjCb, NULL, DIDFT_ALL);
    dev->Release();
    return DIENUM_CONTINUE;
}

int main(void) {
    HINSTANCE h = LoadLibraryA("dinput8.dll");
    char path[MAX_PATH]; GetModuleFileNameA(h, path, MAX_PATH);
    printf("dinput8.dll: %s\n", path);
    typedef HRESULT (WINAPI *PFN)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
    PFN create = (PFN)GetProcAddress(h, "DirectInput8Create");
    IDirectInput8A *di = NULL;
    if (FAILED(create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8A, (LPVOID *)&di, NULL))) return 1;
    di->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumCb, di, DIEDFL_ATTACHEDONLY);
    di->Release();
    return 0;
}
