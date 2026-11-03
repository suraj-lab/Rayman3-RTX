# Darkness: what the 2026-11-03 run established

Run: `tests/run_lightintensity_20261103_1355` (proxy `ca56b32b`, set-light logging on, red-light switch off,
`rtx.lightConversionIntensityFactor = 10`, 2880x1800 fullscreen, 11,400 frames, 44,454 DrawLightInstance calls,
`InitializeLibrary -> 0`). Operator: "dim but not black". `user.conf` restored afterwards.

## Correction to earlier reasoning

The root `shot_*.png` files from 2026-11-01 are **desktop/terminal captures**, not game frames (I read
`shot_run1.png`: it is a PowerShell window running the capture script). Any brightness comparison against
them is meaningless, including one I made in this session. **There is no vanilla (non-Remix) baseline frame
in the repo.** Until we capture one, "too dark" has no measured reference.

## Ruled out / settled

- **Converted-light intensity is not the lever.** 10x via `rtx.lightConversionIntensityFactor` produced no
  visible change (frame mean luminance 29.7 vs the previous Remix frames; no baseline for the original).
  Remix's auto-exposure (`rtx.autoExposure`, present and enabled in this build) is the obvious reason a global
  intensity change gets normalised away.
- **The light-flood explanation.** 512 distinct SetLights: 510 type 3 (directional), 2 type 1 (point, range
  13.8, real attenuation). 30 colours, warm-dominant: `(0.25,0.16,0.08)` x214, `(0.25,0.15,0.08)` x201.
  **510 of 512 have garbage in `Range`** ( ~1.8e19): the game passes a partly-uninitialised `D3DLIGHT8`, which
  is why `memcmp` always differs and the proxy recreates the light every call. That is churn, not a leak.
- **Coordinate mapping.** SetLight positions span x -100..35, y -9..128, z -8..5; `tower_20`'s decoded light
  positions span x -207..58, y -105..108, z -398..134. Nearest decoded level light to any submitted light is
  32 units away: the game places per-object lights in world space, it does not feed level light structs to
  `SetLight` directly. Level data cannot supply positions for this.

## New hypothesis (not yet proven)

Rayman 3 bakes its lighting. World draws in this run: `FVF 0x112` (XYZ|NORMAL|**DIFFUSE**|TEX1) 312,504 draws,
`0x352` (XYZ|NORMAL|**DIFFUSE**|TEX3) 25,564, `0x142` (XYZ|**DIFFUSE**|TEX1) 29,691, `0x312` (XYZ|NORMAL|TEX3)
10,790. Most world geometry carries per-vertex DIFFUSE, i.e. baked illumination. A path tracer takes illumination
from real lights, not from a vertex colour channel, so the baked term is lost; the remaining converted lights are
few and dim. This fits "dim but not black" and would not be fixed by any light-intensity scaling of the API lights.

## Levers that actually exist in THIS build

From `.trex_arm64\d3d9.dll` strings (the docs I read describe a newer build; `rtx.skyAutoDetect`,
`rtx.autoExposure.enabled` and `rtx.exposure` are **not** in this runtime):

`rtx.lightConverter`, `rtx.lights`, `rtx.ignoreLights`, `rtx.lightmapTextures`, `rtx.ignoreBakedLightingTextures`,
`rtx.autoExposure`, `rtx.userBrightness` + `rtx.userBrightnessEVRange`, `rtx.tonemap`, `rtx.localtonemap`,
`rtx.postfx.exposureFraction`, `rtx.legacyMaterial.roughnessConstant`, `rtx.fallbackLightRadiance`,
`rtx.skyBoxTextures`, `rtx.skyDrawcallIdThreshold`, plus the four `lightConversion*` keys already known.

## Order of work

1. **Vanilla baseline (cheapest, no Remix).** `python r3mode.py normal`, reach the same grotto, capture the screen,
   then measure mean luminance and histogram against today's Remix frame. This sizes the real gap; everything else
   is guesswork until it exists. Revert with `python r3mode.py remix`.
2. **Red-light gate.** `run_redlight.cmd A 20`: does an API light change the path-traced image at all? Colour
   change survives auto-exposure, so a red tint is a valid signal. If API lights do not reach captured geometry,
   the fill-light approach below is dead and the fix has to come from Remix's own conversion/exposure side.
3. **If API lights work:** add a big dim fill light (dome or rect) standing in for missing ambient/sky, as the
   NieR project had to. If they do not: sweep `rtx.userBrightness`, `rtx.autoExposure`,
   `rtx.postfx.exposureFraction`, and consider `rtx.lightmapTextures` / `rtx.ignoreBakedLightingTextures` and the
   `rtx.legacyMaterial` emissive keys for the baked-light loss.

## Measured gap, same metric (2026-11-03)

Vanilla baseline: NVIDIA recording of the normal-mode run (2880x1800, 120 fps, 97.3 s); the first ~30 s is the
grotto with the tutorial prompt, the rest is the desktop once the game was closed. Metric: `ffmpeg signalstats`
YAVG / SATAVG on 320-px-wide frames at 1 fps, applied to the video and to both stills.

