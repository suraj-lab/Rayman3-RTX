# Methodology reuse: NieR → Rayman 3

Source examined: public repo `suraj-lab/Nier-Automata-Rendering` at `main`
(`VISION.md`, `PROGRESS.md`, `analysis/LIVE_TRANSFER.md`, `analysis/INTEROP_API.md`,
`analysis/REMIX_FEASIBILITY.md`, `analysis/LAPTOP_PORT.md`). Nothing was executed;
this is a read of published method and results.

Rayman 3 is a **different engine and a different problem** from NieR, so the reuse
is method, not code. Two of the biggest NieR artefacts — CPU skinning and the
skinned-pose churn — have no analogue in Rayman's fixed-function path found so far.

## Reusable as-is

| NieR practice | Why it transfers | Rayman 3 status |
|---|---|---|
| **"The binary is the cheaper oracle"** — read the shipped binary before source, because source and build can disagree | Rayman has no public source at all; the exe is the only authority | Used: the renderer map came entirely from the exe |
| **Pinned sources with commit SHAs** (NieR's S1–S6) | Prevents re-deriving against a moving upstream | Used: Wine `d3d8.h` parsed for vtables; dxvk-remix tag pinned at `0a6cd5b1…`; release symbols pinned by SHA256 and PDB GUID |
| **Self-check an ABI table against an independently known slot** | NieR's reviewer echoed a wrong vtable slot (19 vs 20) | Used: parsed offsets were checked against this project's verified slots 14/15 before any site was decoded |
| **"Rules to not rediscover"** — record hard-won ordering constraints | Rayman already has two: `windowMsg` holds `gSwapChainMapMutex` across `SetWindowPos`; `d3d8d.dll` is only `DebugSetMute` | Partially written; belongs in the renderer doc |
| **Negative results, settled — do not re-run** | Prevents repeating dead ends | Used: `Camera_*` = Ubi.ini inversions; `d3d8d.dll`; `DrawPrimitiveUP` has zero sites |
| **A control that can fail independently of the claim** (NieR's `ColorFill` readback control) | Distinguishes "the thing is broken" from "our measurement is broken" | Used: FVF decode cross-checked arithmetically (stride `0x18` = 12+4+8) and by the ThrowInfo byte fingerprint |
| **Checkpoint document as the authority** (`PROGRESS.md` top section) | Long, interrupted work | `STATUS.md` + `analysis/*.md` already serve this role |

## Reusable with adaptation

| NieR practice | Adaptation for Rayman 3 |
|---|---|
| Live transfer ordering: **create and register our device before `Startup`**, because afterwards `ALREADY_EXISTS` is permanent | Rayman's path differs: Remix owns `d3d9.dll` and the bridge already registers the game's own device. Do not copy the NieR ordering blindly; the equivalent question here is whether Remix accepts the game's device without a second registration. Unverified. |
| Camera extraction from a live structure | Now anchored: `FUN_00492740` reads a camera basis at `+0x08…+0x40` and composes into `DAT_007E6340` before `SetTransform(D3DTS_VIEW)`. |
| Frame-time attribution before optimising | Not yet applicable — Rayman has no live draw stream. |
| Textures/materials coverage accounting | Not yet applicable; Rayman's material path (SetTexture, 31 sites) is unmapped. |

## Not reusable

- **Skinning and pose churn.** Rayman's world path uses `XYZ|DIFFUSE|TEX1`,
  24-byte vertices with no bone indices or weights. There is no skinning transfer
  to port.
- **DX11/D3D11 proxy, FSR4, DLAA, frame generation.** Rayman is D3D8 through a
  D3D8→9→Vulkan bridge; none of that stack applies.
- **Interop API findings** (`dxvk_*` members, `CopyRenderingOutput` ordering).
  Those describe Portal RTX's external-swapchain path. Rayman's bridge uses the
  in-process RemixAPI path instead, which is a different surface — this project
  already recorded `remixapi_InitializeLibrary -> 11 (NOT_INITIALIZED)` in the
  harness, so even that is unresolved here.

## What this changes about the plan

NieR proves the end state is reachable, but it reached it with a **live draw
stream and a camera**. For Rayman, both are now statically located and neither is
validated at runtime:

1. World geometry: FVF `0x142`, 28 sites — Rayman submits what Remix wants.
2. Camera: `FUN_00492740` → `D3DTS_VIEW`; projection still unidentified.
3. Blocking reality: **the game currently aborts before any draw**, on a shim
   `std::system_error` from the window-message lock. That is still the gate in
   front of every rendering step, and it is unaffected by this pass.

So the honest sequencing is: fix/understand the abort, then confirm the FVF and
camera at runtime, then consider transfer. Doing the renderer map first was still
worth it — it establishes that the effort is not wasted once the abort is solved.
