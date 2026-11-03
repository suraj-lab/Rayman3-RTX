# Level data format and light extraction (offline, read-only)

Status: **recon only, 2026-11-03.** No game launch, no game file modified.
Source for format hints: Raymap (`byvar/raymap`) loader code, read as documentation.
Raymap has **no licence file** (GitHub reports `license: null`) => all rights reserved.
Do not copy its code; implement our own reader. BinarySerializer.OpenSpace (MIT) covers
only `.cnt`/`.gf`/saves, no level parsing (its 71-file tree was checked in full).

## What is on disk

`C:\Program Files\GOG Galaxy\Games\Rayman 3\Gamedatabin\` (`GAMEDATA\` only holds saves):

| Files | Count | Size | Note |
|---|---|---|---|
| `<level>\<level>.lvl` + `.ptr` | 61 levels + `fix.lvl/.ptr` | ~305 MB | per-level memory image + relocation table |
| `transit.lvl/.ptr` | per level | small | |
| `tex32_1/2.cnt`, `tex16_1/2.cnt`, `vignette.cnt` | 5 | ~366 MB | textures (`.gf`), `.cnt` container |
| `.hxc`, `.bnh`, `.hos`, `.hst` | | | not examined (likely sound/other) |

## Format (from Raymap's LVL/relocation code)

- Rayman 3 PC levels are **raw memory images**, not a structured archive. `.lvl` is
  little-endian, data starts at base offset 4; `.ptr` is a relocation table:
  `u32 count`, then `count` x (`i32 fileID`, `u32 offset`) pairs, then (Rayman 3) a tail
  of 16-byte "fill-in" entries. Each pair says "the u32 at `offset` is a pointer into
  file `fileID`" (fix.lvl is a separate file the level points into).
- Observed here: `wood_10.ptr` count 50340, size 409364, i.e. 6640 tail bytes = 415 x 16.
- Consequence: lights are not in a flat list. They are structs reachable by walking the
  pointer graph (sector -> light linked list); a faithful reader must resolve `.ptr`
  relocations first.

## Rayman 3 light struct (`LightInfo`, size 0x110, from Raymap comment + reader)

| Offset | Field |
|---|---|
| 0x00 | u8 turnedOn, u8 castShadows, u8 giroPhare, u8 pulse |
| 0x08 | u16 type |
| 0x0C | f32 far, f32 near |
| 0x10.. | f32 fog alphas, giroStep, pulseStep, pulseMaxRange, giroAngle |
| 0x34 | 4x4 row-major matrix (rows at 0x34/0x44/0x54/0x64; row 3 = position, w=1.0 at 0x70). Raymap's comment (0x34) is right; 0x30 holds a small int (2 here). **Verified on the tower_20 hit.** |
| 0x9C | f32[4] colour (RGBA; can be negative for shadow-casting) |
| 0xFC | second colour |

## Check against our captured light (heuristic, not a parse)

The one D3D light captured at runtime was `(0.20, 0.21, 0.80)`. A byte scan of every `.lvl`
for float triples near that value gives 33 hits across many levels (blue floats are common:
fog, vertex colours), so the colour alone proves nothing. Filtering on a plausible light
header at `colour - 0x9C` (on/shadow in {0,1}, type < 8, 0 <= near <= far) leaves **one**:

| File | Struct offset | Decoded |
|---|---|---|
| `tower_20\tower_20.lvl` | 0x233a90 | on=1, type=2, far=9.03, near=5.02, colour (0.204, 0.209, 0.796, 1.0) |

Alpha exactly 1.0 and the near-exact colour make this a credible light struct, but it is
one candidate from a heuristic. Not yet established: which level was running when the
light was captured, and that this struct is the light the game submitted.

## Next

1. Confirm which level the captured light came from (check `tests/run_lights_*`
   logs / operator memory; level is probably `tower_20` if the Fairy Council exterior
   is that map).
2. Write `tools/r3lights.py`: scan every level for the 0x110 light struct with the
   header filter, print position (from matrix) / colour / range per level. Heuristic
   scan first; `.ptr` graph walk only if the scan proves unreliable.
3. Resolve the 0x30 vs 0x34 matrix offset by decoding the candidate above (it should
   hold a valid rotation + a world position near the Fairy Council).
4. `.cnt`/`.gf` texture extraction after that (MIT code allowed).

## Update: which level? (2026-11-03)

The run logs (`tests/run_lights_9X15A6I`) never record the level name, and `SUJ.sav` is encoded.
Level identity is therefore inferred from data:

- `python tools/r3lights.py --match 0.20 0.21 0.80` (colour rounded to 2dp, header filter,
  alpha 1.0) returns exactly one struct across all 61 levels: `tower_20.lvl @0x233a90`.
- Layout cross-check: that struct sits in a run of light structs with a constant **0x118**
  stride (e.g. 0x22bef8, 0x22c010, 0x22c128...), which supports colour-at-+0x9C.
- Treat "captured light = tower_20" as likely, not proven. A direct confirmation needs the
  proxy to log the level (or the operator to say which level was loaded).
- Without `--match`, `r3lights.py` is noisy (~13k hits): the header filter is weak and
  `m0x30` is not a trustworthy position yet (matrix offset 0x30 vs 0x34 unresolved).
- The game also printed `level checksum doesn't match with fix checksum !!!` twice in
  `remix_launch.log`; unexplained, worth a look if level loading matters.

