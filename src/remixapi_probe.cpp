// remixapi_probe — prove the 32-bit RemixAPI path works on this machine.
//
// Loads the x86 Remix d3d9 shim, calls remixapi_InitializeLibrary, and reports
// which interface entry points are non-null. Then tries the pieces we actually
// need for Rayman 3: a material, a mesh, a camera, and an instance draw.
//
// Everything uses the vendored remix_c.h, so the ABI is the header's, not mine.
// Run from a directory containing d3d9.dll + .trex_arm64 (the runtime payload).

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "remix_c.h"

static FILE *g_log;

static void P(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static const char *ErrName(remixapi_ErrorCode c)
{
    switch (c) {
    case REMIXAPI_ERROR_CODE_SUCCESS: return "SUCCESS";
    case REMIXAPI_ERROR_CODE_GENERAL_FAILURE: return "GENERAL_FAILURE";
    case REMIXAPI_ERROR_CODE_LOAD_LIBRARY_FAILURE: return "LOAD_LIBRARY_FAILURE";
    case REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS: return "INVALID_ARGUMENTS";
    case REMIXAPI_ERROR_CODE_GET_PROC_ADDRESS_FAILURE: return "GET_PROC_ADDRESS_FAILURE";
    case REMIXAPI_ERROR_CODE_ALREADY_EXISTS: return "ALREADY_EXISTS";
    case REMIXAPI_ERROR_CODE_REGISTERING_NON_REMIX_D3D9_DEVICE: return "REGISTERING_NON_REMIX_D3D9_DEVICE";
    case REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION: return "INCOMPATIBLE_VERSION";
    default: return "other";
    }
}

int main(void)
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char *slash = strrchr(path, '\\');
    if (slash) lstrcpyA(slash + 1, "remixapi_probe.log");
    g_log = fopen(path, "w");
    if (!g_log) return 1;

    P("remixapi_probe: 32-bit RemixAPI proof");
    P("api version expected: %u.%u.%u",
      REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
    P("sizeof(Interface)=%u HardcodedVertex=%u MeshInfo=%u CameraInfo=%u InstanceInfo=%u",
      (unsigned)sizeof(remixapi_Interface), (unsigned)sizeof(remixapi_HardcodedVertex),
      (unsigned)sizeof(remixapi_MeshInfo), (unsigned)sizeof(remixapi_CameraInfo),
      (unsigned)sizeof(remixapi_InstanceInfo));

    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW(NULL, dllPath, MAX_PATH);
    wchar_t *wslash = wcsrchr(dllPath, L'\\');
    if (wslash) lstrcpyW(wslash + 1, L"d3d9.dll");

    HMODULE shim = LoadLibraryW(dllPath);
    P("LoadLibraryW(%ls) -> %p (err=%lu)", dllPath, (void *)shim, GetLastError());
    if (!shim) { P("VERDICT: cannot load shim"); return 2; }

    typedef remixapi_ErrorCode (*InitFn)(const remixapi_InitializeLibraryInfo *, remixapi_Interface *);
    InitFn init = (InitFn)GetProcAddress(shim, "remixapi_InitializeLibrary");
    P("remixapi_InitializeLibrary -> %p", (void *)init);
    if (!init) { P("VERDICT: missing export"); return 3; }

    remixapi_Interface iface;
    memset(&iface, 0, sizeof(iface));
    remixapi_InitializeLibraryInfo info;
    memset(&info, 0, sizeof(info));
    info.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
    info.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);

    remixapi_ErrorCode rc = init(&info, &iface);
    P("InitializeLibrary -> %d (%s)", (int)rc, ErrName(rc));

    P("interface entry points:");
    P("   Shutdown         %p", (void *)iface.Shutdown);
    P("   CreateMaterial   %p", (void *)iface.CreateMaterial);
    P("   CreateMesh       %p", (void *)iface.CreateMesh);
    P("   SetupCamera      %p   <-- the one that decides the project", (void *)iface.SetupCamera);
    P("   DrawInstance     %p", (void *)iface.DrawInstance);
    P("   CreateLight      %p", (void *)iface.CreateLight);
    P("   SetConfigVariable%p", (void *)iface.SetConfigVariable);

    if (!iface.SetupCamera) {
        P("VERDICT: interface live but NO SetupCamera - camera must come from capture");
        return 0;
    }

    // --- material (no textures; paths left zeroed) ---
    remixapi_MaterialInfo mi;
    memset(&mi, 0, sizeof(mi));
    mi.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
    mi.hash = 0xC0FFEE;
    mi.emissiveIntensity = 1.0f;
    remixapi_MaterialHandle mat = NULL;
    rc = iface.CreateMaterial(&mi, &mat);
    P("CreateMaterial -> %d (%s) handle=%p", (int)rc, ErrName(rc), (void *)mat);

    // --- one triangle, with a normal and a texcoord ---
    static remixapi_HardcodedVertex verts[3];
    memset(verts, 0, sizeof(verts));
    verts[0].position[0] = 0.0f;  verts[0].position[1] = 1.0f;  verts[0].position[2] = 0.0f;
    verts[1].position[0] = 1.0f;  verts[1].position[1] = -1.0f; verts[1].position[2] = 0.0f;
    verts[2].position[0] = -1.0f; verts[2].position[1] = -1.0f; verts[2].position[2] = 0.0f;
    for (int i = 0; i < 3; i++) {
        verts[i].normal[1] = 1.0f;
        verts[i].texcoord[0] = (i == 0) ? 0.5f : (i == 1 ? 1.0f : 0.0f);
        verts[i].texcoord[1] = (i == 0) ? 0.0f : 1.0f;
        verts[i].color = 0xFFFFFFFFu;
    }
    static uint32_t idx[3] = {0, 1, 2};

    remixapi_MeshInfoSurfaceTriangles surf;
    memset(&surf, 0, sizeof(surf));
    surf.vertices_values = verts;
    surf.vertices_count = 3;
    surf.indices_values = idx;
    surf.indices_count = 3;
    surf.material = mat;

    remixapi_MeshInfo meshInfo;
    memset(&meshInfo, 0, sizeof(meshInfo));
    meshInfo.sType = REMIXAPI_STRUCT_TYPE_MESH_INFO;
    meshInfo.hash = 0xC0FFEE;
    meshInfo.surfaces_values = &surf;
    meshInfo.surfaces_count = 1;
    remixapi_MeshHandle mesh = NULL;
    rc = iface.CreateMesh(&meshInfo, &mesh);
    P("CreateMesh -> %d (%s) handle=%p", (int)rc, ErrName(rc), (void *)mesh);

    // --- camera: look down -Z from (0,0,5), 60 deg fov, 16:9 ---
    remixapi_CameraInfo cam;
    memset(&cam, 0, sizeof(cam));
    cam.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
    cam.type = REMIXAPI_CAMERA_TYPE_WORLD;
    float view[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,5,1}};
    float f = 1.0f / tanf(30.0f * 3.14159265f / 180.0f);
    float proj[4][4] = {{f/1.7778f,0,0,0},{0,f,0,0},{0,0,1.001f,1},{0,0,-0.1001f,0}};
    memcpy(cam.view, view, sizeof(view));
    memcpy(cam.projection, proj, sizeof(proj));
    rc = iface.SetupCamera(&cam);
    P("SetupCamera -> %d (%s)", (int)rc, ErrName(rc));

    // --- draw it once ---
    if (mesh) {
        remixapi_InstanceInfo ii;
        memset(&ii, 0, sizeof(ii));
        ii.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
        ii.mesh = mesh;
        ii.transform.matrix[0][0] = 1.0f;
        ii.transform.matrix[1][1] = 1.0f;
        ii.transform.matrix[2][2] = 1.0f;
        ii.doubleSided = 1;
        rc = iface.DrawInstance(&ii);
        P("DrawInstance -> %d (%s)", (int)rc, ErrName(rc));
    }

    P("");
    P("VERDICT: 32-bit RemixAPI initialized and SetupCamera is available.");
    P("That means geometry+camera can be pushed without Remix's capture heuristic.");
    return 0;
}
