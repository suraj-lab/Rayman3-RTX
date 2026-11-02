# Rayman 3 RTX — checkpoint, 2026-11-02

> This project is now a Git repository (branch `main`, initial commit `7559aec`).
> Binaries, staged Remix runtimes, installers and `backups/` are ignored; source,
> scripts, analysis and per-run evidence text are tracked. No remote is configured.

## 2026-11-02 late — textures: the smoking gun is not yet proven

Running with `exposeRemixApi = True` in `.trex/bridge.conf` and
`.trex_arm64/bridge.conf` (backup `backups/before_exposeRemixApi_*`):
`RemixAPI: InitializeLibrary -> 0` now succeeds, and the export probe resolved
`CreateMaterial`, `CreateMesh`, `DrawInstance`. `SetupCamera` resolved to 0 and needs
re-checking against `remix_c.h` before any camera submission work.

Renderer log (`rtx-remix/logs/remix-dxvk.log`) contains exactly one
`[RTX-Compatibility-Info] Trying to bind a texture to a mesh without UVs. Was this
intended?` and one `not detecting a valid camera`. The message sits in the geometry
cluster beside `processGeometryInfo: no position data on input detected`,
`processGeometryInfo: input data is violating some assumptions` and
`Found a null sampler. Fallback to linear-repeat`.

**Do not treat the UV warning as the cause yet.** The proxy saw a position-only FVF
(`0x002`, stride 12) draw, which would produce exactly one such warning benignly. The
proxy keeps per-FVF draw counts (`g_seenCount`) and prints them in `DumpSummary`, but
this run's log has **no summary**: desktop screen capture crashed the ARM64 bridge
server (`0xc0000005`) and the process was terminated before detach. So the count of
UV-less draws versus textured draws is still unknown.

Operator-reported corrections: the world was **not** fully black, and starting an
NVIDIA screen recording crashes the run. Both ~35 s exits match the capture crash,
not game logic. PBR is premature while UVs are unverified.

Next: run once **without** any screen capture, with periodic summaries enabled for
indexed draws (currently only every 500 `DrawPrimitive` calls), then compare UV-less
draw counts against the FVF table. Only then decide between a UV/vertex-declaration
fix and a material/texture-hash investigation.

## 2026-11-02 — operator corrections to the record

The operator reports, for the same builds: **the menu rendered fine and the game was
quit normally**; the world **renders largely correctly**; there is a visible
**skinning-like artifact** on animated objects; there are **too few light sources**;
and surfaces are **not reflective**.

Corrections to earlier entries in this file that were wrong or over-generalised:

- The claim that the runtime skips the UI (`FVF 0x144`, 351,534 draws) and that the
  game therefore cannot be quit from its menu is **wrong**. The single
  `Skipped drawcall ... pre-transformed` line in the renderer log was a one-off, not
  the UI path. The menu renders and quit works. Do not repeat that inference.
- "Missing textures" was the operator's earlier description; the current observation is
  that the world renders but materials look unlit and non-reflective. Treat texture
  binding, lighting and material response as separate questions rather than one.

Open observations, in the operator's priority order:

1. **Skinning-like artifact.** Rayman 3 has no vertex skinning (the world vertex
   formats carry no bone data: `XYZ|NORMAL|DIFFUSE|TEX1..3`, strides 24-52). Animated
   characters are rigid parts driven by per-part world matrices. So this artifact is
   most likely transform/hash reuse of animated instances, not GPU skinning. Not yet
   investigated in the renderer logs.
2. **Too few lights.** Nothing currently supplies light data to the runtime. The
   game does call fixed-function `SetLight`/`LightEnable` (device vtable slots 0x9c,
   0xa4), which the proxy could hook and forward via `remixapi_CreateLight` /
   `remixapi_DrawLightInstance`.
3. **Non-reflective materials.** No PBR materials exist; the runtime uses defaults.
   Real keys in this build include `rtx.opaqueMaterial`, `rtx.legacyMaterial`,
   `rtx.legacyMaterial.roughnessConstant`, `rtx.translucentMaterial` (enumerated from
   the shipped ARM64 `d3d9.dll`, not guessed).

## 2026-11-02 late — UV warning ruled out by measurement

Capture-free run `tests/run_drawcounters_QoiiJgU` (no screen recording). Per-FVF draw
counters, written by the proxy at detach:

