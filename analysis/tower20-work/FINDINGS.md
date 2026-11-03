# tower_20 light team: verified findings (2026-11-03)

Team: 3 analysts + 3 skeptical reviewers, all `deepseek/deepseek-flash`, no shell. The lead re-checked
every number the reviewers disputed. Read-only; nothing applied.

## Verified by the lead (python, from `tests/run_lights_9X15A6I/r3probe.log`)

- **The earlier note "only light index 0 is ever enabled" is false.** The run created 145,456 lights:
  idx0 76,208 / idx1 47,140 / idx2 16,339 / idx3 5,769; type 3 (directional) 138,802, type 1 (point) 6,654;
  all `rc=0`; handles 1..145,456 strictly increasing; 107 distinct (type, diffuse) combinations.
- `(0.20, 0.21, 0.80)` is only 29,068 of them (20%). Others: (0.02,0.06,0.04) x47,140,
  (0.06,0.18,0.35) x47,065, (0.25,0.16,0.08) x5,595, (0.64,0,1.0) x2,308, a type-1 point light
  (0.75,0.17,0.65) x6,654.
- 145,456 creates / 9,600 frames ~ **15 per frame**. This is per-object lighting: the game calls
  `SetLight` with each object's own lights, so the proxy's whole-struct `memcmp` correctly sees a change
  and destroys/recreates. It is churn by design, not a failed change-detection bug. Fix idea: key a
  cache on the light contents instead of "slot idx" (or only submit lights once per frame by content hash).
- Most runtime colours do **not** appear in `tower_20`'s decoded structs (header filter: on=1, alpha 1.0):
  only (0.20,0.21,0.80) matches. So 0x233a90 is a plausible source of one light, not proof that the engine
  feeds level light structs straight into `SetLight`. The level -> D3D light path is unproven.
- **Camera position `(26.2, 188.59, 46.11)` is correct**: `c*R + t` residual ~0 (<= 0.21). The mapping
  analyst's `(-20.3, 88.0, 173.9)` used columns instead of rows (residual up to 150) and is wrong, so its
  distance tables and "negation is best at 58.7" are invalid. The reviewer's correction stands.
- The conclusion "level coordinates cannot be mapped to the world with this data" still holds, but for a
  different reason: no anchor pair exists. Camera y=189 lies outside the lights' y range (-105..108).

## Reviewer-corrected catalogue facts (CSV, 306 rows; reviewer recount, not re-run by lead)

- Types: 273 type 2, 18 type 0, 13 type 7, 2 type 6; no type 3 (engine type enum unmapped: struct type 2
  vs runtime D3D type 3 is an open conflict, so `type` must not be used to filter).
- Exact duplicates: two groups (4 rows and 7 rows), 9 redundant rows.
- Rule "negative colour = corrupt" is wrong: `0x235fc0` is on the 0x118 grid; negative colours may be
  authored (shadow casting). Drop only `far <= 0` rows and off-grid rows. Do not drop type 0 by rule.
- Colours: warm (1,0.5,0) x~22-23, cool (0,1,1) x66 scattered across the level (not clustered),
  38 rows of (2,0,0), 8 grey (0.8). Torch-vs-cool attribution is unverified.
- Moonlight candidates beyond 0x233a90 exist (0x22f378, 0x233ef0, 0x233dd8), none verified.

## Red-light test and proxy patch: review verdict

- Proposed patch does **not compile**: `g_lightOn[]` does not exist; do not apply it. Dropping the
  enable guard would also draw lights for disabled slots.
- `g_transformLogs < 40` is shared by WORLD/VIEW/PROJECTION, so a runtime VIEW is a startup snapshot
  only; do not derive a camera for a test light from it. Hook `SetTransform(VIEW)` into a separate
  "last view" variable instead.
- `D3DLIGHT8X` comment says 100 bytes, true size is 104; comment typo only.
- The real anchor for the mapping question is cheap: `hSetLight` already receives world-space
  `Position` (type-1 point lights) and `Direction`. **Log them** (one line per distinct light) in the next
  authorized run and compare with `tower20-lights.csv`: an exact position match proves the level->world
  mapping; no match with any CSV row means the CSV is not what is submitted.
