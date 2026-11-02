# Rayman 3 renderer reconnaissance — first static pass, 2026-11-02

Goal (NieR-style): establish whether the game submits geometry Remix can ray
trace, and locate the anchors needed for camera/transform capture.

Method: static Ghidra analysis on the desktop's registered REA 6.4.0 provider over
SSH. `Rayman3.exe` (2,916,704 bytes, SHA256
`64deefd3a7b14a32124aa9b31ebba84e1a7c9a4a54ee40d9b65b4043ff1be15c`) was copied
into the isolated analysis workspace. Nothing was executed — no game launch, no
emulation.

Evidence: `evidence/game-pass1` … `game-pass3` in the remote workspace,
`~/Projects/rayman3-rtx-analysis/hooks_0s8j6upd/`. Key REA Evidence IDs:

| Claim | Evidence ID |
|---|---|
| Image overview: 5,846 procedures, 1,894 strings, 9 segments | `ev_463e59ea…` |
| D3D-related strings/procedures | `ev_91c4687c…`, `ev_199c04ee…` |
| `Direct3DCreate8` IAT xref → `0x46F915` | `ev_7b0d15c6…` |
| Renderer init `FUN_0046f870` dossier | `ev_fc4bb4db…` |
| Ubi.ini settings reader `FUN_004460c0` | `ev_68516111…` |
| `d3d8d.dll` debug-mute helper `FUN_0051f6a3` | `ev_bda8cc60…` |

## The decisive finding: software vs hardware vertex processing

`Rayman3.exe` imports exactly one Direct3D entry point, `d3d8.dll::Direct3DCreate8`.
Everything else goes through COM vtables, which is why no `SetTransform` /
`DrawPrimitive` / `FVF` strings exist in the binary.

`FUN_0046f870` is the renderer initialiser. It:

1. `DAT_005E6C30 = Direct3DCreate8()` — the `IDirect3D8*` lives at **VA `0x5E6C30`**.
2. Calls `(**(code **)(*DAT_005E6C30 + 0x20))()` — vtable `+0x20` = `GetAdapterDisplayMode`.
3. Calls `(**(code **)(*DAT_005E6C30 + 0x3C))()` — vtable `+0x3C` = **slot 15 = `CreateDevice`**
   (consistent with the slot-15 finding already recorded in STATUS.md).

Two mutually exclusive `CreateDevice` branches exist in that routine:

```c
if (DAT_007EDD64 == '\0') {
    BehaviourFlags = 0x24;   // D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED
    ... CreateDevice ...
} else {
    BehaviourFlags = 0x44;   // D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED
    ... CreateDevice ...
}
```

**`0x44` is exactly the behaviour flag our proxy recorded from the real game**
(`CreateDevice: behaviour=0x00000044`, fullscreen, 2880x1800, fmt=21).

Consequences, stated with their limits:

- In the current configuration the engine requests **hardware** vertex processing,
  so it should feed D3D8 **world-space `XYZ`** vertices with transforms set via the
  device, rather than transforming to `XYZRHW` itself.
- The `RTX-Compatibility-Info … pre-transformed vertices … isn't currently
  supported` lines in our logs came from the **harness's** own `XYZRHW` triangle.
  They are not evidence about Rayman's geometry, and this pass gives a positive
  reason to doubt that Rayman is pre-transformed when TnL is on.
