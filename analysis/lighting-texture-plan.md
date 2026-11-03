# Lighting: texture-side plan (offline, 2026-11-03)

Everything here is read-only analysis of shipped data plus the documented behaviour of the Remix runtime
that this project actually runs (strings + descriptions read out of `.trex_arm64\d3d9.dll`).

## The runtime already models baked lighting

| Option | Default | Description (from the shipped runtime) |
|---|---|---|
| `rtx.vertexColorStrength` | 0.6 (0..1) | how strongly vertex colours influence materials; 0 = fully ignored |
| `rtx.ignoreAllVertexColorBakedLighting` | False | ignore all baked lighting bound to vertex colours |
| `rtx.lightmapTextures` | hash set | textures used for lightmapping; excluded from albedo selection |
| `rtx.ignoreBakedLightingTextures` | hash set | ignore texture-factor **and** vertex-colour baked lighting for those textures |
| `rtx.lightConverter` | hash set | **textures whose draws spawn a Remix effect light**: a sphere light at the draw's centroid, with radius/intensity/colour and plasma-ball animation controlled in Runtime UI > Lighting > Effect Light |

Consequence: Rayman 3's baked illumination is per-vertex (`XYZ|NORMAL|DIFFUSE|TEX1..3` dominates the world draws),
and Remix has first-class handling for exactly that. `rtx.lightConverter` is the documented way to make
individual objects emit real light, which is what "the mushrooms and orbs give off light" needs.

## Texture inventory (parsed with `analysis/tools/r3tex.py`, MIT-documented .cnt/.gf formats)

| Pack | Dirs | Files | Lightmaps |
|---|---|---|---|
| tex16_1 / tex32_1 | 21 | 984 each | 1 (`lightmap/lm.gf`, 256x256, 9 mips) |
| tex16_2 / tex32_2 | 21 | 1045 each | 1 (same) |
| vignette.cnt | 1 (`FRANCAIS`) | 16 | 0 |

- 1413 distinct texture names; **146 match glow/emissive-ish names** (fire, flame, flare, spark, glow, lamp,
  orb, light, crystal, star). Examples: `ago_fireballred`, `ago_firefx_*`, `ago_lightexplosion`, `ago_spark`,
  `con_doorlight`, `con_lightning`, `con_stars`, `fac_flame`, `fac_oillamp`, `fairylum_glow`,
  `flamp_00..12_adnzw`, `game_shootingstar1..3`. These are the prime `rtx.lightConverter` candidates.
- `.gf` headers carry no material/emissive flags (only format, width, height, bpp, mipmaps, RLE code), so the
  emissive signal is not in the texture format — it must come from the draw state or the level materials.
- Only one lightmap per pack (a small atlas), so per-level baked lighting is **not** carried by lightmaps;
  vertex colours remain the likely carrier.

## Texture hashes: must come from the runtime, not computed offline

Remix texture hashes are `XXH64` over the image hash combined with the managed texture's asset-data hash
(`rtx_texture.h`: `getImageHash()` = `resolvedImageView->image()->getHash()` then `XXH64(&assetDataHash, ...)`).
The image hash comes from the runtime's own texture objects after format conversion, so re-implementing it
from `.gf` data offline would be a guess. Three honest ways to obtain real hashes:

1. **Remix capture** (hotkey during play): the capture writes `textures/` named by hash. One capture serves
   every future tagging decision.
2. **Runtime UI**: the dev menu shows a texture hash for the selected surface.
3. Debug logging, if a suitable option exists in this build (not verified).

## Order once hashes exist

1. Tag the confirmed glow textures with `rtx.lightConverter` (this is the per-object light fix).
2. Compare frames with `rtx.vertexColorStrength` at 0.6 / 0.3 / 0 and with `ignoreAllVertexColorBakedLighting`
   on, to locate the white-blowout source (overdriven vertex colour vs albedo).
3. Only then consider `rtx.lightmapTextures` for `lm.gf`.

