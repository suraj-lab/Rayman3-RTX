# Light submission attempt — 2026-11-02 (negative result)

## What was tried

The proxy now hooks `SetLight` (device slot `0xb0`) and `LightEnable` (`0xb8`), keeps
the game's `D3DLIGHT8` state, creates a matching `remixapi` light and submits it every
frame from an `EndScene` hook (`0x8c`). Layout of `D3DLIGHT8` (104 bytes) taken from
Wine's `d3d8types.h`; slot numbers from the verified `IDirect3DDevice8` table.

## What the runtime reported

```text
RemixAPI: InitializeLibrary -> 0  (0=SUCCESS)
RemixAPI: CreateLight=7069B450 DrawLightInstance=7069D0A0
RemixAPI: light[0] type=3 diffuse=(0.20 0.21 0.80) -> rc=0 handle=00000001
```

- `CreateLight` and `DrawLightInstance` exist and return success.
- The game sets exactly **one** fixed-function light: **directional**, diffuse
  `(0.20, 0.21, 0.80)` — a cool blue moonlight, consistent with the reference
  screenshot of the Fairy Council exterior.
- No other light index is ever enabled, so the warm torches in the reference art are
  **emissive geometry**, not D3D lights.

## Result: no visible change

The operator reports the lighting looked the same as before this change. So a light
created through the API and drawn every frame did **not** affect the rendered image in
this configuration.

The honest readings of that, not yet distinguished:

1. The path-traced scene that reaches the screen is produced by the runtime's own
   compatibility handling of the game's draws, and API-submitted lights do not
   illuminate it unless they belong to the same submitted scene.
2. The light was accepted but is too dim / outside the visible range to matter
   (`0.20, 0.21, 0.80` radiance is modest).
3. Submission happens on the wrong thread or before the scene exists.

**Decisive next experiment:** submit a deliberately absurd light (very bright, e.g.
radiance 50 in pure red) and see whether the image changes at all. If nothing changes,
API lights simply do not reach the compat scene and the approach must change; if the
scene turns red, the mechanism works and it is a tuning problem.

## Defect introduced and not yet fixed

The game re-sets the light every frame. My change-detection uses `memcmp` over the
whole struct and still re-creates the light every frame — **145,456 `CreateLight` calls**
in one session, each allocating a runtime light object. Something in the struct varies
per frame and has not been identified. This is a performance/leak defect and must be
fixed before further light work: log a per-field diff to find which member changes, or
throttle re-creation.

## Also confirmed

- `SetupCamera` resolves to **NULL** in this build even though `remix_c.h` declares it.
  Camera submission cannot use that member; it must come from another path.
- Evidence: `tests/run_lights_9X15A6I/` (probe log, renderer logs, manifest with the
  installed DLL hash). The game was stopped after the run.