```
draws with UVs=446095  without UVs=1424  vertex-shader handles=270755
FVF 0x00000144 pos=XYZRHW stride=28  draws=351534
FVF 0x8B89E414 (shader handle) stride=48 draws=196855
FVF 0x8B90B0C4 (shader handle) stride=48 draws=73900
FVF 0x00000112 pos=XYZ stride=32  draws=66151
FVF 0x00000352 pos=XYZ stride=52  draws=20158
FVF 0x00000142 pos=XYZ stride=24  draws=5292
FVF 0x00000312 pos=XYZ stride=48  draws=2960
FVF 0x00000002 pos=XYZ stride=12   draws=1424
```

The 1,424 UV-less draws are **exactly** the `FVF 0x002` position-only count, i.e. 0.2%
of all draws. The renderer's single `Trying to bind a texture to a mesh without UVs`
warning is therefore **benign and explained**; it is not the texture defect. Do not
"fix" UVs.

Two consequences:

1. **Invisible menu.** The UI path is `FVF 0x144` (XYZRHW, 351,534 draws) and the
   renderer logs `Skipped drawcall, using pre-transformed vertices which isn't
   currently supported`. The runtime drops pre-transformed draws, so 2D/UI does not
   render. That is a runtime limitation, not a proxy bug; submitting UI ourselves via
   `remixapi_DrawInstance` would be the only workaround. Practical effect: the game
   cannot be quit from its own menu under Remix — kill the process instead.
2. **Textures are a material/hash problem.** The textured draws carry UVs (446k of
   them), so geometry attributes are not the blocker. Investigate texture hashing,
   material binding and the `Found a null sampler. Fallback to linear-repeat` path.
   PBR tuning is still premature: without albedo bound, PBR only darkens surfaces.

