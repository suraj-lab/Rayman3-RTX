# Plan: use Rayman Control Panel / RayCarrot libraries for offline game data

Status: **proposed, not started.** Added 2026-11-02 at the operator's request.

## Why this is worth doing

Every remaining blocker is currently being attacked by guessing from a single D3D
call. The game's own data files contain the ground truth:

| Blocker | What the game data has | What we do now instead |
|---|---|---|
| Lighting ("night but lit" reads black) | The level's real light list: type, position, colour, range, attenuation | We captured **one** fixed-function directional light `(0.20 0.21 0.80)` and it made no visible difference |
| Materials / PBR (surfaces not reflective) | Texture sets per surface, including the maps needed for albedo/normal/roughness | Nothing; the runtime uses default materials |
| "Skinning-like" artifact on animated parts | Object/part hierarchy and per-part transforms | Unresolved; suspected transform/hash reuse |

## Relevant upstream projects (verified names, 2026-11-02)

| Repo | Language | Note |
|---|---|---|
| [`RayCarrot/RaymanControlPanel`](https://github.com/RayCarrot/RaymanControlPanel) | C# | Active. Game manager: mod installs, texture replacement, settings, file viewers |
| [`RayCarrot/RayCarrot.Rayman`](https://github.com/RayCarrot/RayCarrot.Rayman) | C# | General Rayman helper library; **archived** but readable. Most likely home of format readers |
| [`RayCarrot/BinaryDataExplorer`](https://github.com/RayCarrot/BinaryDataExplorer) | C# | Inspector for the serialized formats (useful to prototype a parser) |
| `RayCarrot/Rayman3Readvanced` | C# | GBA port — not relevant to the PC D3D8 build |

A local copy of `RaymanControlPanel.exe` already exists at `dl/` in this workspace
(downloaded earlier, never run).

## What to verify first (cheap, offline)

1. Does `RayCarrot.Rayman` (or RCP) actually parse **Rayman 3 PC** level/GAMEDATA and
   texture formats, or only other titles? Read the repo's supported-games table rather
   than assuming.
2. What licence applies to each library, and therefore whether we can use the code
   directly, or must only use it as documentation and write our own parser.
3. Whether the RCP texture-replacement pipeline expects the same container layout we
   already have installed (`GAMEDATA/`, `.trex/`), so extracted textures can be matched
   back to what the runtime hashes.

## Intended use, in order

1. **Extract the level's light list** for the Fairy Council exterior and submit those
   lights through `remixapi_CreateLight`. This replaces guesswork with the values the
   artists actually placed, and directly targets the "too dark" result.
2. **Extract texture sets** to build proper Remix material definitions (albedo, normal,
   roughness/metallic) instead of default materials — this is what "enable PBR" really
   requires; PBR without maps only darkens surfaces.
3. **Read the object/part hierarchy** to explain the animation artifact: Rayman 3 uses
   rigid parts with per-part matrices (no vertex skinning), so a level/object dump can
   tell us whether the runtime is reusing a stale transform rather than the current one.

## Constraints

- Game data stays local. Nothing extracted from `GAMEDATA/` is redistributed in this
  repo; only our own notes, hashes and tooling are committed.
- Third-party code is used only within its licence; if that is unclear, use it as
  format documentation and implement our own reader.
- No game files are modified by this work; it is read-only extraction into the
  project's ignored directories.

## Do this before the next runtime experiment

The decisive light test (absurdly bright red light) still comes **first**, because it
answers whether API-submitted lights reach the compat-rendered scene at all. If they
do not, extracting a perfect light list would change nothing, and the plan must move to
submitting geometry/camera ourselves.
