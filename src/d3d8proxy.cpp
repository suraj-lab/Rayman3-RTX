// d3d8proxy — a D3D8.dll that forwards to d3d8_real.dll (dgVoodoo) and logs
// what Rayman 3 actually asks D3D8 to do.
//
// Why a proxy instead of an ASI: the game resolves d3d8.dll at load time, so
// these hooks are installed when Direct3DCreate8 returns.
//
// Answers one question: does the game submit world-space XYZ geometry with
// WORLD/VIEW/PROJECTION transforms, or pre-transformed XYZRHW only? If the
// latter, Remix can never find a camera and geometry must be reconstructed.

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <intrin.h>
#define REMIX_ALLOW_X86
#include "remix_c.h"

typedef struct IDirect3D8 IDirect3D8;
typedef struct IDirect3DDevice8 IDirect3DDevice8;
typedef struct IDirect3DVertexBuffer8 IDirect3DVertexBuffer8;
typedef UINT D3DFORMAT;
typedef UINT D3DDEVTYPE;
typedef UINT D3DTRANSFORMSTATETYPE;
typedef UINT D3DPRIMITIVETYPE;

typedef struct { float m[4][4]; } D3DMATRIX;

typedef struct {
    UINT BackBufferWidth, BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    UINT MultiSampleType;
    UINT SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed, EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz, FullScreen_PresentationInterval;
} D3DPRESENT_PARAMETERS;

#define D3DFVF_POSITION_MASK  0x00E
#define D3DFVF_XYZ            0x002
#define D3DFVF_XYZRHW         0x004
#define D3DFVF_NORMAL         0x010
#define D3DFVF_DIFFUSE        0x040
#define D3DFVF_TEXCOUNT_MASK  0x0F00
#define D3DFVF_TEXCOUNT_SHIFT 8
#define D3DTS_VIEW           2
#define D3DTS_PROJECTION     3
#define D3DTS_WORLD          256

// Zero-based COM slots, including IUnknown and GetAdapterMonitor (slot 14).
enum { ID3D8_CreateDevice = 15 };
static_assert(sizeof(void *) == 4, "This proxy targets x86");
static_assert(sizeof(D3DPRESENT_PARAMETERS) == 52, "D3D8 presentation ABI mismatch");
enum {
    DEV_SetTransform = 37,
    DEV_DrawPrimitive = 70, DEV_DrawIndexedPrimitive = 71, DEV_DrawPrimitiveUP = 72,
    DEV_SetVertexShader = 76, DEV_SetStreamSource = 83
};

