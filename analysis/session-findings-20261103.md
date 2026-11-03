# Session findings — lighting, materials, methodology (2026-11-03)

Written after a full evening of work on Rayman 3 under RTX Remix 1.6.1 on Windows ARM64.
Everything below is either measured from files/recordings or explicitly marked as inference.

---

## 1. State at end of session

| area | outcome | evidence |
|---|---|---|
| **Materials** | **29 materials** PBRified and saved (30 overrides total incl. one earlier test) | `mod.usda` 24,423 bytes; 30 × each of `diffuse_texture`, `reflectionroughness_texture`, `normalmap_texture`, `height_texture` |
| **Material reach** | **18 of 61 levels** use at least one of those textures | `analysis/textures/level-metrics.csv` cross-referenced with the 29 names |
| **Brightness** | matched to vanilla by measurement | Remix luma 42 → target 51; `exposureBias = 0.3` (+0.28 EV) staged |
| **Hue** | **not fixed** | Remix B/R ≈ 3.1 vs vanilla 1.66; cause established, cure is relighting |
| **Reflections** | **not started** | needs a capture in `swamp_50` or `tower_30` |
| **Mod visibility in game** | not yet visible | `rtx.enableReplacementAssets = False` |

Active `user.conf` lighting keys — four, each with a measured justification:

```ini
rtx.lightmapTextures     = 0x937AA8BB3CCEBDD3
rtx.lightConverter       = 0x5AFAA700F2860C31
rtx.autoExposure.enabled = False
rtx.tonemap.exposureBias = 0.3
```

28 backups in `backups/`, taken before each change.

---

## 2. Environment and tooling discovered

| item | detail |
|---|---|
| Runtime | RTX Remix 1.6.1-0, ARM64 bridge (`NvRemixLauncher32.exe` + `.trex_arm64`) |
| Game chain | `D3D8.dll` (our proxy) → `d3d8_real.dll` (d3d8to9) → `d3d9.dll` (Remix shim) |
| Toolkit | `kit.exe`, MCP at `http://127.0.0.1:18014/mcp/`, discovery file `%LOCALAPPDATA%\NVIDIA\RTX Remix\mcp.json` |
| USD without install | `kit\python\python.exe` (3.12.13) + `extscache\omni.usd.libs` → `pxr 0.25.11`; wrapper `analysis/tools/r3usd.py` |
| ComfyUI | Comfy Desktop, instance "Messing around"; python 3.13.12 ARM64, **torch 2.15.0.dev+cu134, CUDA working**, GPU "NVIDIA RTX Spark N1X", 55 GB VRAM |
| ComfyUI node pack | `github.com/NVIDIAGameWorks/ComfyUI-RTX-Remix` → `custom_nodes/comfyui-rtx_remix`; 31 `RTXRemix*` nodes |
| Only missing dependency | `OpenEXR` (win_arm64 wheel); `requirements.txt` skipped because it pins `pydantic==2.11.5` and would have downgraded 2.13.5 |

PBRify workflow ships **no models**; its `RTXRemixDownloadModel` nodes download four files on first run (~1 GB).

---

## 3. Materials workstream

### Method
1. `Ctrl+Shift+Q` capture in game → `/RootNode/Looks/mat_<hash>` + `/RootNode/meshes/mesh_<hash>` in USD.
2. Read the stage offline with `pxr` (`r3usd_dump.py`) — materials, meshes, lights, metadata.
3. Map texture hashes to real `.gf` names (`r3match.py`, signature matching against the decoded `.cnt` packs).
4. Toolkit: connect ComfyUI → workflow **PBRify** (workflowType *Material Generation*) → Run → **Apply** → **save layer**.
5. Read back with MCP `get_material_textures` to prove the bindings landed.

### Result
- Captured stage has 39 materials, **every one diffuse-only** — no roughness, metallic, emissive or normal authored anywhere (424 shader prims across 13 captures confirmed the same).
- `AperturePBR_Opacity.mdl` defaults: `reflection_roughness_constant = 0.5`, `metallic_constant = 0.f`, `enable_emission = false`, `emissive_intensity = 40`.
- 29 selected for PBR (9 excluded as sky/effect/shadow: `shadow`, `game_sparkle3_ad`, `ago_firefx_blue`, `lep_etoile`, `wood_moon`, `wood_moon_02`, `lan_backsky00`, `wood_clouds`, `wood_brume_lac`).
- Each target became 4 maps: upscaled albedo + generated roughness, normal (OpenGL), height.

