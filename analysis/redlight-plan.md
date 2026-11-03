# Light experiments: validated plan (2026-11-03)

Status: **prepared, not run.** Proxy built (`src/d3d8proxy.dll`, see hash in the session log / manifest at deploy time),
not deployed. No game launch yet.

## What the online check established (sources fetched 2026-11-03)

1. **Remix already converts the game's fixed-function lights.** "During scene capture, all original game lighting is
   converted to path-traced lights ... Directional lights are converted to distant lights in Remix."
   https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/howto/learning-lighting.html
2. **The conversion uses fixed values, not the game's brightness**: `rtx.lightConversionDistantLightFixedIntensity`
   default 1 (W/sr), `rtx.lightConversionDistantLightFixedAngle` default 0.0349 rad (~2 deg),
   `rtx.lightConversionSphereLightFixedRadius` default 4 (point/spot -> sphere).
   https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/runtimeinterface/renderingtab/remix-runtimeinterface-rendering-lightconversion.html
   (The local shim source also marshals `SetLight`/`LightEnable` to the Remix server: `bridge/src/client/d3d9_device.cpp`,
   pinned tag remix-1.6.1.)
3. **Radiance scale**: "recommended to use light radiance values around 1.0 ... values up to approximately 1000 generally
   produce reliable results, while values exceeding 10000 may introduce lighting artifacts"; the result also depends on
   tonemapper exposure (same learning-lighting page).
4. **API light semantics**: `.hash` "identify the light source between the current and previous frame for a more stable
   denoising / light sampling"; to update a light, `DestroyLight` + `CreateLight` with the same hash; push lights each
   frame with `DrawLightInstance`.
   https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/documentation/RemixSDK.md

## What this changes

- Our earlier proxy lights were **added on top of** a light Remix had already created from the same `SetLight`, at radiance
  0.2-0.8 against a converted intensity of 1. A <= 20-80% extra on one light is plausibly invisible. This explains the null
  result without the API being broken. It was never evidence that API lights do not work.
- "Too dark" is therefore probably the *converted* lights being dim: Remix uses a fixed intensity of 1, scaled by whatever
  colour the game sets, and the game's colours in the second scene are tiny ((0.02,0.06,0.04), (0.06,0.18,0.35)).
- Per-object `SetLight` with one constant hash (`idx + 1`) for different lights is not what the docs describe (hash = a stable
  identity per light). Not proven harmful, worth fixing: derive the hash from light content.
- Not found online: any statement that API lights skip legacy-captured geometry (nothing says they do).

## Revised order for the afternoon (each step needs your OK; none is a long run)

0. Prereq: free memory (close Firefox/Spotify/Zoom, ideally Bloomberg + RDP), no screen capture/recording.
1. **Config knob first (no code change).** Back up `user.conf`, then add
   `rtx.lightConversionIntensityFactor = 10` ("Scales the converted light intensities"; `python analysis/tools/r3conf.py apply`) and launch the existing proxy build unchanged. If the scene brightens,
   darkness was the conversion intensity, and no API light is needed. Restore `user.conf` afterwards.
   (Option names verified 2026-11-03 in RtxOptions.md and as strings in both shipped `.trex` and `.trex_arm64` `d3d9.dll`. Other community Remix mods also keep `rtx.*` keys in `user.conf`; that this exact runtime reads the game-folder `user.conf` is still unproven until the first run.)
   Other converter knobs: `rtx.lightConversionDistantLightFixedIntensity` (default 1), `...SphereLightFixedRadius` (4), `...MaxIntensity` (default unbounded). Legacy material defaults: roughness 0.7, metallic 0.1, emissive intensity 0.
2. **Red-light test** only if step 1 is inconclusive: deploy the new proxy, `run_redlight.cmd A 20`
   (distant, radiance 20, 2 deg disc = the converter's own angle; only radiance differs from the converted light).
   The log must show `R3_REDLIGHT mode=A`, `CreateLight rc=0`, and the `DrawLightInstance calls so far` counter rising.
   If nothing changes, try `B 20 5 15`, then `B 20 5 -15` (view handedness unverified).
3. Always collect: `r3probe.log`, Remix logs, hashes into `tests/run_*` with a manifest.

## Skeptic review notes (DeepSeek reviewer, no web access in the workflow agents; lead re-checked)

- Variant A originally changed three variables (radiance, disc size, volumetric scale); fixed: disc now 2 deg, env-tunable.
- Variant B can silently create no light (needs a VIEW first) and sits at the position of the latest recreation; the new
  `DrawLightInstance` counter makes a missing light visible.
- The agents' claims that could not be sourced (firefly-clamp constant, lack of a `hash` field) were wrong or unverifiable;
  only the sources above are relied on.
