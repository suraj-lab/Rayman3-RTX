# FIRST WORKING GAME RUN — 2026-11-02

## Outcome

Rayman 3 **ran** with RTX Remix attached, with no runtime-error dialog and no abort.
The operator confirmed visually: the game ran, but **no textures**.

This is the first run in the project in which the game got past device creation and
kept drawing. Everything before this checkpoint died in the Remix bridge's window
message handler.

Evidence: `tests/run_nowindowchanges_AKKDFHGu/` (`r3probe.log`, `remix_launch.log`,
`manifest.json` binding installed and source hashes).

Installed state during the run:

| File | SHA256 |
|---|---|
| `D3D8.dll` (our proxy) | `062d2c7423cc02eefd68af764b91b554e4e7599ece243a763c21769f1cfc8cb4` |
| `d3d9.dll` (Remix shim) | `a2cbebedf6cc495fa47feb79d197756242e547938784011a83f99b86458dc750` |
| `d3d8_real.dll` (d3d8to9) | `de81bdfcacef68c9aa59e54c053115bbc7732dca09838b5383e0a057b3db0eb0` |

Backup of the pre-change state: `backups/before_nowindowchanges_NKLvY_Cy/`.

## What made the difference

One behaviour flag, injected by our proxy:

```text
CreateDevice: adapter=0 type=1 behaviour=0x00000044 backbuffer=2880x1800 fmt=21 windowed=0
  R3_NO_WINDOW_CHANGES: behaviour 0x00000044 -> 0x00000844
CreateDevice -> hr=0x00000000
```

`D3DCREATE_NOWINDOWCHANGES` (0x800) stops the Remix bridge from doing window
management inside `gSwapChainMapMutex`. Root cause and A/B evidence:
`analysis/abort-fix-findings.md`.

## The renderer observations this run produced (real game, not the harness)

Transforms captured by the proxy: **VIEW ×5, PROJECTION ×5, WORLD ×30**.

Vertex formats actually used by Rayman 3:

| FVF | Stride | Decode | Path |
|---|---|---|---|
| `0x144` | 28 | `XYZRHW\|DIFFUSE\|TEX1` | 2D / UI |
| `0x352` | 52 | `XYZ\|NORMAL\|DIFFUSE\|TEX3` | 3D, lit, multi-texture |
| `0x312` | 48 | `XYZ\|NORMAL\|TEX3` | 3D, lit, multi-texture |
| `0x112` | 32 | `XYZ\|NORMAL\|TEX1` | 3D, lit |
| `0x142` | 24 | `XYZ\|DIFFUSE\|TEX1` | 3D, unlit — **matches the static prediction** |
| `0x002` | 12 | `XYZ` | 3D, position only |

This confirms the static analysis and settles the open question from Phase 1: the
world path really is world-space `XYZ` with normals, so Remix is receiving geometry
it can path trace. `0x142` appearing at stride 24 exactly as predicted is the
independent check.

Two entries (`0x8C1530D0`, `0x8BFB707C`) are not FVFs — those are D3D8 **vertex
shader handles** passed to the same `SetVertexShader` entry point. The proxy's
decoder should distinguish handle vs FVF before interpreting the value; recorded as
a known cosmetic defect in the probe log, not a rendering bug.

## Why it is untextured

```text
RemixAPI: InitializeLibrary -> 11  (0=SUCCESS, 11=NOT_INITIALIZED)
```

The RemixAPI client library is not initialised, so nothing is submitted to the
path tracer: no captured textures, no materials, no remix draws. The game is
rendering through the Remix Vulkan presenter with the game's own textures absent or
unconverted. This was already recorded as an open item in STATUS before this run;
it is now the single blocking issue for actually seeing ray-traced Rayman.

## Limits

- One run, operator-observed. No screenshot was captured by the tooling and no
  frame was validated against the captured matrices yet.
- The run ended after roughly 40 seconds of our monitoring; the process was gone and
  no `EXCEPTION`/`EXITPROCESS` trace was written by the proxy, so the exit cause is
  **unknown** (operator exit vs. crash is not distinguished by this evidence).
- Textures missing: cause not proven to be RemixAPI alone; uninitialised API is the
  established fact, the visual consequence is the operator's observation.
- Window management is now the game's responsibility (flag semantics); ALT+TAB and
  minimise/restore were not exercised.
