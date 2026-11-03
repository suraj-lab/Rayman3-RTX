# RTX Remix methodology — Toolkit, MCP, skill, ComfyUI

Operating notes for driving RTX Remix 1.6.1 on this machine. Written from what was actually executed,
including the parts that are **not implemented yet** — those are marked, because knowing the correct
sequence in advance is cheaper than rediscovering it.

Verified against: toolkit 1.6.1-0, ComfyUI 0.43.2 (ARM64, CUDA working), project `r3-remix-project`.

---

## 1. Connecting to the Toolkit over MCP

The MCP server is part of the Toolkit and only exists while the Toolkit runs.

| step | detail |
|---|---|
| Start | `C:\Program Files\NVIDIA Corporation\RTX Remix\kit\kit.exe` (windowed is fine) |
| Endpoint | `http://127.0.0.1:18014/mcp/` — **keep the trailing slash**; falls back through 18019 |
| Discovery | `%LOCALAPPDATA%\NVIDIA\RTX Remix\mcp.json` → `mcp_endpoint`, `rest_endpoint`, `pid`, version |
| Readiness | wait for `SERVICE_READY service=mcp`; port listening is the practical check |
| Legacy transport | `/exts/lightspeed.trex.mcp.core/transport = sse` serves `:18014/sse` |

**Always verify before trusting the discovery file.** It is removed on clean shutdown but survives a
crash, so it can point at a dead PID and a closed port. Check that `pid` is alive *and* the port
answers; the process is not always called `kit`.

The toolkit's REST API (40 routes on `:8011`) is far smaller than the MCP surface and exposes no
ComfyUI job or queue endpoints — the ComfyUI queue is in-process only.

---

## 2. What the 35 MCP tools can and cannot do

Read tools: project, layers, sublayers, edit target, prim paths, model instances, textures per
material, texture→material-input mapping, available texture/model assets, default asset directories.

Write tools: create/remove/move/lock/mute layer, save layer, set edit target, set selection,
`override_textures`, `ingest_texture`, `ingest_material_asset`, `ingest_model_asset`,
`replace_prim_reference_file_path`, `append_prim_reference_file_path`.

| capability | allowed? |
|---|---|
| Create / delete / rename / move **prims** (including lights) | **no** — this is why relighting is a GUI job |
| Add or replace an **asset reference** on a prim | yes |
| Read a material's bound textures / replace them | yes |
| Author a scalar or bool shader input (`enable_emission`, `emissive_intensity`) | **no** — texture (asset) paths only |
| Undo anything | **no undo tool exists in 1.6.1** |

Selection API detail: `remix_set_selection` takes a **comma-separated string** of prim paths, and the
Toolkit expands geometry prims to include their bound materials — selecting 29 meshes returns 58 prims
(29 meshes + 29 materials), de-duplicated by material downstream.

---

## 3. The `rtx-remix-modding` skill — the rules that matter

Shipped in the install at `skills/rtx-remix-modding/SKILL.md`; the same text is published via
`npx skills add https://github.com/nvidia/skills --skill rtx-remix-modding`.

**Order of operations** (the part the tool list does not tell you):

1. Open or confirm the project
2. **Set the edit target before writing** — "a write lands in whichever layer is current, and the
   wrong layer is the failure that looks like success"
3. Find prims (`selection=true` acts on the viewport selection)
4. Write — a file must be **ingested** before it can be referenced
5. **`save_layer`**, then read back what changed

**Rules with teeth:**

- **Nothing saves itself.** A successful edit lives in memory; the game keeps rendering the old asset
  until the layer is on disk. Confirmed twice: `mod.usda` stayed byte-identical through an Apply that
  the UI reported as "Applied : 2/2 jobs".
- **No undo.** NVIDIA's instruction is not to simulate one — not by stripping edits back out, and not
  by authoring `references = None` (a blocked reference composes to nothing and silently deletes the
  asset from the game).
- **Per captured asset, not per instance.** One override changes every sibling instance; the skill
  requires disclosing this whenever the target has siblings.