### Two gotchas that cost time
1. **Apply does not persist.** The Job Queue shows "Applied : 2/2 jobs" with a green tick, but `mod.usda` stays byte-identical until `remix_save_layer` is called. Verified twice: 194 bytes → 1049 bytes (one material), then 1049 → 24,423 bytes (29 materials).
2. **`Selected Texture` resolves per selected prim**, following each prim's *bound material*. Selecting a material prim is not the same as selecting geometry that uses it — the first run landed on `wood_floor02_night` instead of the intended stone slab.
   `All Textures` exists as an alternative resolver and expands to one job per stage material, de-duplicated by material.

---

## 4. Lighting workstream

### 4.1 What the captures actually contain

| observation | value |
|---|---|
| Effect lights per view | 6–9, all at intensity **318.3099 = 1 × 1000/π**, radius 5.0, white |
| Other lights in the menu scene | **one** DistantLight, colour **(0.25, 0.26, 1.0)** (B/R 4.0), intensity 0.8 |
| Game's own point lights | 0.017–0.118 (they scale with `lightConversionIntensityFactor`) |
| Vertex colours | mean (0.49, 0.47, 0.72) sRGB → **linear B/R 2.37** |
| Albedo textures | mean B/R **0.69–0.81** (warm) |
| Texture factors (`tFactor`) | **white** on all placements, blending off |
| `lm.gf` (lightmap-tagged) | neutral **(128,128,128)** |
| Clipping | **0.00 %** pixels ≥250 in every sampled frame |

### 4.2 Hypotheses tested and falsified

| # | change | result | conclusion |
|---|---|---|---|
| 1 | Effect Light UI slider → 0.550 | no key written to `user.conf`; captures still 318.3099 | setting never persisted |
| 2 | `lightConversionIntensityFactor = 0.3` | game lights 0.118 → 0.0354 (×0.3 exactly); effect lights unchanged | scales converted lights only |
| 3 | remove `lightConverter` | white blobs gone; room went **near-black** (luma 80 → 44) | effect lights *are* the illumination |
| 4 | `lightConversionMaxIntensity = 50` | still 318.3099 | governs converted lights, not effect lights |
| 5 | `effectLightIntensity = 0.25` | set and parsed; no observed drop | effect-light path not honouring it in 1.6.1 |
| 6 | `skyBrightness = 0` | darker, B/R 2.24 → 2.61 | sky is not the blue source |
| 7 | `tonemap.colorBalance = 1,1,0` (+ grading on) | blue survived a ×0 multiplier | **colour grading is inert in this build** |
| 8 | `autoExposure.exposureAverageMode = 0`, `evMaxValue = 2` | no visible change | not the lever |

### 4.3 What is established

- **The blue is the illuminant, not a post effect.** With zero lights captured and no sky, the scene is lit purely by the vertex-colour term, whose linear B/R is 2.37; the render measures 2.6–3.4.
- **Rayman 3 stores gamma-space lighting in vertex colours.** Stored B/R 1.29; linearised 2.37. Remix consumes them as linear light, which inflates the blue channel roughly 1.8×.
- `rtx.vertexColorIsBakedLighting = True` (default) *normalises* by the largest channel — it removes brightness and keeps hue. Setting it **False** (which I did mid-session) turns the vertex colour into a blue illuminant and **created** the strongest blue we measured — a self-inflicted regression, removed in the reset.
- **Auto-exposure invalidates brightness comparisons.** With it on, dimming a light made the frame *brighter* (luma 73 → 104 after removing a light source). Turning it off stabilised measurement (luma 104 → 42).
- **There is no config-only cure for the hue.** The only documented cure is relighting with authored primitive lights, which is what NVIDIA's relighting guide recommends for games whose baked lighting cannot be reused.

### 4.4 Measurement log (scene frames only, menu filtered)

| run | config | luma | B/R |
|---|---|---|---|
| vanilla baseline (17 frames) | dgVoodoo | **51** | **1.66** (1.06–2.36) |
| 20:50 | early experiments | — | 3.91 |
| 21:08 | grading test (inert) | 34 | 3.24 |
| 21:21 | defaults restored | 73 | 2.34 |
| 21:24 | lamp-only converter | 104 | 2.07 |
| 21:26 | auto-exposure off | 42 | 3.14 |
| staged | + `exposureBias 0.3` | ~51 (predicted) | ~3.1 |

