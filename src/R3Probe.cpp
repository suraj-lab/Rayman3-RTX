// R3Probe — logs Rayman 3's D3D8 API usage to r3probe.log.
//
// Answers one question: does the game ever hand D3D *world-space* geometry,
// or are all draws pre-transformed (D3DFVF_XYZRHW)? If XYZRHW dominates and no
// WORLD/VIEW/PROJECTION transforms arrive, Remix can never find a camera and
// geometry has to be recovered from the game's own math instead.
//
// Build: build_probe.cmd (MSVC, x86). Install as scripts\R3Probe.asi with
// BetterRayman3.asi renamed away, so the two don't fight over the vtable.

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// d3d8.h ships only with the legacy DirectX SDK; declare the slice we use.
typedef struct IDirect3D8 IDirect3D8;
typedef struct IDirect3DDevice8 IDirect3DDevice8;
typedef struct IDirect3DVertexBuffer8 IDirect3DVertexBuffer8;
typedef UINT D3DFORMAT;
typedef UINT D3DDEVTYPE;
typedef UINT D3DTRANSFORMSTATETYPE;
typedef UINT D3DPRIMITIVETYPE;
typedef UINT D3DSWAPEFFECT;
typedef UINT D3DMULTISAMPLE_TYPE;

typedef struct { float m[4][4]; } D3DMATRIX;

typedef struct {
    UINT BackBufferWidth, BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    D3DSWAPEFFECT SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed, EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz, FullScreen_PresentationInterval;
} D3DPRESENT_PARAMETERS;

#define D3DFVF_POSITION_MASK   0x00E
#define D3DFVF_XYZ             0x002
#define D3DFVF_XYZRHW          0x004
#define D3DFVF_XYZB1           0x006
#define D3DFVF_XYZB2           0x008
#define D3DFVF_XYZB3           0x00A
#define D3DFVF_XYZB4           0x00C
#define D3DFVF_XYZB5           0x00E
#define D3DFVF_NORMAL          0x010
#define D3DFVF_PSIZE           0x020
#define D3DFVF_DIFFUSE         0x040
#define D3DFVF_SPECULAR        0x080
#define D3DFVF_TEXCOUNT_MASK   0x00000F00
#define D3DFVF_TEXCOUNT_SHIFT  8

#define D3DTS_VIEW             2
#define D3DTS_PROJECTION       3
#define D3DTS_WORLD            256