typedef IDirect3D8 *(__stdcall *Direct3DCreate8_t)(UINT);
typedef HRESULT (__stdcall *CreateDevice_t)(IDirect3D8 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice8 **);
typedef HRESULT (__stdcall *SetTransform_t)(IDirect3DDevice8 *, D3DTRANSFORMSTATETYPE, const D3DMATRIX *);
typedef HRESULT (__stdcall *SetVS_t)(IDirect3DDevice8 *, DWORD);
typedef HRESULT (__stdcall *SetStream_t)(IDirect3DDevice8 *, UINT, IDirect3DVertexBuffer8 *, UINT);
typedef HRESULT (__stdcall *DrawPrim_t)(IDirect3DDevice8 *, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT (__stdcall *DrawPrimUP_t)(IDirect3DDevice8 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
typedef HRESULT (__stdcall *DrawIdx_t)(IDirect3DDevice8 *, D3DPRIMITIVETYPE, UINT, UINT, UINT, UINT);

static Direct3DCreate8_t o_Create8 = NULL;
static CreateDevice_t    o_CreateDevice = NULL;
static SetTransform_t    o_SetTransform = NULL;
static SetVS_t           o_SetVS = NULL;
static SetStream_t       o_SetStream = NULL;
static DrawPrim_t        o_DrawPrimitive = NULL;
static DrawPrimUP_t      o_DrawPrimitiveUP = NULL;
static DrawIdx_t         o_DrawIndexedPrimitive = NULL;

static FILE *g_log = NULL;
static DWORD g_fvf = 0;
static UINT  g_stride = 0;
static unsigned long g_draws = 0, g_drawsIdx = 0, g_drawsUP = 0;
static unsigned long g_texDraws = 0, g_noTexDraws = 0, g_shaderDraws = 0;
static DWORD g_seenFvf[160]; static UINT g_seenStride[160];
static unsigned long g_seenCount[160]; static int g_seenN = 0;
static int g_transformLogs = 0, g_createCalls = 0;
static bool g_xyzrhw = false, g_xyz = false;

static void L(const char *fmt, ...)
{
    if (!g_log) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

typedef void (WINAPI *RaiseException_t)(DWORD, DWORD, DWORD, const ULONG_PTR *);
static RaiseException_t o_RaiseException = NULL;

static void WINAPI hRaiseException(DWORD code, DWORD flags, DWORD count, const ULONG_PTR *args)
{
    DWORD savedError = GetLastError();
    void *caller = _ReturnAddress();
    // Reset before forwarding: handled exceptions can unwind past this hook.
    static thread_local bool logging = false;
    if (!logging) {
        logging = true;
        HMODULE owner = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCSTR)caller, &owner);
        char module[MAX_PATH] = "unknown";
        if (owner) GetModuleFileNameA(owner, module, MAX_PATH);
        L("EXCEPTION code=0x%08lX flags=0x%08lX count=%lu tid=%lu caller=%p module=%s offset=0x%08lX",
          code, flags, count, GetCurrentThreadId(), caller, module,
          owner ? (DWORD)((BYTE *)caller - (BYTE *)owner) : 0UL);
        __try {
            for (DWORD i = 0; args && i < count && i < EXCEPTION_MAXIMUM_PARAMETERS; i++)
                L("  exception arg[%lu]=0x%08lX", i, (unsigned long)args[i]);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            L("  exception arguments unreadable; forwarding original arguments unchanged");
        }
        logging = false;
    }
    SetLastError(savedError);
    o_RaiseException(code, flags, count, args);
}

typedef void (WINAPI *ExitProcess_t)(UINT);
static ExitProcess_t o_ExitProcess = NULL;

// x86 stack capture needs the hook's frame pointer, even under /O2.
#pragma optimize("y", off)
static void WINAPI hExitProcess(UINT code)
{
    DWORD savedError = GetLastError();
    L("EXITPROCESS code=%u tid=%lu caller=%p", code, GetCurrentThreadId(), _ReturnAddress());
    void *frames[16];
    USHORT count = CaptureStackBackTrace(0, 16, frames, NULL);
    L("  exit stack: %u Windows-captured frames (may be incomplete)", (unsigned)count);
    for (USHORT i = 0; i < count; i++) {
        HMODULE owner = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCSTR)frames[i], &owner);
        char module[MAX_PATH] = "unknown";
        if (owner) GetModuleFileNameA(owner, module, MAX_PATH);
        L("  exit frame[%u]=%p module=%s offset=0x%08lX", (unsigned)i, frames[i], module,
          owner ? (DWORD)((BYTE *)frames[i] - (BYTE *)owner) : 0UL);
    }
    SetLastError(savedError);
    o_ExitProcess(code);
}
#pragma optimize("", on)

static void HookHostImport(const char *importName, void *hook, void **original)
{
    if (*original) return;
    size_t nameBytes = strlen(importName) + 1;
    // Inspect only the OS-loaded host image, not an arbitrary PE file.
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return;
    DWORD size = nt->OptionalHeader.SizeOfImage;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || dir.VirtualAddress >= size || dir.Size > size - dir.VirtualAddress) return;
    for (DWORD pos = 0; pos + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= dir.Size; pos += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress + pos);
        if (!desc->Name) break;
        if (!desc->OriginalFirstThunk || !desc->FirstThunk || desc->OriginalFirstThunk >= size || desc->FirstThunk >= size) continue;
        IMAGE_THUNK_DATA32 *names = (IMAGE_THUNK_DATA32 *)(base + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA32 *iat = (IMAGE_THUNK_DATA32 *)(base + desc->FirstThunk);
        DWORD limit = (size - desc->OriginalFirstThunk) / sizeof(*names);
        DWORD slots = (size - desc->FirstThunk) / sizeof(*iat);
        if (slots < limit) limit = slots;
        for (DWORD i = 0; i < limit && names[i].u1.AddressOfData; i++) {
            DWORD name = names[i].u1.AddressOfData;
            if (IMAGE_SNAP_BY_ORDINAL32(name) || name >= size || sizeof(WORD) + nameBytes > size - name) continue;
            if (memcmp(base + name + sizeof(WORD), importName, nameBytes)) continue;
            DWORD old;
            if (!VirtualProtect(&iat[i].u1.Function, sizeof(DWORD), PAGE_READWRITE, &old)) {
                L("HostTrace: %s IAT protection failed error=%lu", importName, GetLastError()); return;
            }
            *original = (void *)iat[i].u1.Function;
            iat[i].u1.Function = (DWORD)hook;
            DWORD unused;
            if (!VirtualProtect(&iat[i].u1.Function, sizeof(DWORD), old, &unused))
                L("HostTrace: %s WARNING IAT protection restore failed error=%lu", importName, GetLastError());
            L("HostTrace: host %s import hooked; forwarding unchanged", importName);
            return;
        }
    }
    L("HostTrace: host has no named %s import; calls outside this import are not traced", importName);
}

