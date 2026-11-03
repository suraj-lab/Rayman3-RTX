// Verify the xinput proxy: load xinput9_1_0.dll from this folder and report pad state for a few seconds.
#include <windows.h>
#include <stdio.h>
typedef struct { WORD wButtons; BYTE bLeftTrigger; BYTE bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XGAMEPAD;
typedef struct { DWORD dwPacketNumber; XGAMEPAD Gamepad; } XSTATE;

int main(int argc, char **argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 3;
    HMODULE h = LoadLibraryA("xinput9_1_0.dll");
    if (!h) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    char p[MAX_PATH]; GetModuleFileNameA(h, p, MAX_PATH);
    printf("using %s\n", p);
    typedef DWORD (WINAPI *GS)(DWORD, XSTATE *);
    GS get = (GS)GetProcAddress(h, "XInputGetState");
    if (!get) { printf("no XInputGetState export\n"); return 1; }
    XSTATE s;
    for (int i = 0; i < secs * 4; i++) {
        DWORD rc = get(0, &s);
        if (rc == 0)
            printf("rc=0 buttons=0x%04X LX=%6d LY=%6d RX=%6d RY=%6d LT=%3u RT=%3u\n",
                   s.Gamepad.wButtons, s.Gamepad.sThumbLX, s.Gamepad.sThumbLY,
                   s.Gamepad.sThumbRX, s.Gamepad.sThumbRY, s.Gamepad.bLeftTrigger, s.Gamepad.bRightTrigger);
        else
            printf("rc=%lu (not connected)\n", (unsigned long)rc);
        Sleep(250);
    }
    return 0;
}