static FILE *g_log = NULL;
static void L(const char *fmt, ...)
{
    if (!g_log) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

enum { ID3D8_CreateDevice = 14 };
enum {
    DEV_SetTransform = 37, DEV_DrawPrimitive = 70, DEV_DrawIndexedPrimitive = 71,
    DEV_SetVertexShader = 76, DEV_SetStreamSource = 83,
    DEV_DrawPrimitiveUP = 72
};

typedef IDirect3D8 *(__stdcall *Direct3DCreate8_t)(UINT);
typedef HRESULT (__stdcall *CreateDevice_t)(IDirect3D8 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice8 **);
typedef HRESULT (__stdcall *SetTransform_t)(IDirect3DDevice8 *, D3DTRANSFORMSTATETYPE, const D3DMATRIX *);
typedef HRESULT (__stdcall *SetVS_t)(IDirect3DDevice8 *, DWORD);
typedef HRESULT (__stdcall *SetStream_t)(IDirect3DDevice8 *, UINT, IDirect3DVertexBuffer8 *, UINT);
typedef HRESULT (__stdcall *DrawPrim_t)(IDirect3DDevice8 *, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT (__stdcall *DrawPrimUP_t)(IDirect3DDevice8 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
typedef HRESULT (__stdcall *DrawIdx_t)(IDirect3DDevice8 *, D3DPRIMITIVETYPE, UINT, UINT, UINT, UINT);

static Direct3DCreate8_t o_Direct3DCreate8 = NULL;
static CreateDevice_t    o_CreateDevice = NULL;
static SetTransform_t    o_SetTransform = NULL;
static SetVS_t           o_SetVS = NULL;
static SetStream_t       o_SetStream = NULL;
static DrawPrim_t        o_DrawPrimitive = NULL;
static DrawPrimUP_t      o_DrawPrimitiveUP = NULL;
static DrawIdx_t         o_DrawIndexedPrimitive = NULL;

static DWORD g_fvf = 0;
static UINT  g_stride = 0;
static unsigned long g_draws = 0, g_drawsIdx = 0, g_drawsUP = 0;
static DWORD g_seenFvf[128]; static UINT g_seenStride[128];
static unsigned long g_seenCount[128]; static int g_seenN = 0;
static int g_transformLogs = 0;
static bool g_xyzrhwSeen = false, g_xyzSeen = false;

static IDirect3D8 *__stdcall hDirect3DCreate8(UINT sdk);
static HRESULT __stdcall hCreateDevice(IDirect3D8 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice8 **);

static const char *PosName(DWORD fvf)
{
    switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZ:    return "XYZ";
    case D3DFVF_XYZRHW: return "XYZRHW";
    case D3DFVF_XYZB1:  return "XYZB1";
    case D3DFVF_XYZB2:  return "XYZB2";
    case D3DFVF_XYZB3:  return "XYZB3";
    case D3DFVF_XYZB4:  return "XYZB4";
    case D3DFVF_XYZB5:  return "XYZB5";
    default:            return "none";
    }
}

static void Note(DWORD fvf, UINT stride)
{
    if (fvf & D3DFVF_XYZRHW) g_xyzrhwSeen = true;
    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZ) g_xyzSeen = true;
    for (int i = 0; i < g_seenN; i++)
        if (g_seenFvf[i] == fvf) { g_seenCount[i]++; return; }
    if (g_seenN < 128) {
        g_seenFvf[g_seenN] = fvf; g_seenStride[g_seenN] = stride;
        g_seenCount[g_seenN] = 1; g_seenN++;
        L("  new FVF 0x%08lX pos=%-6s tex=%lu normals=%d diffuse=%d stride=%lu",
          (unsigned long)fvf, PosName(fvf),
          ((unsigned long)fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT,
          (fvf & D3DFVF_NORMAL) ? 1 : 0, (fvf & D3DFVF_DIFFUSE) ? 1 : 0, (unsigned long)stride);
    }
}

static void DumpSummary(const char *why)
{
    L("");
    L("=== summary (%s) ===", why);
    L("  DrawPrimitive=%lu DrawIndexedPrimitive=%lu DrawPrimitiveUP=%lu",
      g_draws, g_drawsIdx, g_drawsUP);
    L("  saw XYZRHW buffers: %s | saw XYZ buffers: %s",
      g_xyzrhwSeen ? "YES (pre-transformed)" : "no", g_xyzSeen ? "YES (world space)" : "no");
    for (int i = 0; i < g_seenN; i++)
        L("  FVF 0x%08lX pos=%-6s stride=%-3lu draws=%lu",
          (unsigned long)g_seenFvf[i], PosName(g_seenFvf[i]), (unsigned long)g_seenStride[i], g_seenCount[i]);
}

static void PatchSlot(void **vt, int idx, void *hook, void **orig)
{
    DWORD old;
    if (!VirtualProtect(&vt[idx], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) return;
    if (*orig == NULL) *orig = vt[idx];
    vt[idx] = hook;
    VirtualProtect(&vt[idx], sizeof(void *), old, &old);
}

static HRESULT __stdcall hSetTransform(IDirect3DDevice8 *dev, D3DTRANSFORMSTATETYPE st, const D3DMATRIX *m)
{
    if (g_transformLogs < 24 && m && (st == D3DTS_WORLD || st == D3DTS_VIEW || st == D3DTS_PROJECTION)) {
        const char *n = (st == D3DTS_WORLD) ? "WORLD" : (st == D3DTS_VIEW) ? "VIEW" : "PROJECTION";
        const float *f = &m->m[0][0];
        L("SetTransform %-10s [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f]",
          n, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15]);
        g_transformLogs++;
    }
    return o_SetTransform(dev, st, m);
}

static HRESULT __stdcall hSetVertexShader(IDirect3DDevice8 *dev, DWORD fvf)
{
    g_fvf = fvf;
    return o_SetVS(dev, fvf);
}

static HRESULT __stdcall hSetStreamSource(IDirect3DDevice8 *dev, UINT s, IDirect3DVertexBuffer8 *vb, UINT stride)
{
    if (s == 0) g_stride = stride;
    return o_SetStream(dev, s, vb, stride);
}

static HRESULT __stdcall hDrawPrimitive(IDirect3DDevice8 *dev, D3DPRIMITIVETYPE pt, UINT sv, UINT pc)
{
    g_draws++;
    Note(g_fvf, g_stride);
    if (g_draws % 400 == 0) DumpSummary("periodic");
    return o_DrawPrimitive(dev, pt, sv, pc);
}

static HRESULT __stdcall hDrawPrimitiveUP(IDirect3DDevice8 *dev, D3DPRIMITIVETYPE pt, UINT pc, const void *data, UINT stride)
{
    g_drawsUP++;
    Note(g_fvf, stride);
    return o_DrawPrimitiveUP(dev, pt, pc, data, stride);
}

static HRESULT __stdcall hDrawIndexedPrimitive(IDirect3DDevice8 *dev, D3DPRIMITIVETYPE pt, UINT minIdx, UINT nVerts, UINT startIdx, UINT pc)
{
    g_drawsIdx++;
    Note(g_fvf, g_stride);
    return o_DrawIndexedPrimitive(dev, pt, minIdx, nVerts, startIdx, pc);
}

static HRESULT __stdcall hCreateDevice(IDirect3D8 *d3d, UINT adapter, D3DDEVTYPE type, HWND hwnd,
                                      DWORD flags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **out)
{
    L("CreateDevice: adapter=%u type=%d behaviour=0x%08lX backbuffer=%lux%lu fmt=%d",
      adapter, (int)type, (unsigned long)flags,
      pp ? (unsigned long)pp->BackBufferWidth : 0UL, pp ? (unsigned long)pp->BackBufferHeight : 0UL,
      pp ? (int)pp->BackBufferFormat : -1);
    HRESULT hr = o_CreateDevice(d3d, adapter, type, hwnd, flags, pp, out);
    L("CreateDevice -> hr=0x%08lX device=%p", (unsigned long)hr, out ? (void *)*out : NULL);
    if (SUCCEEDED(hr) && out && *out) {
        void **vt = *(void ***)*out;
        PatchSlot(vt, DEV_SetTransform,         (void *)hSetTransform,         (void **)&o_SetTransform);
        PatchSlot(vt, DEV_SetVertexShader,      (void *)hSetVertexShader,      (void **)&o_SetVS);
        PatchSlot(vt, DEV_SetStreamSource,      (void *)hSetStreamSource,      (void **)&o_SetStream);
        PatchSlot(vt, DEV_DrawPrimitive,        (void *)hDrawPrimitive,        (void **)&o_DrawPrimitive);
        PatchSlot(vt, DEV_DrawPrimitiveUP,      (void *)hDrawPrimitiveUP,      (void **)&o_DrawPrimitiveUP);
        PatchSlot(vt, DEV_DrawIndexedPrimitive, (void *)hDrawIndexedPrimitive, (void **)&o_DrawIndexedPrimitive);
        L("device vtable hooked (orig draws: %p / %p / %p)",
          (void *)o_DrawPrimitive, (void *)o_DrawIndexedPrimitive, (void *)o_DrawPrimitiveUP);
    }
    return hr;
}

static IDirect3D8 *__stdcall hDirect3DCreate8(UINT sdk)
{
    IDirect3D8 *d3d = o_Direct3DCreate8(sdk);
    L("Direct3DCreate8(%u) -> %p", sdk, (void *)d3d);
    if (d3d) {
        void **vt = *(void ***)d3d;
        PatchSlot(vt, ID3D8_CreateDevice, (void *)hCreateDevice, (void **)&o_CreateDevice);
    }
    return d3d;
}

static void HookIAT(HMODULE mod)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base +
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    for (; imp->Name; imp++) {
        const char *dll = (const char *)(base + imp->Name);
        if (_stricmp(dll, "d3d8.dll") != 0) continue;
        IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        IMAGE_THUNK_DATA *orig  = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        for (; thunk->u1.Function; thunk++, orig++) {
            if (orig->u1.AddressOfData && IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + orig->u1.AddressOfData);
            if (strcmp((const char *)ibn->Name, "Direct3DCreate8") != 0) continue;
            DWORD old;
            if (VirtualProtect(&thunk->u1.Function, sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) {
                o_Direct3DCreate8 = (Direct3DCreate8_t)thunk->u1.Function;
                thunk->u1.Function = (ULONG_PTR)hDirect3DCreate8;
                VirtualProtect(&thunk->u1.Function, sizeof(void *), old, &old);
                L("IAT hooked: d3d8.dll!Direct3DCreate8 (real=%p)", (void *)o_Direct3DCreate8);
                return;
            }
        }
    }
    L("IAT hook FAILED: d3d8.dll!Direct3DCreate8 not found");
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        char *slash = strrchr(path, '\\');
        if (slash) lstrcpyA(slash + 1, "r3probe.log");
        g_log = fopen(path, "w");
        L("R3Probe attached");
        if (!slash || _stricmp(slash + 1, "r3probe.log") != 0) {
            L("note: exe path truncated");
        }
        HookIAT(GetModuleHandleA(NULL));
    } else if (reason == DLL_PROCESS_DETACH) {
        DumpSummary("detach");
        if (g_log) { fclose(g_log); g_log = NULL; }
    }
    return TRUE;
}
