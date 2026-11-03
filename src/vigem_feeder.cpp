// Rayman 3 only, launch-scoped virtual Xbox 360 pad.
//
// Reads the DualSense/DS4 over raw HID and pushes XInput state through ViGEmBus, but ONLY while
// Rayman3.exe is alive; the virtual pad is removed the moment the game exits, so no other
// application ever sees it. While another window is foreground the feeder sends neutral state.
//
// usage: vigem_feeder.exe [--exe Rayman3.exe] [--test SECONDS]
//   --test  create the pad, feed a slow circle for SECONDS, then remove it (no game needed)
//
// ViGEmClient.dll is loaded dynamically (exe folder, then system dirs), so no import library is needed.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

// ---- ViGEm ABI (hand-declared; see ViGEm/Client.h, MIT) ----
typedef void *PVIGEM_CLIENT;
typedef void *PVIGEM_TARGET;
typedef unsigned long VIGEM_ERROR;
#define VIGEM_ERROR_NONE 0x20000000UL

typedef struct {
    WORD wButtons;
    BYTE bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} XUSB_REPORT;

static PVIGEM_CLIENT(__cdecl *p_alloc)(void);
static void (__cdecl *p_free)(PVIGEM_CLIENT);
static VIGEM_ERROR(__cdecl *p_connect)(PVIGEM_CLIENT);
static void (__cdecl *p_disconnect)(PVIGEM_CLIENT);
static PVIGEM_TARGET(__cdecl *p_x360_alloc)(void);
static void (__cdecl *p_target_free)(PVIGEM_TARGET);
static VIGEM_ERROR(__cdecl *p_target_add)(PVIGEM_CLIENT, PVIGEM_TARGET);
static VIGEM_ERROR(__cdecl *p_target_remove)(PVIGEM_CLIENT, PVIGEM_TARGET);
static VIGEM_ERROR(__cdecl *p_x360_update)(PVIGEM_CLIENT, PVIGEM_TARGET, XUSB_REPORT);

static int LoadViGEm(void) {
    HMODULE m = LoadLibraryA("ViGEmClient.dll");
    if (!m) return 0;
    p_alloc = (PVIGEM_CLIENT(__cdecl *)(void))GetProcAddress(m, "vigem_alloc");
    p_free = (void (__cdecl *)(PVIGEM_CLIENT))GetProcAddress(m, "vigem_free");
    p_connect = (VIGEM_ERROR(__cdecl *)(PVIGEM_CLIENT))GetProcAddress(m, "vigem_connect");
    p_disconnect = (void (__cdecl *)(PVIGEM_CLIENT))GetProcAddress(m, "vigem_disconnect");
    p_x360_alloc = (PVIGEM_TARGET(__cdecl *)(void))GetProcAddress(m, "vigem_target_x360_alloc");
    p_target_free = (void (__cdecl *)(PVIGEM_TARGET))GetProcAddress(m, "vigem_target_free");
    p_target_add = (VIGEM_ERROR(__cdecl *)(PVIGEM_CLIENT, PVIGEM_TARGET))GetProcAddress(m, "vigem_target_add");
    p_target_remove = (VIGEM_ERROR(__cdecl *)(PVIGEM_CLIENT, PVIGEM_TARGET))GetProcAddress(m, "vigem_target_remove");
    p_x360_update = (VIGEM_ERROR(__cdecl *)(PVIGEM_CLIENT, PVIGEM_TARGET, XUSB_REPORT))GetProcAddress(m, "vigem_target_x360_update");
    return p_alloc && p_free && p_connect && p_disconnect && p_x360_alloc && p_target_free &&
           p_target_add && p_target_remove && p_x360_update;
}

// ---- pad reading (HID) ----
static HANDLE g_hid = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_lock;
static XUSB_REPORT g_state;
static volatile LONG g_readerRun = 1;
static volatile LONG g_packets = 0;