Also present in this run's renderer log: `InstanceManager: detected unsupported quad
index layout for billboard creation` and an Opacity Micromap "non triangle list
topology" fallback. Both are runtime-side, recorded but not yet investigated.

## FIRST WORKING GAME RUN — 2026-11-02

Rayman 3 **ran** under RTX Remix with no abort. Operator-confirmed: it ran, **no
textures**. Read `analysis/first-working-run.md`. Evidence:
`tests/run_nowindowchanges_AKKDFHGu/`.
Captured from the real game: VIEW ×5, PROJECTION ×5, WORLD ×30, and six FVFs
(`0x144` 2D/UI, `0x352`, `0x312`, `0x112`, `0x142`, `0x002` world-space) — the static
prediction of `0x142` at stride 24 was confirmed live.
Still open: `RemixAPI: InitializeLibrary -> 11 (NOT_INITIALIZED)`, so nothing is
submitted to the path tracer — the reason there are no textures. Exit cause of the
run is unrecorded; ALT+TAB/minimise untested with the new flag.

## Abort root cause found and fixed in the proxy — 2026-11-02

Read `analysis/abort-fix-findings.md`. The fullscreen abort is the Remix bridge's
window management running **inside** `gSwapChainMapMutex` (`window.cpp:155`) — the
nested lifecycle message re-enters `windowMsg` and the non-recursive CRT mutex throws
`std::system_error`. Proven by intervention, both directions, on the isolated chain:
behaviour `0x44` → exit `0xC000041D` with `std::system_error`; behaviour `0x844`
(`D3DCREATE_NOWINDOWCHANGES`) → exit **0**, lifecycle clean 3/3, stress clean 80/80.
Inverting after making it default reproduced the failure again with `=0`.
`src/d3d8proxy.cpp` now ORs `0x800` into BehaviourFlags (**default on**,
`R3_NO_WINDOW_CHANGES=0` opts out; value verified against the shim's own
`test [ebp-74h],800h`). The corrected ThrowInfo `+12` parser is what finally named
the harness throw as the same `std::system_error` seen in the game.
Proxy standalone gate still passes; matrix's three known failures unchanged.
**Not yet run in Rayman 3** — deploying the rebuilt proxy into the game and one launch
need explicit approval. Risk to check in-game: with the flag set D3D stops managing the
focus window, so fullscreen placement and ALT+TAB/minimise behaviour must be verified.

## Rayman3.exe renderer reconnaissance (static) — 2026-11-02

Read `analysis/rayman3-renderer-recon.md`. First static Ghidra pass on the game
itself on the desktop REA provider over SSH; nothing was executed. Anchors found:
`IDirect3D8*` global at VA `0x5E6C30`, renderer init `FUN_0046f870`, which calls
`CreateDevice` (vtable +0x3C) with behaviour **0x24 (software TnL)** or **0x44
(hardware TnL)** depending on `DAT_007EDD64`. The game's recorded device was 0x44,
so Rayman should submit **world-space XYZ** geometry when TnL is on — the earlier
"pre-transformed vertices" warning came from our own harness triangle.
Dead ends recorded: `Camera_VerticalAxis`/`HorizontalAxis` are Ubi.ini inversion
flags (`FUN_004460c0`), not matrices; `d3d8d.dll` is only a `DebugSetMute` helper.
Next: Phase 1 **done** — see below.

### Phase 1 map (complete)

Vtable offsets parsed from Wine `d3d8.h` and self-checked against the project's
verified slots 14/15. 669 device-vtable call sites censused; renderer cluster at
`0x46F800–0x479000`. FVF immediates decoded at all 47 `SetVertexShader` sites:
**28 use `0x142` = XYZ|DIFFUSE|TEX1 (world-space 3D)**, 12 use `0x144` = XYZRHW
(2D/HUD only), 1 uses `0x002`. `FUN_00472740` verified as
`SetTransform(D3DTS_WORLD) -> SetIndices -> SetStreamSource(stride 0x18) ->
SetVertexShader(0x142) -> DrawIndexedPrimitive`. Stride 24 = 12+4+8 matches the FVF.
Device global `IDirect3DDevice8*` = `0x7EDB18`; `IDirect3D8*` = `0x5E6C30`.
So real 3D Rayman geometry IS world-space and path-traceable; only the HUD is
pre-transformed. No `DrawPrimitiveUP` sites exist. Still static — FVF from immediates
(6 computed sites unresolved), not from a runtime run.
Next: map the `0x142` world path (`0x48D000–0x491300`), find view/projection
`SetTransform` sites (`D3DTS_VIEW`=2, `PROJECTION`=3) for camera capture.

### Phase 2 (camera) + methodology reuse — done

Camera anchor found: `FUN_00492740` reads a camera basis at `+0x08…+0x40`, negates
conditionally on a `+0xD8` flag (mirrored view, paired with `D3DRS_CULLMODE` 0x16

to 2 or 3), composes a 4x4 into `DAT_007E6340`, then calls `SetTransform`
(`D3DTS_VIEW`) at `0x492870`; `FUN_004928a0` follows and is the likely projection
stage (unconfirmed). Exactly one clean VIEW site exists; `0x10`/`0x11` hits at 28
other sites are nearest-push noise, recorded as unresolved, not as transform states.
Methodology comparison with the public NieR repo: `analysis/methodology-reuse-from-nier.md`.
Reusable: binary-as-oracle, pinned SHAs, self-checking ABI tables against known
slots, recorded negative results, independent controls. Not reusable: skinning/
pose transfer, DX11/FSR4/DLAA stack, Portal-RTX interop ordering.
Still no execution; nothing validated against a rendered frame. The shim
`std::system_error` abort remains the gate in front of every rendering step.

### Phase 3 (projection + world path) — done

`FUN_004928a0` sets `SetTransform(device, 3, local_40)` — **D3DTS_PROJECTION**,
with state and matrix computed locally (which is why Phase 2's immediate scan missed
it; near/far come from camera `+0x60`/`+0x64`). Camera pipeline is now complete:
VIEW @ `0x492870` (`FUN_00492740`), PROJECTION (`FUN_004928a0`), WORLD per draw.
`DAT_007E6340` has only two xrefs (`0x49282C`, `0x59083D`); its exact role is unresolved.
`FUN_00490960` is the batched world-geometry renderer: stride `0x18`, `SetVertexShader
(0x142)`, strip batching into `DAT_00637DC0` flushed above `0x7CB` vertices, drawing
`count >> 1` triangles. So `0x142` is confirmed as the real scene format, not just
the small immediate path. Unverified: the `>> 1` primitive convention, all runtime
matrix values, and `FUN_00588750`'s matrix semantics.

## Mod hook analysis (static, desktop REA/Ghidra) — 2026-11-02

Read `analysis/mod-hook-analysis.md`. Nine hashed DLL copies were analysed statically
on the desktop via its existing registered REA 6.4.0 + Ghidra 12.1.2 provider over SSH;
nothing was executed and no installed game file changed. Result: no loaded mod installs
a competing window procedure. Ultimate ASI Loader (`winmm.dll`) only scans `scripts\`/
`plugins\` for `*.asi` and loads them; its `SetWindowPos`/`SetWindowTextW` strings have
zero code xrefs. ALchemy (`dsound.dll`) is a DirectSound proxy only. BetterRayman3 is
the one real game-code patcher (Rayman3 WidescreenFix + Hooking.Patterns, `Ubi.ini`
config) and is **disabled**. This removes mod WndProc interference for the installed
configuration and keeps the Remix shim's mutex failure as the lead. Limitation: static
analysis only; `GetProcAddress`-resolved calls are not fully covered.

## Latest resume point — authorized fullscreen probe now fails
Read `analysis/fullscreen-probe-findings.md` first. One approved isolated run of
`--lifecycle --fullscreen --stress` created/drew/Presented successfully, then hit
C++ exception `0xE06D7363` during lifecycle queue processing and exited `0xC000041D`.
Evidence: `tests/fullscreen_probe_nm5zsdvr/` and `tests/remix_abi_snxkrde5/`.
The runner returned 0 because lifecycle mode is evidence-only; the runtime FAILED.
No completed lifecycle/stress verdict was recorded. The throw stack had zero frames,
and a diagnostic parser bug prevented reliable type identification: this is not yet
a confirmed reproduction of Rayman's mutex failure.
`DumpTypeName` now uses the verified x86 ThrowInfo type-array offset +12, not the
unwind-function field +4. CPU-only regression failed before and passed after:
`tests/exception_metadata_1k7xroe5/`. No second fullscreen run occurred.
Next: stage a source-bound fresh harness and improve stack/module-RVA capture before
requesting another single isolated replay. Rayman was not launched; installed game
DLL hashes remained unchanged, and no matching test processes remained after the run.

## Previous resume point — exact shim symbols recovered

Read `analysis/shim-symbol-findings.md` first. NVIDIA's release PDB matches the shim's
GUID/age exactly; preserved evidence and pinned client source are in
`analysis/shim-symbols-1.6.1/`. The captured stack resolves to `WndProc::windowMsg`
at `window.cpp:155`, locking `gSwapChainMapMutex`, called by `RemixWndProc:294`.
The handler holds that non-recursive mutex across fullscreen `SetWindowPos`.
Same-thread resize-message reentrancy inside that call is now a concrete hypothesis,
not a proven Rayman cause; mutex corruption/lifetime remains unexcluded.
Next: an explicitly approved isolated fullscreen/message-pump/resize probe targeting
this internal nesting path. The earlier app-WndProc nesting test covered a different
lock lifetime. No game launch, GPU probe, installed-DLL replacement or settings
change occurred during symbol recovery. Do not suppress the exception or swap mutex types.

## Previous runtime resume point — 23:15 exit-tracing run

Read `analysis/exit-trace-findings.md` first. Runtime evidence: `tests/exit_trace_game_2am85wxs`.
The latest authorized run captured ExitProcess code 3, the Rayman CRT abort/signal path,
and a C++ throw path inside the Remix d3d9.dll shim. Binary metadata identifies std::system_error;
decoding points to an invalid-argument error after a failed lock in window-message handling.
Why the lock fails and the exact triggering message remain unresolved. No real game draws yet.
Next: match the handler to bridge source/symbols and reproduce its window-lifecycle path in an isolated harness.
Do not guess at mutex changes, suppress the exception, or blindly change TnL.

Installed D3D8.dll is now `src/d3d8proxy.exit-trace.dll`, SHA256
`ba32d48ccba9f40e05c1c7b02f07255ded8c955c228968d81658ba1da06125ab`.
Backup: `backups/before_exit_trace_game_n90y__iq`.
`src/d3d8proxy.dll` remains the older exception-only build; it is NOT the installed version now.
The game remains in Remix diagnostic mode. Game/launcher/bridge were stopped at checkpoint.
Settings, converter/shim and save matched their pre-launch hashes. No further launch occurred.
Another installed-DLL replacement, settings change or game launch requires specific approval.

## Location and safety

Workspace: `C:\Users\suraj\Projects\Rayman 3 RTX`.
Game remains in `C:\Program Files\GOG Galaxy\Games\Rayman 3`.
Standalone testing did not launch Rayman or change installed game DLLs.
Subsequently, the user authorized backing up/replacing D3D8.dll and one game launch.
The slot-15 proxy with exception tracing is now installed. The runtime error persists.
Backups: `backups/before_slot15_wt6wcn7m` and `backups/before_exception_trace_pywc7goa` (prior DLLs, logs, and manifests).
One additional game launch was authorized for the exception-only build, then one more for the exit tracer.
Earlier sections below retain those stages' evidence; the latest resume point above describes current installation state.

## Verified corrections

- IDirect3D8 has 16 COM methods: GetAdapterMonitor is zero-based slot 14, CreateDevice is slot 15.
- Proxy now hooks slot 15 and no longer reads device slots from the smaller IDirect3D8 vtable.
- Repeated Direct3DCreate8 calls do not recursively re-hook shared slots.
- RemixAPI probe uses the real vendored NieR `remix_c.h`, not handwritten declarations.
- x86 presentation structure size is checked at compile time.
- Standalone harness checks device, transform/FVF state, DrawPrimitiveUP, and Present return codes.

## Tests and evidence

`python tests/run_abi.py` creates a fresh evidence directory per run.
Latest: `tests/abi_6jd4pkuk`. This also checks handled/continuable SEH, C++ exceptions,
and explicit ExitProcess(73) forwarding on the passing backend cases.

| Backend | Direct | Through proxy |
|---|---|---|
| Remix-bundled d3d8to9 -> system D3D9 | PASS | PASS |
| crosire d3d8to9 -> system D3D9 | PASS | PASS |
| BR3 DXWrapper with D3d8to9=1 -> system D3D9 | PASS | access violation |
| Saved dgVoodoo | heap corruption | heap corruption |

The latest overall matrix returns failure because three cases fail; do not describe the complete suite as passing.
An earlier exit-tracer development run (`tests/abi_511d_by5`) also failed the host-stack checks and BR3 direct after Present.
Keeping a frame pointer in the exit hook fixed stack capture; BR3 direct passed the following run, so it is not proven consistently reliable.
The renamed DXWrapper log shows generic API hooking including our d3d8.dll; its failure is not proof of game incompatibility.
The dgVoodoo standalone failure is unexplained and does not negate the earlier user-observed in-game play.
The legacy `abitest/b.cmd` and its source are old evidence, not the corrected harness; use `tests/run_abi.py`.

`python tests/run_remix_abi.py` uses an isolated runtime copy.
Latest evidence: `tests/remix_abi_npvuorj_` (exit-tracing candidate).
Earlier passing evidence: `tests/remix_abi_8inyihr6` and `tests/remix_abi_dlm9q8b_`.
Full x86 test host -> corrected D3D8 proxy -> bundled d3d8to9 -> x86 Remix shim -> ARM64 renderer:
CreateDevice, SetTransform, SetVertexShader, DrawPrimitiveUP, EndScene, Present all returned S_OK; harness exited normally.
Proxy observed one DrawPrimitiveUP. Vulkan 1.4.351 renderer startup was logged.
This is API-call/bridge evidence, not a screenshot or proof of ray-traced output.
The synthetic triangle uses XYZRHW; its missing-camera warning is expected, not evidence about Rayman's geometry.
RemixAPI initialization from the x86 shim returned 11 (NOT_INITIALIZED) at the first draw, despite full renderer startup.
Later lifecycle stages and other runtime/API versions remain untested.

Project `src/d3d8proxy.dll` (previously installed): exception-only tracing build,
SHA256 `bf7adbe3db012c79edfdf98fed8dad837f83e318b8cfa414c72c6b7eca14867e`.
Separate validated candidate: `src/d3d8proxy.exit-trace.dll`,
SHA256 `ba32d48ccba9f40e05c1c7b02f07255ded8c955c228968d81658ba1da06125ab`.
It adds host ExitProcess tracing, preserving the exit code and recording Windows-captured module-relative frames.
The exit tracer was subsequently deployed for the latest authorized run; see the resume point above.
Its adjacent manifest binds source hashes and standalone test evidence; its original deployed_to_game=false field records candidate creation time.
Current source and test runners build the exit-tracing candidate, not the older canonical project DLL.
Earlier binaries are preserved as `src/d3d8proxy.slot15-no-trace.dll` and `src/d3d8proxy.pre-slot15.dll`.

## NieR reuse

Local `Nier-Automata-Rendering-depth` has ongoing work and was left untouched.
Reference files are pinned under `reference/nier-remix`, revision
`79394e598f4bea316837e86bf7003e3a6d6d46ab`; manifest records individual SHA256 values.
The Rayman vendored API header matches that pinned header exactly.
NieR's live host explicitly validates required API members and uses its own D3D9Ex device,
registers it, then calls Startup (ALREADY_EXISTS is handled).
That separate-host lifecycle is a reference, not something already working inside Rayman's x86 bridge.
Camera/mesh submission and capture converters are available for reuse but are not yet ported.

## Controlled game test and error capture

One authorized game launch used the validated slot-15 proxy.
Rayman CreateDevice succeeded (S_OK): 2880x1800, hardware vertex processing, fullscreen.
Device vtable hooks were installed, but no hooked draw/transform call was recorded before the runtime-error dialogs.
Window text confirmed: Microsoft Visual C++ Runtime Library, abnormal program termination.
Being alive at 27 seconds was NOT evidence of stability: the process was blocked on error dialogs.

Preserved evidence: `tests/runtime_error_jqz5cwkt`.
Includes current logs, executable/DLL hashes, and `Rayman3.dmp` (88,431 bytes).
MiniDumpWriteDump succeeded; minidump signature and thread/module stream counts checked (8 threads, 85 modules).
This is a live dialog-state dump, not a crash-time exception dump: no exception stream is present.
Contexts are native ARM64 (flags 0x4000df); x86 EBP unwinding is not applicable to these contexts.
The dump's module list does not show MFC42D/MSVCRTD, so earlier claims of a debug-MFC ASSERT were unsupported.
No process was terminated and no dialog was dismissed by the diagnostic capture.

## Exception-tracing game run

Authorized launch at approximately 22:45 local time, 2026-11-01; evidence: `tests/trace_game_6syzwgke`.
The user observed the runtime error again. The host RaiseException IAT hook installed successfully,
CreateDevice returned S_OK at 2880x1800 fullscreen, and no hooked draws or exception records followed.
A detach summary was logged. The game, launcher and bridge were stopped by the time evidence was copied;
no fresh live dump or dialog text was captured, and process exit code/call stack were not recorded.
No matching recent Application events or Rayman WER report directories were found in the bounded checks.
Converter/shim, Ubi.ini, remix.conf and SUJ.sav hashes matched their pre-launch snapshots.
Absence of a host RaiseException record does not exclude dependency-raised or hardware exceptions.

Offline decoding confirms the CRT reporter resolves MessageBoxA dynamically rather than using its named import.
The abort-shaped path ultimately has a direct named ExitProcess import call; see `analysis/termination-path.md`.
The exit-tracer candidate passed explicit non-zero exit forwarding and a full Remix-chain normal exit with ten captured frames.
Actual Rayman unwind depth and the abort's cause remain unproven. No exception is swallowed or abort bypassed.

## Offline lifecycle probe (2026-11-02)

No game launch, no installed-DLL change, no settings change; installed DLL hashes checked unchanged.
See `analysis/lifecycle-probe.md`. Two findings:

- The three window messages alone do not reproduce the abort. The isolated chain created a
  windowed device and a fullscreen A8R8G8B8 device (Rayman's own parameters, behaviour 0x44),
  drew, Presented, and handled `WM_ACTIVATEAPP`/`WM_SIZE`/`WM_DESTROY` with normal exit.
- A rejected `CreateDevice` is fatal rather than reported: fullscreen with `D3DFMT_UNKNOWN`
  (D3DERR_INVALIDCALL) produced unhandled `0xE06D7363`, `EXCEPTION_NONCONTINUABLE`, with NULL
  exception object and NULL ThrowInfo on the first-chance record, terminating through the host
  CRT's unhandled-exception filter. No `catch` can match that shape. That case is a harness
  parameter error, not the game's path, and Rayman's recorded request was fmt=21.
- New harness capability: `python tests/run_remix_abi.py --lifecycle [--fullscreen]`.

## Next

The exit-tracing deployment/run is complete; see `analysis/exit-trace-findings.md` for the recorded stack and decoded shim path.
Source/symbol matching is now documented in `analysis/shim-symbol-findings.md`.
Continue with a targeted standalone window-message reproduction before proposing another game test.
The corrected hook disproves the earlier claim that the game aborts before creating a device; it does not explain the remaining abort.
Do not retry launches or change game/system settings without a specific evidence-backed test.
No real Rayman frame, world-space geometry, camera matrix, or offline rendering capture has yet been validated.
