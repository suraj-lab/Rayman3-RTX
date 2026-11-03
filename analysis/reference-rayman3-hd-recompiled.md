# Reference: Rayman 3 HD Recompiled (`MysteriousAeon/rayman3-hd-recompiled`)

Saved 2026-11-03 as a cross-reference for this project. **Not** part of the Remix pipeline and not a
replacement for it — see §3. Kept because it documents, by name, several visual defects the Remix path
is also fighting, and because its recompiled lighting code is ground truth we do not have.

Upstream: <https://github.com/MysteriousAeon/rayman3-hd-recompiled>

---

## 1. What it is

A **native Windows port of Rayman 3 HD for Xbox 360**, produced by **static recompilation** — the 360
machine code is translated to C and runs directly, no emulator.

| item | detail |
|---|---|
| Source game | **Xbox 360 HD** release (Xbox Live Arcade, title ID `5841123F`), **without** a title update |
| Install | Installer uses *your own* 360 package; package is read-only, never modified |
| Toolchain | **ReXGlue SDK** (BSD-3, includes Xenia-derived code); XenonRecomp and UnleashedRecomp as references |
| Renderer | **Direct3D 12** (+ DirectXShaderCompiler) |
| Requirements | Windows 10/11 **64-bit**, VC++ 2015–2022 x64 redist, **D3D12-capable GPU**, ~2.3 GB free |
| Licence | Project code **MIT**; game-derived translations generated locally and **not** in the repo; game IP Ubisoft/Microsoft |

Dependencies of note: SDL 3, Dear ImGui, FFmpeg, libmspack, CPython, spdlog, toml++.

---

## 2. Features relevant to this project

### 2.1 HD+ corrections — the useful part

Settings → **Game profile**: `Vanilla` (original presentation + compatibility fixes), `HD+` (adds
visual corrections), `Custom` (individual toggles under Settings → Corrections).

| correction | what it fixes |
|---|---|
| **Correct HD Lighting Colors** | *"Removes the unnatural pink tint in Hoodlum Headquarters and the Tower of the Leptys."* |
| **Correct Sky Colors** | *"Restores the intended sky colors in Clearleaf Forest and the Bog of Murk."* |
| Separate texture coordinates for two-layer effects | corrects texture coordinate handling for layered effects |
| Correct Charge Trail Colors | restores Rayman's fist charge-trail colours |
| Correct Robo-Pirate statue textures | fixes four upside-down statue textures in Hoodlum Headquarters |
| Combo Meter Animation | smooth closing animation instead of abrupt disappearance |

**Why this matters here:** these are the same *class* of defect we have been chasing in Remix — a
colour cast attributed to the game's own lighting path, and sky colours that are wrong. The author
found them to be **inherited from the Xbox 360 HD release** and fixed them in the game's code, not with
a renderer setting. That is a second data point that Rayman 3's shipped lighting/colour data is not
trustworthy as an albedo/multiplier source, which is what our B/R measurements concluded from the
other direction.

### 2.2 Controller support

Native **DualSense**: dynamic lightbar colours, adjustable adaptive-trigger resistance, optional
controller-speaker effects, plus Xbox-compatible pads and fully remappable bindings. Also mouse+keyboard
with a Look Mode and two keyboard layouts.

This covers the workstream we were solving with ViGEm (`src/vigem_feeder.cpp`, `play-remix-pad.cmd`) —
natively, no virtual pad, no resident process.

### 2.3 Mod support

Settings → Mods: install/enable/disable/prioritise mods; **textures, text and font glyphs, subtitles**.
Explicitly **cannot** replace models, levels, audio, scripts or executable code. Mods are validated
against supported game files. Authoring guide ships as `tools/installer/MODDING.md`.

Useful contrast with Remix: ours replaces materials via USD references and needs `enableReplacementAssets`;
this replaces texture *files* inside the game's own package layout.

### 2.4 Known limitations (upstream's own list)

- Full playtest completed at v1.0.1; later releases get automated regression + install testing, **not**
  a full playthrough each time.