- **Do not hand-edit `.usda` or use raw `pxr` scripts for mutations.** That bypasses the command layer,
  so nothing validates or can revert it, and the result usually lands in a layer the runtime ignores.
  Reading with `pxr` is fine and is used throughout this repo.
- **A group of targets needs confirmation.** Ten or fewer: list them. More: state how many and what
  they become, and wait. One named target needs no question.
- **Relay tool refusals verbatim** — they name the blocker and the remedy.

---

## 4. ComfyUI + RTX Remix node pack

### 4.1 What was installed

| item | where |
|---|---|
| ComfyUI Desktop | instance "Messing around"; python 3.13.12 ARM64; torch 2.15.0.dev+cu134 with CUDA (RTX Spark N1X, 55 GB VRAM) |
| Node pack | `git clone https://github.com/NVIDIAGameWorks/ComfyUI-RTX-Remix` → `<install>\ComfyUI\custom_nodes\comfyui-rtx_remix` |
| Extra dependency | `pip install OpenEXR` (win_arm64 wheel) into the instance `.venv` |

**Do not run the pack's `requirements.txt`.** It pins `pydantic==2.11.5` and would downgrade the
ComfyUI venv's 2.13.5; `torch` is listed unpinned and is already satisfied, so pip leaves it alone.
Only `OpenEXR` was genuinely missing.

Restart the backend after cloning — custom nodes load at startup only.

### 4.2 Verifying the install

```bash
curl http://127.0.0.1:8188/rtx-remix/v1/workflows     # expect success + workflow list
curl http://127.0.0.1:8188/object_info | grep -c RTXRemix   # expect 31
```

Workflows discovered from `custom_nodes/comfyui-rtx_remix/api_workflows/*.json`:

| displayName | name | type | size |
|---|---|---|---|
| **PBRify** | `integration_pbrify` | Material Generation | 9.9 KB |
| PBR Fusion 4 | `PBRFusion4_v1.0.2` | Material Generation | 21.9 KB |

Also present for hand-running in the ComfyUI UI: `restapi_pbrify`, `restapi_pbrify_lowVRAM`.

### 4.3 PBRify specifics

- Outputs are tagged in the workflow: `albedo`, `roughness`, `normal_ogl`, `height` — all `.png`, linear.
  In the Toolkit, these become the `Texture Type` of each workflow output item; they must mirror the
  names, and `normal_ogl` specifically is OpenGL convention.
- `Apply Behavior` choices for texture outputs are **Replace** and **None** only (an `APPEND` value
  exists but is used for mesh/reference outputs). Replace *authors* the shader input, so a channel that
  does not exist yet is created — no need for a separate "add" step.
- The workflow downloads its own models on first run via `RTXRemixDownloadModel` nodes (~1 GB):
  `4x-PBRify_UpscalerDAT2_V1`, `1x-PBRify_Height`, `1x-PBRify_NormalV3`, `1x-PBRify_RoughnessV2`.