## Confirmed (operator): captured light = tower_20

Operator confirms the Fairy Council exterior should be `tower_20`, matching the unique scan hit.
Decoded `tower_20.lvl @0x233a90` (hexdump-verified):

- header: on=1, shadow=0, type=2, far=9.03, near=5.02
- matrix at +0x34: identity rotation, position (4.79, -6.20, -346.5)
- colour at +0x9C: (0.204, 0.209, 0.796, 1.0); second colour block empty
- +0x110/+0x114: two pointers into neighbouring structs (linked-list links)

Open: the game struct says type 2, but the D3D light we captured was type 3 (directional),
so the engine converts the type or this is a different light with the same colour. Light
positions are in level units (this one is ~346 units out on Z). `tower_20` holds 306
header-plausible light structs (18 type0, 273 type2, 2 type6, 13 type7), many of them
near-duplicates, so the filter still over-reports.

## Hexdump check of six more structs (2026-11-03, offline)

Dumped `tower_20.lvl` at 0x233a90 (ref), 0x231bf0 (type 7), 0x235fc0 (negative colour), 0x382f0 (off-grid
negative), 0x237abc (type 6), 0x22f378 (blue candidate). Findings:

- **Matrix offset resolved, both sources were right.** `+0x30` is a u32 matrix *type tag* (2 for type-2
  lights, 5 for type 7); the 4x4 floats start at `+0x34`. Raymap's reader consumes the tag at 0x30; its
  comment names the float start.
- **Layout holds on all six**: type u16 @+8, far/near @+0xC/+0x10, fog-ish floats @+0x14.., 1.0 flags near
  +0x28/+0x2C, `01 01 00 02`-style flag word @+0xB0, colour @+0x9C with alpha 1.0 in every case.
- **Second colour @+0xFC is not independent**: it is zero for type 2/6 and a *copy of the colour* for
  type 7 (0x231bf0: (1.004,1.004,2.0,1.0) at both +0x9C and +0xFC). Ignore it for type 2.
- **Type 7 carries a real rotation** (0x382f0: non-trivial 3x3; 0x231bf0: diag(1,-1,-1)), type 2 is
  identity-rotation + position: type 7 looks like the directional/oriented kind, type 2 positional. Not
  proven against `SetLight` yet (runtime types seen were D3D 3 and 1 only).
- **Negative colour is authored, not corrupt**: 0x235fc0 sits on the 0x118 grid with
  (-0.418,-0.418,-0.418,1.0); 0x382f0 (-0.449,-0.5,-0.5) is a valid type-7 with a colour copy at +0xFC, so
  it is not corrupt either (the "off-grid" label means a different list, tail pointers 0x6818/0/1/0x2fb43c
  instead of two neighbour pointers). Negative = shadow/darkening light. Drop neither.