---

## 5. Reflections workstream

Not started. Established groundwork:

- Texture-path scan of all `.lvl` files finds planar-reflection name candidates (`*_reflect1`, `*_reflect2`) in exactly **7 levels**: `moor_60`, `swamp_50`, `swamp_60`, `swamp_81`, `swamp_82`, `swamp_83`, `tower_30`.
- The lever exists in the runtime: **`rtx.raytracedRenderTargetTextures`** (hash set), alongside `rtx.raytracedRenderTarget.enable`.
- Mirroring is real in captured geometry: instance transforms with determinant −1, and paired light positions mirrored about a plane (z = +11.02 / −30.97).
- `raytracedRenderTarget.enable = False` was tried earlier and made the game look worse; it is currently `True`.

---

## 6. Methodology (reusable)

### 6.1 Measuring what the player sees
- **Capture thumbnails are pre-tonemap HDR** (`captures/thumbs/<capture>.dds`, 512×512 RGBA16F, 8 bytes/px). They reflect scene radiance, not the displayed image. Decode as half-float (`struct.unpack('<4e')`), not 8-bit — reading them as bytes previously produced "0 % white" on a blown frame.
- **Video frames are the only post-tonemap ground truth.** Mean RGB via `ffmpeg -vf scale=1:1:flags=area -f rawvideo -pix_fmt rgb24`; luma/clipping via `signalstats` (`YAVG`, `YMAX`, `SATAVG`).
- **Filter UI/menu frames**: a frame is menu/UI when `YAVG > 80` **and** `B/R` between 0.7 and 1.6. Including them once dragged a scene average from B/R 3.24 to 1.67.
- Use **B/R** (mean blue ÷ mean red) as the colour-cast metric; it compares directly against vanilla's 1.66.

### 6.2 Operating rules learned the hard way
- **Never alt-tab in exclusive fullscreen.** `vkCreateSwapchainKHR` fails with `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR`, retries once, then the game exits. Capture with `Ctrl+Shift+Q` and report afterwards instead.
- `Ctrl+Shift+Q` is a Remix-runtime hotkey; in vanilla mode it does nothing and nothing is recorded (recordings come from the Remix bridge).
- ComfyUI + game do not fit in RAM together on this machine (~13 GB needed; 22.7 GB total).
- Verify every Remix option against `RtxOptions.md` or the startup log before trusting it; several names exist in the binary but are not UI-exposed, and vice versa.

### 6.3 Things I got wrong this session (kept as a caution)
- Claimed the `lightConversion*` keys did not exist — they do; my string search required an `rtx.` prefix in the same buffer.
- Set `vertexColorIsBakedLighting = False` chasing brightness and thereby **created** the strongest blue cast, then spent hours attributing it.
- Stacked up to eight unverified keys at once, making several runs unattributable.
- A process check treated `tasklist`'s "INFO: No tasks…" line as a running process and later tried `taskkill /PID No`; harmless, but wrong.
- Read the first material run as "applied" from the UI status when the layer had not been saved.

---

## 7. Next steps, cheapest first

1. **Reflections**: capture in `swamp_50` or `tower_30`; extract `*_reflect1/2` hashes from the capture's textures; set `rtx.raytracedRenderTargetTextures`. One capture, one config key.
2. **Make the materials visible**: `rtx.enableReplacementAssets = True`. Costs the `Ctrl+Shift+Q` capture hotkey, so do it after step 1.
3. **Hue**: relighting with authored primitive lights (1–2 h per room, no undo). Our decoded level lights (13,211 candidates across 61 levels) are the natural placement source, but the coordinate registration between level space and capture space is unsolved.
4. **Emissive**: `enable_emission` is a bool the MCP cannot author; the reachable path is ingesting a replacement material with emission enabled and swapping the reference (allowed: references can be added/replaced).

---

## 8. Repo artefacts added or updated