static HANDLE OpenPad(void) {
    GUID guid;
    HidD_GetHidGuid(&guid);
    HDEVINFO set = SetupDiGetClassDevsA(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA did = { sizeof(did) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, NULL, &guid, i, &did); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailA(set, &did, NULL, 0, &need, NULL);
        if (!need) continue;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_A det = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(need);
        det->cbSize = sizeof(*det);
        if (!SetupDiGetDeviceInterfaceDetailA(set, &did, det, need, NULL, NULL)) { free(det); continue; }
        HANDLE h = CreateFileA(det->DevicePath, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, NULL);
        if (h == INVALID_HANDLE_VALUE)
            h = CreateFileA(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        free(det);
        if (h == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES at = { sizeof(at) };
        if (HidD_GetAttributes(h, &at) && (at.VendorID == 0x054C) &&
            (at.ProductID == 0x0CE6 || at.ProductID == 0x05C4 || at.ProductID == 0x09CC)) {
            SetupDiDestroyDeviceInfoList(set);
            return h;
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return INVALID_HANDLE_VALUE;
}

// DS4/DualSense input report payload (after the report ID): sticks 1..4, buttons 5..7, triggers 8..9
static void Parse(const BYTE *p, XUSB_REPORT *o) {
    ZeroMemory(o, sizeof(*o));
    o->sThumbLX = (SHORT)(((int)p[1] - 128) * 258);
    o->sThumbLY = (SHORT)(((int)p[2] - 128) * -258);
    o->sThumbRX = (SHORT)(((int)p[3] - 128) * 258);
    o->sThumbRY = (SHORT)(((int)p[4] - 128) * -258);
    o->bLeftTrigger = p[8];
    o->bRightTrigger = p[9];
    BYTE b1 = p[5], b2 = p[6];
    if (b1 & 0x10) o->wButtons |= 0x4000;   // Square -> X
    if (b1 & 0x20) o->wButtons |= 0x1000;   // Cross  -> A
    if (b1 & 0x40) o->wButtons |= 0x2000;   // Circle -> B
    if (b1 & 0x80) o->wButtons |= 0x8000;   // Triangle -> Y
    if (b1 & 0x01) o->wButtons |= 0x0100;   // L1
    if (b1 & 0x02) o->wButtons |= 0x0200;   // R1
    if (b2 & 0x01) o->wButtons |= 0x0020;   // Share -> Back
    if (b2 & 0x02) o->wButtons |= 0x0010;   // Options -> Start
    if (b2 & 0x04) o->wButtons |= 0x0040;   // L3
    if (b2 & 0x08) o->wButtons |= 0x0080;   // R3
    if (b2 & 0x40) o->wButtons |= 0x0001;   // PS -> guide-ish
    BYTE hat = b1 & 0x0F;
    switch (hat) {
    case 0: o->wButtons |= 0x0001; break;
    case 1: o->wButtons |= 0x0001 | 0x0008; break;
    case 2: o->wButtons |= 0x0008; break;
    case 3: o->wButtons |= 0x0002 | 0x0008; break;
    case 4: o->wButtons |= 0x0002; break;
    case 5: o->wButtons |= 0x0002 | 0x0004; break;
    case 6: o->wButtons |= 0x0004; break;
    case 7: o->wButtons |= 0x0001 | 0x0004; break;
    default: break;
    }
}

static DWORD WINAPI Reader(LPVOID) {
    BYTE buf[128];
    DWORD got = 0;
    OVERLAPPED ov;
    while (g_readerRun) {
        ZeroMemory(&ov, sizeof(ov));
        ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        DWORD n = 0;
        if (!ReadFile(g_hid, buf, sizeof(buf), &n, &ov)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(ov.hEvent, 200) == WAIT_OBJECT_0) GetOverlappedResult(g_hid, &ov, &n, FALSE);
                else n = 0;
            } else n = 0;
        }
        CloseHandle(ov.hEvent);
        if (n >= 10) {
            const BYTE *payload = buf;
            size_t len = n;
            if (len > 2 && buf[0] == 0x31) { payload = buf + 2; }   // BT: report id + 2-byte header
            else if (buf[0] != 0x01 && buf[0] != 0x31) { continue; }
            XUSB_REPORT st;
            Parse(payload, &st);
            EnterCriticalSection(&g_lock);
            g_state = st;
            InterlockedIncrement(&g_packets);
            LeaveCriticalSection(&g_lock);
        }
    }
    return 0;
}

static int IsGameForeground(DWORD pid) {
    HWND h = GetForegroundWindow();
    if (!h) return 0;
    DWORD wpid = 0;
    GetWindowThreadProcessId(h, &wpid);
    return wpid == pid;
}

int main(int argc, char **argv) {
    const char *exeName = "Rayman3.exe";
    int testSecs = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--exe") && i + 1 < argc) exeName = argv[++i];
        else if (!strcmp(argv[i], "--test") && i + 1 < argc) testSecs = atoi(argv[++i]);
    }
    InitializeCriticalSection(&g_lock);

    g_hid = OpenPad();
    if (g_hid == INVALID_HANDLE_VALUE) { printf("no DualSense/DS4 found\n"); return 2; }
    printf("pad opened\n");
    CreateThread(NULL, 0, Reader, NULL, 0, NULL);

    if (!LoadViGEm()) { printf("ViGEmClient.dll not found (install ViGEmBus, keep ViGEmClient.dll beside this exe)\n"); return 3; }
    PVIGEM_CLIENT cl = p_alloc();
    if (!cl) { printf("vigem_alloc failed\n"); return 4; }
    VIGEM_ERROR e = p_connect(cl);
    if (e != VIGEM_ERROR_NONE) { printf("vigem_connect failed 0x%08lX (is ViGEmBus installed?)\n", e); p_free(cl); return 5; }
    PVIGEM_TARGET pad = p_x360_alloc();
    e = p_target_add(cl, pad);
    if (e != VIGEM_ERROR_NONE) { printf("target_add failed 0x%08lX\n", e); p_disconnect(cl); p_free(cl); return 6; }
    printf("virtual xbox pad added\n");

    DWORD gamePid = 0;
    DWORD start = GetTickCount();
    for (;;) {
        Sleep(4);
        if (testSecs) {
            double t = (GetTickCount() - start) / 500.0;
            XUSB_REPORT st;
            ZeroMemory(&st, sizeof(st));
            st.sThumbLX = (SHORT)(16000 * (t < 3.14159 ? 1 : 0));
            p_x360_update(cl, pad, st);
            if ((int)((GetTickCount() - start) / 1000) >= testSecs) break;
            continue;
        }
        if (!gamePid) {
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            PROCESSENTRY32 pe = { sizeof(pe) };
            if (snap != INVALID_HANDLE_VALUE) {
                if (Process32First(snap, &pe)) do {
                    if (!_stricmp(pe.szExeFile, exeName)) { gamePid = pe.th32ProcessID; break; }
                } while (Process32Next(snap, &pe));
                CloseHandle(snap);
            }
            if (!gamePid) { Sleep(400); if ((GetTickCount() - start) > 600000) break; continue; }
            printf("game pid=%lu, feeding pad\n", (unsigned long)gamePid);
            start = GetTickCount();
        }
        HANDLE gp = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, gamePid);
        if (gp && WaitForSingleObject(gp, 0) == WAIT_OBJECT_0) { CloseHandle(gp); break; }
        if (gp) CloseHandle(gp);
        XUSB_REPORT st;
        EnterCriticalSection(&g_lock);
        st = g_state;
        LeaveCriticalSection(&g_lock);
        if (!IsGameForeground(gamePid)) ZeroMemory(&st, sizeof(st));
        p_x360_update(cl, pad, st);
    }

    p_target_remove(cl, pad);
    p_target_free(pad);
    p_disconnect(cl);
    p_free(cl);
    InterlockedExchange(&g_readerRun, 0);
    if (g_hid != INVALID_HANDLE_VALUE) CloseHandle(g_hid);
    printf("virtual pad removed after %ld packets\n", (long)g_packets);
    return 0;
}