static const char *PosName(DWORD fvf)
{
    switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZ:    return "XYZ";
    case D3DFVF_XYZRHW: return "XYZRHW";
    case 0x006:         return "XYZB1";
    case 0x008:         return "XYZB2";
    case 0x00A:         return "XYZB3";
    case 0x00C:         return "XYZB4";
    case 0x00E:         return "XYZB5";
    default:            return "none";
    }
}

static void Note(DWORD fvf, UINT stride)
{
    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) g_xyzrhw = true;
    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZ) g_xyz = true;
    for (int i = 0; i < g_seenN; i++)
        if (g_seenFvf[i] == fvf) { g_seenCount[i]++; return; }
    if (g_seenN < 160) {
        g_seenFvf[g_seenN] = fvf; g_seenStride[g_seenN] = stride; g_seenCount[g_seenN] = 1; g_seenN++;
        L("  new FVF 0x%08lX pos=%-6s tex=%lu nrm=%d dif=%d stride=%lu",
          (unsigned long)fvf, PosName(fvf),
          ((unsigned long)fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT,
          (fvf & D3DFVF_NORMAL) ? 1 : 0, (fvf & D3DFVF_DIFFUSE) ? 1 : 0, (unsigned long)stride);
    }
}

// The vendored API header defines the x86 calling convention and table layout;
// successful compilation does not establish x86 runtime support.
static void ProbeRemixApi(void)
{
    static bool attempted = false;
    if (attempted) return;
    attempted = true;
    HMODULE shim = GetModuleHandleA("d3d9.dll");
    if (!shim) { L("RemixAPI: no d3d9.dll in this process"); return; }
    PFN_remixapi_InitializeLibrary init = (PFN_remixapi_InitializeLibrary)GetProcAddress(shim, "remixapi_InitializeLibrary");
    if (!init) { L("RemixAPI: shim present but no remixapi_InitializeLibrary export"); return; }

    remixapi_Interface iface = {};
    remixapi_InitializeLibraryInfo info = {};
    info.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
    info.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
    remixapi_ErrorCode rc = init(&info, &iface);
    L("RemixAPI: InitializeLibrary -> %d  (0=SUCCESS, 11=NOT_INITIALIZED)", (int)rc);
    if (rc == REMIXAPI_ERROR_CODE_SUCCESS) {
        L("RemixAPI: Shutdown=%p CreateMaterial=%p CreateMesh=%p SetupCamera=%p DrawInstance=%p",
          (void *)iface.Shutdown, (void *)iface.CreateMaterial, (void *)iface.CreateMesh,
          (void *)iface.SetupCamera, (void *)iface.DrawInstance);
    }
}

static void DumpSummary(const char *why)
{
    L("");
    L("=== summary (%s) ===", why);
    L("  DrawPrimitive=%lu DrawIndexedPrimitive=%lu DrawPrimitiveUP=%lu", g_draws, g_drawsIdx, g_drawsUP);
    L("  draws with UVs=%lu  without UVs=%lu  vertex-shader handles=%lu",
      g_texDraws, g_noTexDraws, g_shaderDraws);
    L("  saw XYZRHW buffers: %s | saw XYZ buffers: %s",
      g_xyzrhw ? "YES (pre-transformed)" : "no", g_xyz ? "YES (world space)" : "no");
    for (int i = 0; i < g_seenN; i++)
        L("  FVF 0x%08lX pos=%-6s stride=%-3lu draws=%lu",
          (unsigned long)g_seenFvf[i], PosName(g_seenFvf[i]), (unsigned long)g_seenStride[i], g_seenCount[i]);
}