| | Luma | Saturation |
|---|---|---|
| Vanilla grotto (video, 0-30 s) | 63-70 | 16-21 |
| Vanilla still (`shot_10.png`) | 58.9 | 20.6 |
| Remix grotto (`remix_lightrun_02.png`) | 41.5 | 6.1 |

**Remix keeps about 65-70% of the vanilla luminance and about 30% of the saturation.** The defect is real and
now quantified: it is both a brightness loss and a colour collapse, which is what losing baked illumination while
keeping albedo textures looks like (dim light means low chroma everywhere).

Frames kept: `tests/baseline_normal_20261103/video/v005.png`, `v020.png` (grotto), `v035.png`, `v060.png`,
`v095.png` (desktop). Next step unchanged: the red-light gate (`run_redlight.cmd A 20`), then either an ambient
fill light (if API lights reach the scene) or exposure/`rtx.userBrightness` tuning plus baked-light handling.

## Emissive objects are the missing light (operator observation, 2026-11-03)

The operator pointed out that in vanilla most of the grotto's light comes from the objects themselves: the glowing
mushrooms, the red orbs, the teal crystal. That matches the measurement (saturation collapses to 6.1 in Remix), and
this build exposes legacy-emissive knobs, found as strings in `.trex_arm64\d3d9.dll`:

`enableEmissive`, `emissiveIntensity`, `emissiveColorConstant`, `boostEmissives`, `emissiveBlendOverrideEmissiveIntensity`,
`emissiveTextureSampleFootprintScale`, plus `userBrightness` + `userBrightnessEVRange`, `enableAutoExposure`,
`exposureBias`, `exposureFactor`, `skyBrightness`.

Note the option-name shape: the DLL stores `rtx.legacyMaterial` and the bare field names separately, so
`rtx.legacyMaterial.enableEmissive` / `.emissiveIntensity` / `.emissiveColorConstant` are the expected keys, and
`rtx.userBrightness.userBrightness` is already written by the runtime itself (value 50).

Two candidate tests, in order of usefulness:

- **E1 (emissive, addresses the real cause).** `rtx.legacyMaterial.enableEmissive = True`,
  `rtx.legacyMaterial.emissiveIntensity = 1`, `rtx.legacyMaterial.emissiveColorConstant = 1, 1, 1`.
  Risk: legacy materials carry no emissive mask, so the constant may light everything uniformly (white-out).
  That is itself informative: it shows the path is unmasked and the intensity has to be dialled down.
- **B1 (cosmetic fallback).** `rtx.userBrightness.userBrightness = 70` (default 50): raises tonemapped brightness
  without touching lighting. Makes the frame look closer to vanilla but does not restore colour or bounce light.

Tooling: `analysis/tools/r3conf.py apply --set "key=value" ...` (repeatable, whitelisted keys, backup + `restore`).

## E1 result: emissive works but is unmasked (2026-11-03, run_emissive_20261103)

All three keys were accepted (echoed in `remix_launch.log` at 14:04:23). Measured with the same signalstats metric:

| | Luma | Sat |
|---|---|---|
| Vanilla grotto | 63-70 | 16-21 |
| Remix before emissive | 41.5 | 6.1 |
| **Remix, emissive on, intensity 1, white constant** | **117-130** | **15-31** |

Saturation is restored (15-31, vanilla 16-21) and brightness overshoots by about 2x. The frames show why: every
surface glows uniformly, so the scene reads as a flat wash (Rayman and the cave lit from everywhere at once). The
constant is applied per non-replaced material with no mask, so this knob cannot make *only* the mushrooms and orbs
glow; it is a global fill.

Two ways to use that:

1. **As a deliberate ambient fill**, tuned down: `emissiveIntensity` around 0.2-0.3 should land near vanilla luma
   without the wash. Cheap, runtime-only, no API needed. Colour can be tinted if the result reads too grey.
2. **Object-local light** (the orbs and mushrooms actually lighting their surroundings) still needs working API
   lights or replacement emissive materials, so the red-light gate stays relevant.

Also recorded: the game wrote `Rayman3.exe.3144.dmp` at 14:06:24 with `common device objects were not disposed of`,
i.e. an exit-time crash; cause unverified and the same message appeared in earlier runs.

`user.conf` restored to pristine (`efcb8a33`) after the test; evidence in `tests/run_emissive_20261103/`.

## Playable session, new area (2026-11-03, run_play_20261103)

With the DualSense working (as a DirectInput pad) the operator reached a new area. Config was **pristine** - no
emissive or light-conversion keys - so this is the untouched Remix look in a different room.

Per-5 s signalstats from the 45.6 s recording: luma 37-81, saturation 7.6-38.1 (vanilla grotto reference: 63-70 /
16-21). Two things stand out in the frames:

- The purple cave with torch glows and teal crystals (t~25 s) reads much better than the grotto: brightness and
  colour are in the right family. So the dark result is not uniform across the game.
- Large surfaces come out pure white and blown (snow platforms, leaves), and at t~44 s an object renders as
  white with black swirls. Those look like texture/vertex-colour handling, not lighting: worth a separate look
  before tuning lights further.

Controller: DirectInput enumerates the DualSense with or without ribshark's fix, so the fix is a no-op for this
pad; the remaining problem is mapping ("works but doesn't feel like a controller"), which the dinput8 wrapper or
BR3_Config axis assignment can address.
