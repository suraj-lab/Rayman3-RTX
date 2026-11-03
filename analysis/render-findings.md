# Render findings, 2026-11-03 session (measured from recordings)

## Areas that are correct

Remix reaches vanilla parity in several rooms (vanilla baseline: luma 63-70, sat 16-21):

| recording | window | luma | sat |
|---|---|---|---|
| 17:24 clip | 60-80 s | 63.1 | 22.2 |
| 17:24 clip | 220-240 s | 61.6 | 21.6 |
| 17:24 clip | 260-280 s | 62.0 | 19.6 |

So the renderer is capable of the right result; the defect is localised, not global.

## The bad section is the last part of the level (operator-confirmed)

17:24 clip (292.7 s), top black band measured as fully-black leading rows of 200:

| time | top black rows | share of frame |
|---|---|---|
| 250 s | 31 | 16% |
| 260 s | 82 | **41%** |
| 280 s | 53 | 27% |
| 290 s | 51 | 26% |

Luma/sat for 280-293 s: **34.8 / 4.9** (dark and nearly grey).

**Operator confirms the game was fullscreen throughout.** That rules out the alternative explanation I raised
(window positioned on the desktop) and means the band is a rendering artifact: the presented image is displaced
downwards in that area.

Related earlier evidence: three of five captured VIEW matrices had determinant -1 (mirrored basis), and
`Rayman3.exe` contains `_REFLECT1`, `_REFLECT2`, `_reflect1`, `_reflect2`, `watereffect`, `waterfx`, i.e. it draws
planar reflections into render-target textures. `rtx.raytracedRenderTarget.enable` defaults to True here.

## Hotkeys for this build (from its own option text)

| key | action |
|---|---|
| **Ctrl+Shift+Q** | capture hotkey (`rtx.captureHotKey` default `CTRL,SHFT,Q`) |
| Alt+X | Remix developer menu |
| F10 | **not** a capture key in this build - do not use |

`rtx.captureShowMenuOnHotkey = False` is now set in `user.conf`, so Ctrl+Shift+Q should start a capture directly
instead of only opening a menu. As of this session `rtx-remix/captures/` still contains no files, so the hotkey has
not yet been exercised.

## Process rules for this project

- Provide the hotkey list BEFORE launching, and wait for the operator's explicit go-ahead before launching the game.
- Never sample a recording at long intervals and infer short events; analyse at 1-2 s when looking for toggles.
- An in-progress mp4 cannot be read; the recording must be stopped first.

## Why Ctrl+Shift+Q produced no capture (2026-11-03)

The hotkey works: the runtime logged 63 times

    warn: [GameCapturer] Cannot begin capture while replacement assets are enabled.

`rtx.enableReplacementAssets` was True. Captures are refused in that state, which is why `captures/` stayed empty
through every attempt (F10 earlier was never the binding; this was the real blocker). Set
`rtx.enableReplacementAssets = False` for a capture run; it takes effect on the next launch.
