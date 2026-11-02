# Rayman 3 RTX — running Rayman 3 (2003) through RTX Remix

Status: **the game runs under RTX Remix with no startup abort.** The 3D world renders
through the path-traced runtime but comes out largely untextured, and Remix logs
`Trying to bind a texture to a mesh without UVs`. No ray-traced *assets* exist yet;
this repo is the investigation, not a mod.

Rayman 3 is a D3D8 title driven through `D3D8 -> d3d8to9 -> Remix d3d9 shim -> ARM64
bridge`. There is no public engine source, so everything below comes from the shipped
binaries, NVIDIA's release symbols, and controlled runs.

## Status

| Area | Status |
| --- | --- |
| Startup abort | **Fixed.** The Remix bridge held `gSwapChainMapMutex` across fullscreen window management; a nested lifecycle message re-entered `windowMsg` and the non-recursive CRT mutex threw `std::system_error` → CRT abort. The proxy now injects `D3DCREATE_NOWINDOWCHANGES`, proven by A/B and inversion. |
| Game runs | **Yes** — first working run 2026-11-02, operator-confirmed. |
| 3D world | Renders, but surfaces are effectively untextured/black. |
| Textures / materials | **Not working.** Renderer log: `Trying to bind a texture to a mesh without UVs. Was this intended?` PBR is not the bottleneck while UVs are absent. |
| Camera capture | VIEW / PROJECTION / WORLD captured live (5 / 5 / 30 calls in the first run). |
| Geometry submission | Not implemented. The runtime consumes the game's own draws; nothing is submitted through `remixapi_DrawInstance` yet. |
| Ray-traced output | **None.** Not claimed anywhere in this repo. |
| Runtime stability | The ARM64 bridge server died with `0xc0000005` in both long runs, coinciding with desktop screen capture. Unrelated to the game's own exit. |

## How it works

`r3mode.py` switches the game between the normal RayGfx/dgVoodoo stack and the Remix
chain:

```text
python r3mode.py remix     # D3D8.dll = our proxy -> d3d8_real = d3d8to9 -> d3d9 = Remix -> NvRemixLauncher32
python r3mode.py normal    # restore dgVoodoo + BetterRayman3
python r3mode.py status
```

Remix API is enabled per-game in `.trex/bridge.conf` and `.trex_arm64/bridge.conf`
(`exposeRemixApi = True`); without it `remixapi_InitializeLibrary` returns
`11 NOT_INITIALIZED`.

## Documentation

Start with `STATUS.md` — it is the authoritative resume record.

| Topic | Document |
| --- | --- |
| Startup abort: cause, A/B, inversion | `analysis/abort-fix-findings.md` |
| First working run, live captures | `analysis/first-working-run.md` |
| Rayman3.exe renderer map (static) | `analysis/rayman3-renderer-recon.md` |
| Matching the shim's release symbols | `analysis/shim-symbol-findings.md` |
| Mod hook audit | `analysis/mod-hook-analysis.md` |
| Methodology borrowed from the NieR project | `analysis/methodology-reuse-from-nier.md` |
| Earlier probes and negative results | `analysis/lifecycle-probe.md`, `analysis/fullscreen-probe-findings.md`, `analysis/exit-trace-findings.md` |
| Light submission attempt (negative result) | `analysis/light-submission-attempt.md` |
| Plan: read level light/texture data via RayCarrot libraries | `analysis/raycarrot-data-plan.md` |

## Repo layout

| Path | Contents |
| --- | --- |
| `src/` | The D3D8 proxy, the standalone ABI/lifecycle harness, the RemixAPI probe |
| `tests/` | Runners (`run_abi.py`, `run_remix_abi.py`) and per-run evidence |
| `analysis/` | Findings, evidence manifests, pinned third-party source snapshots |
| `r3mode.py` | Switches the game between the normal and Remix stacks |

Binaries, staged Remix runtimes, installers and `backups/` are deliberately not
tracked; see `.gitignore`.

## What is *not* claimed

- No ray-traced frame, no textures, no materials.
- Captured matrices have not been validated against a rendered frame.
- The exact nested message that triggered the original abort was never captured; the
  mechanism is established by intervention, not by an observed stack.
- Rayman 3 assets are not redistributed here.