No REA was needed for any of this. The desktop (SSH, Ghidra 12.1.2, REA checkout, 32 cores, 61 GB) is verified
ready if a question turns into one about code, e.g. how the renderer consumes `DIFFUSE`.

## .gf decoding works: the white-out textures are named in the data (2026-11-03)

`analysis/tools/gfdecode.py` now decodes Rayman 3 `.gf` textures (own PNG writer, no PIL).

**Format, verified:** header is `u32 format, i32 width, i32 height, u8 bytesPerPixel, u8 mipmapLevels,
u8 rleCode`; the RLE stream is **channel-major across all mip levels** (channel 0 of every mip in turn, then
channel 1, ...). Proof: `wood_light.gf` consumes its payload **exactly** (251,611 / 251,611 bytes) and decodes to
a glowing glass lamp; `mnu_gen.gf` (single mip) decodes to the menu panel art. Earlier attempts failed because the
decoder treated the stream as one mip per channel. 16-bit packs (`tex16_*`) are not handled yet (bpp=2).

**Sweep of 2,029 textures (32-bit packs).** Fully white textures — the blow-out candidates:

| texture | size | mean | note |
|---|---|---|---|
| `white`, `typoray3` | 16x16 / 256x256 | 255.0 | pure white utility textures |
| `snow1` | 32x32 | 255.0 | snow overlay |
| `overlight` | 16x16 | 255.0 | literally named an over-light |
| `ago_snowexplosion` | 256x256 | 255.0 | effect overlay |
| `mar_sun`, `bulle2_aaa`, `viseur_fan_ps2`, `raz_glass1`, `sk8_wayb1`, `ball_skyframe3` | 16-256px | 246-255 | additive/overlay sprites |
| `lum_sun` | 256x256 | 235.1 | 100% "bright" pixels — sun glow |
| `ball_water_path`, `mo_mtsnow1` | 128x128 | 235-239 | water/snow overlays |

These are **additive/alpha overlay sprites** in the original renderer. Drawn as opaque geometry by the path tracer
they become pure-white surfaces — which is exactly the artifact seen in `run_play_20261103` (snow platforms and
leaves blown to white). That gives a concrete, checkable cause for the blow-out, independent of the darkness.

Next offline: decode the 16-bit packs too, and cross-check these names against the level texture references once a
capture provides hashes.

## Correction: the white "blow-out" is not widespread (measured 2026-11-03)

I earlier described large surfaces as rendering pure white, from looking at two stills. Measuring the actual
recordings (2 fps, 320x200, luma >= 235 counted as near-white; the H.264 is limited-range so 250 was the wrong
threshold) says otherwise:

| source | mean near-white | peak | mean bright (>=210) | peak |
|---|---|---|---|---|
| Remix play run (`NvRemixBridge 14.20.28`) | 0.2% | 2.9% | 0.6% | 3.3% |
| Vanilla baseline (`Rayman 3 13.59.52`) | 0.2% | 1.5% | 0.4% | 2.6% |

Near-white area is ~0.2% of the frame in both, peaking at 2.9% in Remix vs 1.5% in vanilla. So the white patches
are localized highlights, not blown surfaces, and my "large surfaces render pure white" claim was an over-reading
of stills. What stays measured and real is the earlier result: Remix keeps ~65-70% of vanilla luminance and ~30%
of its saturation.

The white textures found in the packs (`overlight`, `snow1`, `white`, `typoray3`, `ago_snowexplosion`, `lum_sun`)
are still the right suspects **if** white areas appear, but their being additive in the original renderer is a
hypothesis about draw state, not something measured. Testing it needs the render-state logging planned for the
proxy, not more stills.

## 16-bit packs decoded too

`tex16_*` use format `0x115c` (4444) and `0x235` (565), bpp 2, channels stored low-byte-first. Verified by
decoding `wood_light` from both a 16-bit and a 32-bit pack and getting the same lamp image. Full sweep: **4,058
textures decoded across all four packs**, both consume their payloads exactly.