- **Type 6 (0x237abc) is not an ordinary light**: colour (1,0,0), far 100, near 40, +0x14 = 50000.0,
  +0xF4 = 150.0. Looks like a fog/region volume with a debug-red colour; exclude from light lists.
- On-grid structs end with two pointers (+0x110/+0x114) to the neighbouring entries, then 1, 0: the
  0x118-stride array is a linked list, so a `.ptr` walk could enumerate real lights exactly.

## `.ptr`-walk reader (2026-11-03): `analysis/tools/r3lights_walk.py`

The lights are an in-file linked list, so they can be enumerated structurally instead of by colour.
Verified on `tower_20`:

- `.ptr` entry offsets + 4 = file position of a relocated u32; pointer value v = file position v + 4.
  (Check: the u32 at `0x233a90 + 0x110` is relocated and holds 0x233ba4 -> 0x233ba8 = next struct.)
- A list element is 0x118 bytes; `+0x110` is a relocated "next" pointer; the list end holds raw 1.
- Walk result for `tower_20`: **307 lights in 10 chains** (lengths 170/62/21/17/13/11/5/4/2/2); 0x233a90
  is in the 170-long chain. Versus the heuristic scan (306): 283 in common, **24 found only by the
  walk** (near = 0 or near == far, which the heuristic's `0 < near < far` rule rejected) and **23
  heuristic-only rows that are not lights** (the type-0 junk and a few others: their `+0x110` is not a
  relocated pointer, often float bits like 0x3f800000). So type-0 rows ARE false positives, and the
  heuristic also missed real lights.
- `--all` gives per-level counts (e.g. tower_20 307, tower_10 591, wood_10 757, knaar_45 814,
  menu_20 5). `--csv` exports one level. `fix.lvl` is not covered by the glob.
- Limit: a list of exactly one light has no links and is missed (marked `shortcut` in the script).

## Light type enum and level census (2026-11-03)

Raymap's `LightInfo.LightType` (read as documentation): 0 Unknown, 1 Parallel, 2 Spherical, 3 Hotspot,
4 Ambient, 5 ParallelOtherType, 6 Fog (also background colour), 7 ParallelInASphere, 8 SphereOtherType.
Consistent with what we decoded: 0x237abc (type 6) is a fog record, not a light; type 7 has a rotation.
Census over the 61 walked levels (`tower20-work/level-light-census.csv`): Spherical 11,489, ParallelInASphere
1,190, Ambient 185, Parallel 173, Fog 97, Hotspot 77. Flag word @+0xB0 (tower_20): mostly `01 01 00 02`
and `01 01 01 02` (byte meanings not yet pinned down; Raymap reads send/objectLighted/painting/alpha flags).

## "level checksum doesn't match with fix checksum !!!" (2026-11-03, offline)

Checked so it does not have to be investigated later:

- The string lives in `Rayman3.exe` at `0x1c0ad0`: the **game** prints it, not Remix.
- It appears at the **end** of a launch log, immediately before Remix logs `Saved layer config to 'user.conf'`:
  draw-count run lines 278-279 of 282, light run lines 291-293 of 296. So it is logged at level unload /
  exit, not at load, and both runs otherwise completed normally.
- All 61 levels carry the same words at `+8/+12/+16` as `fix.lvl`'s `+4/+8/+12` (`0x36c, 0x1c3, 0xb2`):
  a shared build stamp, so nothing is being rejected as foreign data.
- The per-level u32 at `+4` is unique per level and is **not** crc32/adler32 of the `.lvl` bytes (any slice
  tried); the hash is unidentified. `fix.lvl` contains 36 of the 61 level names.
- Verdict: **benign at present, not pursued.** Revisit only if it starts appearing at load time or a level
  fails to load; the next step would be finding the expected value in `fix.lvl`'s level list.
- Side finding from the same logs: Remix writes the game-folder `user.conf` (`Saved layer config to
  'user.conf'`, timestamp matching 2026-11-02 23:52), so `analysis/tools/r3conf.py` edits a file this
  runtime really reads - the earlier doubt about that is resolved.