- Not yet proven: that the hardware branch is the one taken at runtime (it follows
  from `0x44` being observed, but the flag's writer was not traced), nor that every
  draw uses a single FVF. Draw-time FVF inspection remains the confirmation step.

## Supporting facts and two dead ends

- `FUN_004460C0` reads `%WINDIR%\UbiSoft\Ubi.ini`: `GLI_Mode`, `Adapter`,
  `Identifier`, `TexturesCompressed`, `TriLinear`, `DynamicShadows`,
  `StaticShadows`, `Outline`, `TexturesMem`, `SoundOnHD`, `Complete`,
  `Camera_HorizontalAxis`, `Camera_VerticalAxis`, plus `SingleProcessor` →
  `SetProcessAffinityMask`.
- **Dead end 1:** `Camera_VerticalAxis` / `Camera_HorizontalAxis` are Ubi.ini
  *inversion settings* read with `GetPrivateProfileIntA`, **not** camera matrices.
  Do not treat them as transform anchors.
- **Dead end 2:** `d3d8d.dll` is not a renderer path. `FUN_0051F6A3` loads
  `d3d8.dll`/`d3d8d.dll` only to `GetProcAddress("DebugSetMute")` — the debug
  D3D runtime's mute control. Useful context for the debug MFC build, nothing more.

## Proposed plan (mirrors the NieR project structure)

Phase 1 — static map (no game launch). Locate, inside `FUN_0046f870` and its
callees: the device object global written by `CreateDevice`, then the indirect
calls made through it that map to `SetVertexShader` (FVF), `SetTransform`
(`D3DTS_WORLD/VIEW/PROJECTION`), `SetStreamSource`, `DrawPrimitiveUP` /
`DrawIndexedPrimitive`. Deliverable: a table of `game VA → D3D8 slot → argument
source`. This is the equivalent of NieR's verified-transform work, done without
running anything.

Phase 2 — confirm the vertex format. Cheapest confirmation is draw-time telemetry
from the existing D3D8 proxy (FVF, draw counts, transform slot writes) during one
approved game run — but that is blocked until the abort is fixed, so Phase 1 must
answer it statically first.

Phase 3 — camera capture. Once `SetTransform`/view-matrix call sites are known,
capture view/projection per frame and check them against rendered output (the NieR
"verified transforms" gate).

Phase 4 — live remix transfer. Only after phases 1–3, and only if draws are indeed
world-space, is a NieR-style live transfer meaningful.

## Phase 1 results — the renderer map (static, no execution)

### Verified vtable offsets

Derived from Wine's `include/d3d8.h` by parsing the interface declaration order
(**not** from memory), then self-checked against the two slots this project had
already verified independently: `GetAdapterMonitor` = `0x38` (slot 14) and
`CreateDevice` = `0x3C` (slot 15). Both matched, so the table is anchored.

| Method | Offset |
|---|---|
| `Present` | `0x3C` |
| `BeginScene` / `EndScene` / `Clear` | `0x88` / `0x8C` / `0x90` |
| `SetTransform` | `0x94` |
| `SetViewport` | `0xA0` |
| `SetRenderState` | `0xC8` |
| `SetTexture` | `0xF4` |
| `DrawPrimitive` / `DrawIndexedPrimitive` | `0x118` / `0x11C` |
| `DrawPrimitiveUP` / `DrawIndexedPrimitiveUP` | `0x120` / `0x124` |
| `SetVertexShader` (FVF) | `0x130` |
| `SetStreamSource` | `0x14C` |
| `SetIndices` | `0x154` |

### Call-site census (669 indirect device-vtable calls in `.text`)

Census file: `analysis/research-pass-2026-11-02/d3d8-callsite-census.json`.

| Slot | Sites | | Slot | Sites |
|---|---|---|---|---|
| `SetRenderState` | 335 | | `Present` | 16 |
| `SetTransform` | 56 | | `Reset` | 13 |
| `SetStreamSource` | 53 | | `DrawPrimitive` | 7 |
| `SetVertexShader` | 47 | | `BeginScene` | 6 |
| `SetIndices` | 39 | | `EndScene` | 5 |
| `DrawIndexedPrimitive` | 39 | | `Clear` | 3 |
| `SetTexture` | 31 | | `SetViewport` | 2 |

The renderer is a tight cluster at **`0x46F800–0x479000`**, with further draw code at
`0x48D000–0x491300`. `DrawPrimitiveUP` / `DrawIndexedPrimitiveUP` have **zero**
sites — everything is indexed through buffers.

### The FVF answer: world-space for 3D, pre-transformed only for 2D

`SetVertexShader`'s FVF is an immediate push, so all 47 sites were decoded
statically (41 resolved, 6 computed):

| FVF | Sites | Decode | Meaning |
|---|---|---|---|
| `0x142` | **28** | `XYZ \| DIFFUSE \| TEX1` | **world-space 3D geometry** |
| `0x144` | **12** | `XYZRHW \| DIFFUSE \| TEX1` | pre-transformed 2D / HUD |
| `0x002` | 1 | `XYZ` | world-space, untextured |

So the dominant path is world-space, and the pre-transformed path is confined to
the 2D overlay — the same split NieR has. Real 3D Rayman geometry is available to
a path tracer; only the HUD would be skipped.

### Verified draw sequence (one path, `FUN_00472740`)

```c
SetTransform(device, 0x100 /* D3DTS_WORLD */, matrix);   // world matrix built per draw
SetIndices(device, DAT_00637dc4, DAT_007eb1f0);
SetStreamSource(device, 0, DAT_005f5df0, 0x18);          // stride 24
SetVertexShader(device, 0x142);                          // XYZ|DIFFUSE|TEX1
DrawIndexedPrimitive(device, 4, 0, 4, 0, 2);             // TRIANGLELIST, 2 tris
```

Stride `0x18` = 12 (XYZ) + 4 (diffuse) + 8 (one UV), which matches FVF `0x142`
exactly — an independent arithmetic check on the decode, not an assumption.

**Key globals:** `IDirect3D8*` at `0x5E6C30`; `IDirect3DDevice8*` at `0x7EDB18`.

### Limits of Phase 1

- FVFs are recovered from immediate pushes in the static image; a computed FVF
  (6 sites) could differ at runtime. Draw-time logging remains the confirmation gate.
- This establishes the *available* paths, not per-frame draw counts or which path
  renders which object.
- The verified sequence is one small path (2 triangles); the world-geometry path
  still needs the same treatment.
- Nothing was executed: no game run, no emulation, no patching.

## Proposed next steps after Phase 1

1. Map the `0x142` world-geometry draw path (likely `0x48D000–0x491300`) to find
   where world/view matrices are computed and stored, for camera capture.
2. Identify the view and projection `SetTransform` sites (`D3DTS_VIEW` = 2,
   `D3DTS_PROJECTION` = 3) — Phase 1's sample only showed `D3DTS_WORLD`.
3. Only then consider draw-time confirmation and a NieR-style live transfer.

## Phase 2 — camera anchor

`SetTransform` state arguments decoded at all 56 sites (immediate pushes). Exactly
one resolves cleanly to **`D3DTS_VIEW` (state 2)** at `0x492870`. No clean
`PROJECTION` site was found; states `0x10`/`0x11` appearing at 28 sites are almost
certainly heuristic noise (unrelated pushes caught by nearest-immediate scanning),
not real transform states. Treat those as unresolved.

### `FUN_00492740` — the view-matrix builder

Evidence: `ev_d92df5e2…` (assembly, 99 lines). Signature `undefined4 FUN_00492740(int camera)`.

Observed in the disassembly and decompilation:

- Reads float/dword fields from the camera object at `+0x08, +0x0C, +0x10, +0x18,
  +0x1C, +0x20, +0x28, +0x2C, +0x30, +0x38, +0x3C, +0x40` — three axes plus
  translation, i.e. a camera basis.
- Negates `+0x08`, `+0x18`, `+0x28`, `+0x38` (or not) depending on a flag at
  `+0xD8`, and correspondingly sets `SetRenderState(D3DRS_CULLMODE = 0x16)` to
  `2` or `3` — a **mirrored-camera branch**, which is why the negation is
  conditional rather than a fixed transform.
- Builds a 4x4 matrix in stack locals (`local_40 … local_4`, with `local_4 =
  0x3f800000` = 1.0f, the homogeneous term), then calls helper `0x00588750` with
  `&DAT_007E6340`.
- `CALL dword ptr [ECX + 0x94]` at `0x492870` — `SetTransform` on the device in
  `ECX`.
- Calls `FUN_004928a0` immediately after — the likely projection/perspective
  stage, not yet analysed.

So the camera state is a live object whose basis is read per frame, and the view
matrix is composed into `DAT_007E6340` before being handed to D3D8. That is the
capture point a NieR-style transform reader would use — **identified, not yet
read at runtime**.

### Limits of Phase 2

- No `PROJECTION` site confirmed; `FUN_004928a0` is a candidate, not a finding.
- Field offsets are read from decompiler output; the semantic mapping (which field
  is eye vs. axis) is inference from structure, not verified against runtime values.
- Whether `DAT_007E6340` holds view, world-view, or a combined matrix is unresolved.
- Still no execution; nothing validated against a rendered frame.

## Phase 3 — projection found, world path characterised

Evidence: `ev_648c5999…` (`FUN_004928a0`), `ev_91d40706…` (`FUN_00490960`),
`ev_ea832776…` (`FUN_00588750`), `ev_39235cac…` (xrefs to `DAT_007E6340`).

### `FUN_004928a0` sets `D3DTS_PROJECTION`

```c
local_18 = *(float *)(param_1 + 100) / (*(float *)(param_1 + 100) - *(float *)(param_1 + 0x60));
local_8  = (int)(*(float *)(param_1 + 0x60) * local_18) + -0x80000000;
iVar2    = (**(code **)(*DAT_007edb18 + 0x94))(DAT_007edb18, 3, local_40);   // state 3 = PROJECTION
```

So the **projection matrix is set explicitly**, with `state = 3`. Phase 2's
immediate-push scan could not see it because the state and matrix are both computed
locally rather than pushed as literals — the same reason 6 other sites read as
`unresolved`. That gap is now closed by function-level analysis, not by re-guessing.

Near/far plane inputs live in the camera object at `+0x60` and `+0x64` (`100`
decimal), which is the pair the perspective ratio is built from. This is structural
inference from the arithmetic, not a runtime-read value.

### Camera pipeline, now complete end to end

| Stage | Function | Call |
|---|---|---|
| View | `FUN_00492740` | `SetTransform(device, 2 /*VIEW*/, matrix)` @ `0x492870` |
| Projection | `FUN_004928a0` | `SetTransform(device, 3 /*PROJECTION*/, matrix)` |
| World | per-draw | `SetTransform(device, 0x100 /*WORLD*/, matrix)` |

`DAT_007E6340` (the composed camera matrix) has exactly two xrefs: `0x49282C`
(inside the view builder) and `0x59083D`. Its exact role — view, or world×view — is
still unresolved.

### `FUN_00490960` — the batched world-geometry path

```c
SetStreamSource(device, 0, DAT_00637dc0, 0x18);   // stride 24
SetIndices(device, DAT_00637dc4, 0);
SetVertexShader(device, 0x142);                    // XYZ|DIFFUSE|TEX1
DrawIndexedPrimitive(device, 4, 0, DAT_00637dbc, 0, DAT_00637dbc >> 1);
```

This is a **batching renderer**: vertices accumulate into buffer `DAT_00637DC0` with
a running count `DAT_00637DBC`, flushed when the count exceeds `0x7CB`, and each
strip emits `count >> 1` triangles of `TRIANGLELIST`. That is the world geometry
path, and it confirms `0x142` is the format real scene content uses — not just the
small immediate path Phase 1 sampled.

### Limits of Phase 3

- `DAT_00637DBC >> 1` as primitive count implies an index/vertex pairing convention
  I have not verified; do not rely on it as a triangle count formula yet.
- No runtime values: near/far, FOV and the actual matrices are unread.
- `FUN_00588750` (489 instructions, 8 callees) is a large matrix utility; its
  semantics were not decoded beyond being called from the view builder.
- Nothing executed; no frame has been compared against these matrices.

## Limits of this pass

- Static only; nothing executed, so no runtime confirmation of FVF, draw path or
  which `CreateDevice` branch was taken.
- Ghidra pseudocode is a decompiler representation, not original source.
- `find_xrefs_to_name` cannot resolve imported API names on this provider; the
  anchor chain here uses concrete addresses and dossiers instead.
- Procedure identification is by address only; the names are Ghidra defaults
  (`FUN_*`), not recovered symbols.
