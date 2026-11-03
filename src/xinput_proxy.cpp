// xinput proxy: presents a DirectInput game controller (DualSense etc.) as an XInput pad,
// so rayman3-input-fix (which consumes XInput) can feed Rayman 3 a device it understands.
// User-mode only: no driver, no install. Drop-in xinput1_4.dll / XINPUT9_1_0.dll beside the game.
//
// Mapping (DirectInput GUID -> XInput):
//   lX/lY = left stick, lRx/lRy = right stick, lZ = left trigger, lRz = right trigger
//   buttons 0..3 = Square/Cross/Circle/Triangle -> X/Y/A/B, 4/5 = LB/RB, 6/7 = LT/RT,
//   8/9 = Back/Start, 10/11 = LS/RS, POV hat -> dpad
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

// ---- XInput ABI (hand-rolled so we do not depend on xinput.h) ----
typedef struct { WORD wButtons; BYTE bLeftTrigger; BYTE bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XGAMEPAD;
typedef struct { DWORD dwPacketNumber; XGAMEPAD Gamepad; } XSTATE;
typedef struct { BYTE Type; BYTE SubType; WORD Flags; XGAMEPAD Gamepad; WORD wVibrationLeftMotorSpeed, wVibrationRightMotorSpeed; } XCAPS;
typedef struct { WORD wButtons; BYTE bLeftTrigger; BYTE bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XKEYSTROKE_GAMEPAD_;

#define XB_DPAD_UP 0x0001
#define XB_DPAD_DOWN 0x0002
#define XB_DPAD_LEFT 0x0004
#define XB_DPAD_RIGHT 0x0008
#define XB_START 0x0010
#define XB_BACK 0x0020
#define XB_LEFT_THUMB 0x0040
#define XB_RIGHT_THUMB 0x0080
#define XB_LEFT_SHOULDER 0x0100
#define XB_RIGHT_SHOULDER 0x0200
#define XB_A 0x1000
#define XB_B 0x2000
#define XB_X 0x4000
#define XB_Y 0x8000

static IDirectInput8A *g_di = NULL;
static IDirectInputDevice8A *g_dev = NULL;
static int g_tried = 0;
static DWORD g_packet = 0;

static BOOL CALLBACK FirstPad(LPCDIDEVICEINSTANCEA inst, LPVOID ctx) {
    *(GUID *)ctx = inst->guidInstance;
    return DIENUM_STOP;
}

static void OpenPad(void) {
    if (g_dev || g_tried) return;
    g_tried = 1;
    typedef HRESULT(WINAPI *PFN)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
    HMODULE me = GetModuleHandleA("dinput8.dll");
    if (!me) me = LoadLibraryA("dinput8.dll");
    if (!me) return;
    PFN create = (PFN)GetProcAddress(me, "DirectInput8Create");
    if (!create) return;
    if (FAILED(create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8A, (LPVOID *)&g_di, NULL)) || !g_di) return;

    GUID g;
    if (FAILED(g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, FirstPad, &g, DIEDFL_ATTACHEDONLY))) return;
    if (IsEqualGUID(g, GUID_NULL)) return;                      // no pad attached
    if (FAILED(g_di->CreateDevice(g, &g_dev, NULL)) || !g_dev) return;
    if (FAILED(g_dev->SetDataFormat(&c_dfDIJoystick2))) { g_dev->Release(); g_dev = NULL; return; }
    // background + nonexclusive so we need no window handle from the host game
    if (FAILED(g_dev->SetCooperativeLevel(GetForegroundWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
        g_dev->Release(); g_dev = NULL; return;
    }
    g_dev->Acquire();
}

static SHORT Axis(LONG v, LONG centre, LONG range) {
    LONG d = v - centre;
    if (d < -range) d = -range;
    if (d > range) d = range;
    return (SHORT)(d * 32767 / (range ? range : 1));
}

static BYTE Trigger(LONG v, LONG lo, LONG hi) {
    LONG span = hi - lo;
    LONG t = v - lo;
    if (t < 0) t = 0;
    if (t > span) t = span;
    return (BYTE)(span ? t * 255 / span : 0);
}

static BOOL ReadPad(XGAMEPAD *out) {
    OpenPad();
    if (!g_dev) return FALSE;
    DIJOYSTATE2 js;
    HRESULT hr = g_dev->GetDeviceState(sizeof(js), &js);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) { g_dev->Acquire(); hr = g_dev->GetDeviceState(sizeof(js), &js); }
    if (FAILED(hr)) return FALSE;

    ZeroMemory(out, sizeof(*out));
    // ranges: DualSense reports 0..65535 on sticks, 0..255 on triggers; both handled by centre/range
    out->sThumbLX = Axis(js.lX, 32767, 32767);
    out->sThumbLY = (SHORT)-Axis(js.lY, 32767, 32767);   // DInput Y is inverted vs XInput
    out->sThumbRX = Axis(js.lRx, 32767, 32767);
    out->sThumbRY = (SHORT)-Axis(js.lRy, 32767, 32767);
    out->bLeftTrigger = Trigger(js.lZ, 0, 255);
    out->bRightTrigger = Trigger(js.lRz, 0, 255);

    if (js.rgbButtons[0]) out->wButtons |= XB_X;          // Square
    if (js.rgbButtons[1]) out->wButtons |= XB_A;          // Cross
    if (js.rgbButtons[2]) out->wButtons |= XB_B;          // Circle
    if (js.rgbButtons[3]) out->wButtons |= XB_Y;          // Triangle
    if (js.rgbButtons[4]) out->wButtons |= XB_LEFT_SHOULDER;
    if (js.rgbButtons[5]) out->wButtons |= XB_RIGHT_SHOULDER;
    if (js.rgbButtons[6]) out->bLeftTrigger = 255;
    if (js.rgbButtons[7]) out->bRightTrigger = 255;
    if (js.rgbButtons[8]) out->wButtons |= XB_BACK;
    if (js.rgbButtons[9]) out->wButtons |= XB_START;
    if (js.rgbButtons[10]) out->wButtons |= XB_LEFT_THUMB;
    if (js.rgbButtons[11]) out->wButtons |= XB_RIGHT_THUMB;

    DWORD pov = js.rgdwPOV[0];
    if (pov != 0xFFFFFFFF) {
        if (pov > 29250 || pov < 6750) out->wButtons |= XB_DPAD_UP;
        if (pov > 2250 && pov < 15750) out->wButtons |= XB_DPAD_RIGHT;
        if (pov > 11250 && pov < 24750) out->wButtons |= XB_DPAD_DOWN;
        if (pov > 20250 && pov < 33750) out->wButtons |= XB_DPAD_LEFT;
    }
    return TRUE;
}

extern "C" {

DWORD WINAPI XInputGetState(DWORD idx, XSTATE *state) {
    if (idx != 0 || !state) return ERROR_DEVICE_NOT_CONNECTED;
    if (!ReadPad(&state->Gamepad)) return ERROR_DEVICE_NOT_CONNECTED;
    state->dwPacketNumber = ++g_packet;
    return ERROR_SUCCESS;
}
DWORD WINAPI XInputSetState(DWORD idx, void *vib) { (void)vib; return idx == 0 && g_dev ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI XInputEnable(BOOL enable) { (void)enable; return 0; }
DWORD WINAPI XInputGetCapabilities(DWORD idx, DWORD flags, XCAPS *caps) {
    (void)flags;
    if (idx != 0) return ERROR_DEVICE_NOT_CONNECTED;
    OpenPad();
    if (!g_dev) return ERROR_DEVICE_NOT_CONNECTED;
    ZeroMemory(caps, sizeof(*caps));
    caps->Type = 1;   // XINPUT_DEVTYPE_GAMEPAD
    caps->SubType = 2; // XINPUT_DEVSUBTYPE_GAMEPAD
    caps->Flags = 4;   // XINPUT_CAPS_FFB_SUPPORTED (harmless)
    caps->Gamepad.wButtons = 0xF3FF;
    caps->Gamepad.bLeftTrigger = caps->Gamepad.bRightTrigger = 255;
    caps->Gamepad.sThumbLX = caps->Gamepad.sThumbLY = caps->Gamepad.sThumbRX = caps->Gamepad.sThumbRY = (SHORT)-64;
    return ERROR_SUCCESS;
}
DWORD WINAPI XInputGetKeystroke(DWORD idx, DWORD user, void *ks) { (void)idx; (void)user; (void)ks; return ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI XInputGetBatteryInformation(DWORD idx, BYTE devtype, void *batt) { (void)devtype; (void)batt; return idx == 0 && g_dev ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD idx, GUID *r, GUID *c) { (void)r; (void)c; return idx == 0 && g_dev ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI XInputGetStateEx(DWORD idx, XSTATE *state) { return XInputGetState(idx, state); }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    if (reason == DLL_PROCESS_DETACH) {
        if (g_dev) { g_dev->Unacquire(); g_dev->Release(); g_dev = NULL; }
        if (g_di) { g_di->Release(); g_di = NULL; }
    }
    return TRUE;
}
}