- Decisive red test stays as designed: keep type 3 with radiance (50,0,0) as Variant A, then a sphere light
  with radius ~1000 near the camera as Variant B. Needs operator approval to launch.

## Next (all need approval or a rebuilt proxy)

1. Proxy: log SetLight type/position/direction/range per distinct light; keep a "last VIEW" global.
2. Build + run once with the red-light switch (`R3_REDLIGHT`), compare against control.
3. Offline meanwhile: hexdump 0x231bf0 (type 7) and 0x235fc0 to widen layout verification; look for the
   0xFC second colour block, which may explain the non-matching runtime colours.

## Update: hexdump results (see level-data-format.md)

Layout confirmed on six structs; 0x30 is a matrix type tag; 0xFC colour is a copy only for type 7;
negative colours are authored; type 6 is a fog-like volume. The 0x118-stride array is a linked list
(+0x110/+0x114 neighbour pointers), so exact enumeration is possible via the `.ptr` relocations.
Still unresolved: how level struct types (2, 7) map to the D3D types (3, 1) the game submits. The
rebuilt proxy's SetLight log is the experiment that answers it.

## Update: `.ptr` walk (analysis/tools/r3lights_walk.py)

Structural enumeration replaces the colour heuristic: 307 lights / 10 chains in tower_20, ref light in
the 170-long chain. Heuristic had 24 misses (near==0 or near==far) and 23 false positives (type 0 etc.).
`tower20-work/tower20-walk.csv` is the authoritative list; `tower20-lights.csv` is superseded.
Review caveat: the earlier agent catalogue was built on the heuristic CSV, so its counts (273 type 2 ...)
and false-positive rules are now obsolete. Re-run any analysis against the walk CSV.

## Update: runtime vs level lights, and the type mapping (offline, 2026-11-03)

Source: `tests/run_lights_9X15A6I/r3probe.log` (145,456 SetLight->CreateLight events), grouped per
"lighting setup" (consecutive slots 0,1,2.. = one setup) and per 600-frame bucket.

- **Two phases.** Frames 0 to ~4,800: only single-light setups, always D3D type 3 diffuse (0.20,0.21,0.80)
  (29,068 setups). From ~4,800: 2-4 light setups, mainly slot0 (0.02,0.06,0.04) + slot1 (0.06,0.18,0.35)
  (30,726 pairs), plus (0.25,0.16,0.08) x2 / (0.64,0,1) / (0.75,0.17,0.65) variants; 132 distinct setups.
  The log therefore spans a scene change (e.g. menu/intro to a level, or two levels); the level in
  phase 2 is unidentified.
- **Exact colour match across all 61 levels (colour to 2 dp):** only `(0.20,0.21,0.80)` matches any level
  light, and only in `tower_20` (0x233a90). The other 105 runtime colours match nothing (median level has 0
  matches by chance). Free-scale matching was too loose to be informative (it "explains" 93% for menumap),
  so it is discarded.
- **What this supports:** phase 1 = tower_20, and the game passes that level light's colour through
  unchanged. Level type 2 (Spherical) arrives as D3D type 3 (directional) in this one case; D3D type 1
  appears only in phase 2 (6,654 setups, colour (0.75,0.17,0.65)). One sample is not a mapping: the rest of
  the table needs the SetLight position/direction log.
- **Phase-2 colours are not raw level colours**, so they come from engine computation (blend/attenuation/
  per-perso lighting) or from lights outside the linked lists we walk. Unresolved.

### Consequence for "set lights for all levels"

The proxy already forwards what the game submits, and that produced no visible change (the red-light test is
still the open question). Injecting level-file lights adds information only for lights the game does not
itself pass to SetLight (positional lights, fog/background colour of type 6, ambient type 4). Do not build a
per-level injector before the red-light test shows API lights reach the scene. If they do, the per-level
key light has to be chosen by evidence per level (SetLight log), not by a type rule.
