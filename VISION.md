# Vision

## What this is

Get Rayman 3 (Ubisoft, 2003, D3D8) rendered by RTX Remix on Windows on ARM, starting
from a game that was never supported by it.

## In scope

- Making the game run reliably through the Remix bridge on this machine.
- Understanding the game's own renderer well enough to supply what Remix needs
  (camera, world-space geometry, UVs, materials).
- Recording evidence for every claim, with hashes, so results are reproducible.

## Out of scope

- Shipping a playable mod. No distributable asset pack is planned.
- Redistributing game assets.
- Changing the game's gameplay, TnL settings or save data.
- Modifying the Remix runtime itself.

## Success criteria, in order

1. Game starts and reaches gameplay without an abort. **Done (2026-11-02).**
2. World geometry draws with the game's own textures bound (UVs resolved).
3. Captured view/projection verified against a rendered frame.
4. Path-traced output — the actual point of the exercise. Not started.

## Risks accepted knowingly

- The startup fix relies on `D3DCREATE_NOWINDOWCHANGES`, which makes window/focus
  management the game's job; ALT+TAB and minimise behaviour are unverified.
- Rayman 3's world path may use vertex-shader variants that the runtime's
  compatibility path does not map to UVs; if so, the fix may require submitting
  geometry ourselves through `remixapi_DrawInstance`.
- Desktop screen capture has crashed the ARM64 bridge server twice. Capture is a
  known hazard here, not a neutral observation tool.