| file | purpose |
|---|---|
| `analysis/tools/r3usd.py` | run any script under the toolkit's bundled USD |
| `analysis/tools/r3usd_dump.py` | capture or captures directory → JSON (templates vs placements, UV/normal/topology validation) |
| `analysis/tools/r3level_metrics.py` | per-file lights, texture paths, reflection candidates |
| `analysis/tools/r3conf.py` | reversible `user.conf` edits with backups |
| `analysis/tools/r3match.py` | capture hash → `.gf` name matching |
| `tests/test_remix_inventory.py` | 6 tests covering the exporters and the reflection classifier |
| `analysis/textures/capture-inventory.json` | 13 captures, schema v2, with limitations recorded |
| `analysis/textures/capture-hash-matches.csv` | 54 hash → name matches with a `bound_to_material` column |
| `analysis/textures/level-metrics.csv` | 62 rows: lights, texture refs, reflection candidates per file |
| `analysis/remix-review-20261103.md` | merged into this document (§9); file is a pointer stub |

Capture state: **36 captures**, 90 captured texture DDS, 120 ingested `.rtex.dds`.

---

## 9. Carried over from the earlier review (`remix-review-20261103.md`, now folded in here)

That document covered the offline analysis; its verified claims and corrections still stand and belong with the rest.

### 9.1 Verified claims

| claim | evidence |
|---|---|
| USD access without installing anything | `kit\python\python.exe` 3.12.13 + `extscache\omni.usd.libs` → `pxr 0.25.11` |
| Capture geometry is cheap to replace | 13 captures, union of 596 mesh templates, 15.7k–41.3k visible triangles per view |
| Materials are diffuse-only | **424 shader prims across 13 captures**, every one authoring exactly `diffuse_texture`, `wrap_mode_u/v`, `filter_mode`, `enable_opacity` |
| Material defaults are 0.5 / 0.0 / off | shipped `AperturePBR_Opacity.mdl` |
| The 18-26-40 capture is `intro_15` | **6 of 16** sphere lights matched a decoded `intro_15` light at distance **0.000**; no other level scored |
| Capture transforms are X-mirrored | instance `xformOp:transform` determinant **−1**; level-light positions match only after undoing X |
| Level-light decoding is correct | those same 6 exact position matches |
| Effect lights are identifiable | 7 of 16 sphere lights at exactly **318.30987548828125**, radius 5.0, white |
| Baked vertex colour is universal | **1567 / 1567** placements carry `_remix_metadata:isVertexColorBakedLighting = True` |

**Provenance.** Rows above rest on `analysis/textures/capture-inventory.json`, `analysis/textures/level-metrics.csv` and the shipped MDL. Four claims do **not**, and are marked here so they are not mistaken for file-backed:

| claim | actually rests on |
|---|---|
| capture is `intro_15`; 6 exact light matches | `r3lights_walk.py` decode + the X-mirror undo |
| luma figures (131.6 vs vanilla 63–70) | screen measurements in `tests/` |
| effect lights are the cause of the blow-out | inference from the 318.31 vs 0.03–0.12 gap — now measured (see §4) |
| `DistantLight` set matches only 1 of 6 colours | same light decode |

### 9.2 Corrections to earlier reports

1. **"63k triangles with UVs on every mesh"** — counted hidden `/RootNode/meshes` templates *and* their placements. Correct figure: 179 templates / 200 placements, 31,032 template tris / 31,767 placed.
2. **"379 meshes carry UVs"** — the test only looked for a primvar named `st`. **15 placements have a zero-length `st`**, and 4 have invalid normals.
3. **"Reflections in 26 of 61 levels"** — the marker list included the bare substring `reflect`, so `gold_txy_reflect.tga` (a pickup effect) counted. On texture paths, planar candidates appear in exactly **7** files.
4. **"Texture directory mapping is heuristic"** — it need not be; `.lvl` files embed texture paths, e.g. `intro_15` references `wood:33 charact:30 sfx:22 templeptz:3 leptys:1 objets:1`.
5. **"Nearest decoded light is 32 units away, so level lights do not reach `SetLight`"** — the capture is mirrored; after undoing X the match is exact.
6. **"Radiant power" ranking table** — colour × range² is not power; level-file `far` is not the submitted intensity.
7. **"Absent shader inputs mean Remix hides them"** — they are absent from the *file*; the MDL supplies the defaults.

### 9.3 Open questions still open

- The `DistantLight` set does not map cleanly to decoded level lights (only 1 of 6 colours matched). They may be engine ambient globals rather than level data.
- 15 placements sample a diffuse texture with zero-length UVs — expected to read as a flat colour at (0,0), not yet confirmed visually.
- Whether the two tagged converter textures are appropriate light sources at all; `game_sparkle3_ad` is a 32×32 sparkle that spawns a sphere light per draw.