static void PatchSlot(void **vt, int idx, void *hook, void **orig)
{
    if (vt[idx] == hook) return; // Shared vtables may be returned more than once.
    DWORD old;
    if (!VirtualProtect(&vt[idx], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) {
        L("  PatchSlot(%d) VirtualProtect FAILED err=%lu", idx, GetLastError());
        return;
    }
    if (*orig == NULL) *orig = vt[idx];
    vt[idx] = hook;
    VirtualProtect(&vt[idx], sizeof(void *), old, &old);
}

static HRESULT __stdcall hSetTransform(IDirect3DDevice8 *d, D3DTRANSFORMSTATETYPE st, const D3DMATRIX *m)
{
    if (g_transformLogs < 40 && m && (st == D3DTS_WORLD || st == D3DTS_VIEW || st == D3DTS_PROJECTION)) {
        const char *n = (st == D3DTS_WORLD) ? "WORLD" : (st == D3DTS_VIEW) ? "VIEW" : "PROJECTION";
        const float *f = &m->m[0][0];
        L("SetTransform %-10s [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f]",
          n, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15]);
        g_transformLogs++;
    }
    return o_SetTransform(d, st, m);
}

static HRESULT __stdcall hSetVS(IDirect3DDevice8 *d, DWORD fvf) { g_fvf = fvf; return o_SetVS(d, fvf); }

static HRESULT __stdcall hSetStream(IDirect3DDevice8 *d, UINT s, IDirect3DVertexBuffer8 *vb, UINT stride)
{
    if (s == 0) g_stride = stride;
    return o_SetStream(d, s, vb, stride);
}

// A FVF carrying any texture-coordinate set. UVs are what the runtime needs to bind
// a texture; counting these against UV-less draws decides whether the renderer's
// "mesh without UVs" warning is benign or the actual defect.
#define FVF_HAS_TEX(f) (((f) & 0xF00) != 0)

static void CountDraw(void)
{
    // A D3D8 vertex-shader handle is not an FVF; it has no valid 0xF00 field.
    if (g_fvf != 0 && (g_fvf >> 16) != 0) g_shaderDraws++;
    else if (FVF_HAS_TEX(g_fvf)) g_texDraws++;
    else g_noTexDraws++;
    if ((g_texDraws + g_noTexDraws + g_shaderDraws) % 5000 == 0) DumpSummary("periodic");
}

static HRESULT __stdcall hDraw(IDirect3DDevice8 *d, D3DPRIMITIVETYPE pt, UINT sv, UINT pc)
{
    g_draws++; Note(g_fvf, g_stride); CountDraw();
    if (g_draws == 1) ProbeRemixApi();
    if (g_draws % 500 == 0) DumpSummary("periodic");
    return o_DrawPrimitive(d, pt, sv, pc);
}

static HRESULT __stdcall hDrawUP(IDirect3DDevice8 *d, D3DPRIMITIVETYPE pt, UINT pc, const void *data, UINT stride)
{
    g_drawsUP++; Note(g_fvf, stride); CountDraw();
    ProbeRemixApi();
    return o_DrawPrimitiveUP(d, pt, pc, data, stride);
}

static HRESULT __stdcall hDrawIdx(IDirect3DDevice8 *d, D3DPRIMITIVETYPE pt, UINT mi, UINT nv, UINT si, UINT pc)
{
    g_drawsIdx++; Note(g_fvf, g_stride); CountDraw();
    if (g_drawsIdx == 1 && g_draws == 0) ProbeRemixApi();
    return o_DrawIndexedPrimitive(d, pt, mi, nv, si, pc);
}

// Opt-in: D3DCREATE_NOWINDOWCHANGES (0x800) makes the Remix bridge skip its window
// management inside gSwapChainMapMutex, which is where the fullscreen abort comes from.
// Value 0x800 verified against the installed shim guard `test [ebp-74h],800h`.
// Default on: the game aborts in fullscreen without it. R3_NO_WINDOW_CHANGES=0 opts out.
static int NoWindowChanges(void)
{
    static int cached = -1;
    if (cached < 0) {
        char value[8];
        DWORD n = GetEnvironmentVariableA("R3_NO_WINDOW_CHANGES", value, sizeof(value));
        cached = !(n == 1 && value[0] == '0');
    }
    return cached;
}