- Results are ingested into `<project>\assets\ingested\comfyui\<job-uuid>\textures\<timestamp>\` as
  `.rtex.dds` + `.meta`. Ingest conversion runs `ConvertDDS` / `ConvertNormal` in the toolkit log.

### 4.4 The sequence that worked

1. Toolkit → **AI Tools** → ComfyUI Setup → server `127.0.0.1:8188` → expect *"ComfyUI is connected and ready."*
2. Workflow Type `All`, Workflow **PBRify**.
3. Workflow Inputs → `texture` = **Selected Texture** (resolves per selected prim, following each
   prim's *bound material* — selecting a material prim is not equivalent to selecting its users).
4. Per output item: Texture Type matches its name; Apply Behavior = **Replace**.
5. **Run Workflow** → watch the footer count (`29 graphs` for 29 materials).
6. **Apply** in the Job Queue (irreversible).
7. **`remix_save_layer`** — without this nothing reaches disk. Then read back with
   `remix_get_material_textures`.

### 4.5 Failure modes seen

| symptom | cause |
|---|---|
| Apply says "Applied : 2/2 jobs" but `mod.usda` unchanged | layer never saved |
| Jobs land on a different asset than intended | `Selected Texture` resolved from a different selected prim |
| Node pack absent from workflow list | backend not restarted after cloning, or import error in the ComfyUI log |
| `Preset: No presets available` | normal — PBRify ships none |

---

## 5. Recipes

### 5.1 Read a capture offline

```bash
python analysis/tools/r3usd.py analysis/tools/r3usd_dump.py <capture.usd> out.json
```
Separates hidden `/RootNode/meshes` templates from `/RootNode/instances` placements, validates
topology/UVs/normals, records `remix_category:*` metadata and light parameters.

### 5.2 Measure what the player actually sees

Capture thumbnails (`captures/thumbs/<capture>.dds`) are **pre-tonemap HDR** (512×512 RGBA16F,
`struct.unpack('<4e')`). They are useful for radiance, useless for colour or exposure.

Video frames are the only post-tonemap truth:

```bash
ffmpeg -ss T -i clip.mp4 -frames:v 1 -vf "scale=1:1:flags=area" -f rawvideo -pix_fmt rgb24 -   # mean RGB
ffmpeg -ss T -i clip.mp4 -frames:v 1 -vf "signalstats,metadata=print" -f null -                # YAVG/YMAX/SATAVG
```

Metrics: **B/R** = mean blue ÷ mean red (colour cast; vanilla ≈ 1.66), mean luma, near-white share.
**Filter UI frames** (`YAVG > 80` and `B/R` 0.7–1.6) or they drag a scene average from 3.24 to 1.67.

### 5.3 Reversible runtime config edits

`analysis/tools/r3conf.py status|apply|restore --set "key=value"` — whitelisted keys only, backups
before every write. Remix rewrites `user.conf` on exit, so in-game changes persist.

---

## 6. Not implemented yet — the documented path for each

### 6.1 Emissive materials
`enable_emission` is a **bool**; no MCP tool writes scalars. Reachable route: ingest a replacement
material with emission enabled, then `remix_replace_prim_reference_file_path` to swap the material.
Allowed (references may be replaced), untested. Global alternative `rtx.legacyMaterial.enableEmissive`
was tried earlier and washes every surface.

### 6.2 Relighting with authored lights
Lights are prims, and the MCP cannot create prims, so this is a **Toolkit GUI** job:
select a mesh → **Add Light** in the Property Panel → choose type, set radiance (~1.0 by NVIDIA's
guidance; captured lights legitimately reach hundreds) → `Copy Light` / `Paste Light` to repeat.
Captured lights cannot be copied. Budget 1–2 h per room, no undo.
Our decoded level lights (13,211 candidates) would be ideal placement data, but level space ↔ capture
space registration is unsolved (instances are X-mirrored; origins are not geometry positions).

### 6.3 Reflections
Capture in `swamp_50` or `tower_30` (the 7 levels with `*_reflect1/2` texture paths), extract those
hashes from the capture's textures, then set `rtx.raytracedRenderTargetTextures`. The global
`rtx.raytracedRenderTarget.enable = False` is not a substitute — it made the game look worse.

### 6.4 Making the mod visible in game
`rtx.enableReplacementAssets = True`. This also disables the `Ctrl+Shift+Q` capture hotkey, so
sequence it after any capture work.

---

## 7. Operating hazards specific to this machine

- **Never alt-tab in exclusive fullscreen.** Swapchain recreation fails
  (`VK_ERROR_NATIVE_WINDOW_IN_USE_KHR`), retries once, then the game exits. Use `Ctrl+Shift+Q` and
  report afterwards.
- `Ctrl+Shift+Q` is registered by the Remix runtime — it does nothing in vanilla mode, and no
  recording is produced either (recordings come from the Remix bridge).
- ComfyUI + game together exceed available RAM (~13 GB needed of 22.7 GB total). Run one at a time.
- A hard kill in exclusive fullscreen can leave an unresponsive window; recover with
  `ShowWindow(SW_HIDE)` + `SetWindowPos` offscreen — never sign out, and never `rm -rf`-style cleanup.