- **Hardware compatibility testing is limited** — developer's Windows 11 machine plus some external testers.
- Some inherited 360 HD visual issues remain beyond what HD+ corrects.
- A **rare audio-initialisation crash** with unknown cause.
- Holding Walk while swimming can block forward movement.
- Changing Windows display scaling with Settings open can leave a visual artifact (reopen Settings).

---

## 3. Why it is not a Remix path

| | Recompiled | our stack |
|---|---|---|
| game data | Xbox 360 HD package (XEX/STFS) | PC GOG build; `.cnt` / `.gf` / `.lvl` |
| renderer | **D3D12** with translated Xenos shaders | D3D8 → `d3d8to9` → Remix `d3d9` shim → path tracer |
| Remix injectable | **no** — Remix needs D3D8/D9 draw calls | yes |
| architecture | **x64 only** | native ARM64 bridge (`.trex_arm64`) |

Consequences:

- **No path tracing, no Ray Reconstruction, no DLSS** in the Recompiled build.
- On this laptop (Windows **ARM64**) it would run under x64 emulation; D3D12 + translated shaders under
  emulation is the least favourable case for the RTX Spark GPU. Unverified — would need a launch test.
- **Our offline corpus does not transfer.** 4,074 decoded `.gf` textures, 13,211 decoded level-light
  candidates, the capture hash→name matcher and `level-metrics.csv` all describe the **PC D3D8 build**
  and its container formats. The 360 package is a different format entirely.

---

## 4. What to take from it

Ordered by value to this project:

1. **`docs/PROVENANCE.md`** — how the lighting/colour corrections were derived, and therefore what the
   *intended* colours are. Directly addresses our open hue question (§4 of
   `session-findings-20261103.md`).
2. **The "Correct HD Lighting Colors" correction itself** — reads as a fixed tint applied at a known
   place in the game's lighting path. If it is a per-level colour constant, that tells us whether the
   blue we measure is authored data or an emergent consequence of gamma-space vertex lighting read as
   linear (our current conclusion). **This is the ground truth our six hours of measurements could
   not supply.**
3. **Correct Sky Colors** for Clearleaf Forest / Bog of Murk — a worked example of the sky path being
   independently wrong, which corroborates our finding that `skyBrightness` was not our cast source.
4. **Their controller mapping** as a reference for DualSense layout — relevant if the ViGEm path is
   ever revisited or if a native-pad route is chosen.
5. **`tools/installer/MODDING.md`** — their texture-mod format, for comparison with our USD override
   approach.

---

## 5. Decision this creates (unresolved)

Two different end products, not two implementations of one:

| | Recompiled + HD+ | Remix + our materials |
|---|---|---|
| lighting | **correct** (authored) | path-traced but blue-tinted |
| textures | HD originals + texture mods | 29 PBRified materials, 18/61 levels |
| controller | native DualSense | ViGEm virtual pad |
| path tracing | none | yes |
| ARM64 | emulated x64 | native bridge |
| effort to finish | play it | relighting, 1–2 h per room |

Recorded here so the trade is visible rather than rediscovered: **correct lighting on native code, or
path tracing on a game whose lighting model fights it.** Tonight's evidence suggests both is a
relighting project measured in weeks, not a config change.

---

## 6. Sources

| what | where |
|---|---|
| repo | <https://github.com/MysteriousAeon/rayman3-hd-recompiled> |
| README (raw) | <https://raw.githubusercontent.com/MysteriousAeon/rayman3-hd-recompiled/main/README.md> |
| build docs | `docs/BUILDING.md` in repo |
| provenance | `docs/PROVENANCE.md` in repo |
| mod authoring | `tools/installer/MODDING.md` in repo |
| licence | `LICENSE` (MIT, with scope exclusions), `tools/installer/THIRD_PARTY_NOTICES.txt` |

Fetched and indexed 2026-11-03. Details above are quoted or paraphrased from the README and repo pages
at that time; the project is pre-1.0 in spirit (playtested at v1.0.1, limited hardware coverage) and its
feature list may move.