static HRESULT __stdcall hCreateDevice(IDirect3D8 *d3d, UINT adapter, D3DDEVTYPE type, HWND hwnd,
                                       DWORD flags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **out)
{
    L("CreateDevice: adapter=%u type=%d behaviour=0x%08lX backbuffer=%lux%lu fmt=%d windowed=%d",
      adapter, (int)type, (unsigned long)flags,
      pp ? (unsigned long)pp->BackBufferWidth : 0UL, pp ? (unsigned long)pp->BackBufferHeight : 0UL,
      pp ? (int)pp->BackBufferFormat : -1, pp ? (int)pp->Windowed : -1);
    if (NoWindowChanges() && !(flags & 0x800u)) {
        L("  R3_NO_WINDOW_CHANGES: behaviour 0x%08lX -> 0x%08lX", (unsigned long)flags, (unsigned long)(flags | 0x800u));
        flags |= 0x800u;
    }
    HRESULT hr = o_CreateDevice(d3d, adapter, type, hwnd, flags, pp, out);
    L("CreateDevice -> hr=0x%08lX", (unsigned long)hr);
    if (SUCCEEDED(hr) && out && *out) {
        void **vt = *(void ***)*out;
        PatchSlot(vt, DEV_SetTransform,         (void *)hSetTransform,         (void **)&o_SetTransform);
        PatchSlot(vt, DEV_SetVertexShader,      (void *)hSetVS,                (void **)&o_SetVS);
        PatchSlot(vt, DEV_SetStreamSource,      (void *)hSetStream,            (void **)&o_SetStream);
        PatchSlot(vt, DEV_DrawPrimitive,        (void *)hDraw,                 (void **)&o_DrawPrimitive);
        PatchSlot(vt, DEV_DrawPrimitiveUP,      (void *)hDrawUP,               (void **)&o_DrawPrimitiveUP);
        PatchSlot(vt, DEV_DrawIndexedPrimitive, (void *)hDrawIdx,              (void **)&o_DrawIndexedPrimitive);
        L("device vtable hooked");
    }
    return hr;
}

static HMODULE g_self = NULL;

static void LoadReal(void)
{
    char path[MAX_PATH];
    GetModuleFileNameA(g_self, path, MAX_PATH);
    char *slash = strrchr(path, '\\');
    if (slash) lstrcpyA(slash + 1, "d3d8_real.dll");
    HMODULE real = LoadLibraryA(path);
    if (!real) { L("FATAL: cannot load %s err=%lu", path, GetLastError()); return; }
    o_Create8 = (Direct3DCreate8_t)GetProcAddress(real, "Direct3DCreate8");
    L("loaded %s, Direct3DCreate8=%p", path, (void *)o_Create8);
}

extern "C" __declspec(dllexport) IDirect3D8 * WINAPI Direct3DCreate8(UINT sdk)
{
    HookHostImport("RaiseException", (void *)hRaiseException, (void **)&o_RaiseException);
    HookHostImport("ExitProcess", (void *)hExitProcess, (void **)&o_ExitProcess);
    if (!o_Create8) LoadReal();
    if (!o_Create8) return NULL;
    IDirect3D8 *d3d = o_Create8(sdk);
    g_createCalls++;
    L("Direct3DCreate8(%u) call #%d -> %p", sdk, g_createCalls, (void *)d3d);
    if (d3d) {
        void **vt = *(void ***)d3d;
        void *before = vt[ID3D8_CreateDevice];
        PatchSlot(vt, ID3D8_CreateDevice, (void *)hCreateDevice, (void **)&o_CreateDevice);
        L("  patched CreateDevice slot %d: %p -> %p", ID3D8_CreateDevice,
          before, vt[ID3D8_CreateDevice]);
    }
    return d3d;
}

extern "C" __declspec(dllexport) void WINAPI DebugSetMute(void) {}

extern "C" __declspec(dllexport) HRESULT WINAPI ValidatePixelShader(DWORD *a, DWORD *b, BOOL c, char **d)
{
    typedef HRESULT (WINAPI *T)(DWORD *, DWORD *, BOOL, char **);
    static T f = NULL;
    if (!f) {
        HMODULE real = LoadLibraryA("d3d8_real.dll");
        if (real) f = (T)GetProcAddress(real, "ValidatePixelShader");
    }
    return f ? f(a, b, c, d) : E_FAIL;
}

extern "C" __declspec(dllexport) HRESULT WINAPI ValidateVertexShader(DWORD *a, DWORD *b, DWORD *c, BOOL e, char **f)
{
    typedef HRESULT (WINAPI *T)(DWORD *, DWORD *, DWORD *, BOOL, char **);
    static T fn = NULL;
    if (!fn) {
        HMODULE real = LoadLibraryA("d3d8_real.dll");
        if (real) fn = (T)GetProcAddress(real, "ValidateVertexShader");
    }
    return fn ? fn(a, b, c, e, f) : E_FAIL;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        g_self = self;
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        char *slash = strrchr(path, '\\');
        if (slash) lstrcpyA(slash + 1, "r3probe.log");
        g_log = fopen(path, "w");
        L("d3d8proxy loaded (pre-D3D-init)");
        LoadReal();
    } else if (reason == DLL_PROCESS_DETACH) {
        DumpSummary("detach");
        if (g_log) { fclose(g_log); g_log = NULL; }
    }
    return TRUE;
}
